#include "PageRepository.h"
#include "v2/ISourceProvidersV2.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
SourceErrorV2 error(SourceErrorKindV2 kind) { return {kind,QStringLiteral("music.sourceRequestFailed"),{}, {},kind!=SourceErrorKindV2::InvalidRequest}; }
bool liveState(SourceSessionStateV2 s) { return s==SourceSessionStateV2::Ready || s==SourceSessionStateV2::Connecting; }
bool eligible(SourceRegistry *sources,const QString &source)
{
    if (!sources) return false;
    for (const auto &d:sources->enabledInstances())
        if (d.sourceInstanceId==source)
            return d.enabled && sources->pluginManager()
                && sources->pluginManager()->plugin(d.pluginPackageId).state==PluginState::Loaded;
    return false;
}
void disconnectAll(QList<QMetaObject::Connection> &connections)
{
    for (auto c:connections) QObject::disconnect(c);
    connections.clear();
}
}
struct PageRepository::Request {
    QPointer<IMusicSourceSessionV2> session;
    QUuid providerId;
    QList<QMetaObject::Connection> connections;
    bool invoking=false;
    bool dispatched=false;
    bool done=false;
    SourcePageResultV2 result;
};
struct PageRepository::Group {
    PageCacheKeyV2 key;
    quint64 generation=0;
    quint64 revision=0;
    QString scope;
    QHash<QString,std::shared_ptr<Request>> requests;
    std::optional<AggregateCursorState> continuation;
    bool cacheable=true;
    bool cacheInvalidated=false;
    bool fannedOut=false;
    bool finishing=false;
};

PageRepository::PageRepository(SourceRegistry *sources,AggregateComposer *composer,
                               QObject *parent,std::shared_ptr<PageCache> cache)
    :QObject(parent),m_sources(sources),m_composer(composer),m_cache(cache?std::move(cache):std::make_shared<PageCache>())
{
    if (!sources) return;
    connect(sources,&SourceRegistry::instanceChanged,this,&PageRepository::sourceChanged);
    connect(sources,&QObject::destroyed,this,[this] {
        for (const auto &id:m_groups.keys()) fail(id,error(SourceErrorKindV2::Unavailable));
    });
    if (sources->pluginManager()) connect(sources->pluginManager(),&PluginManager::pluginChanged,this,[this](const QString &package) {
        if (!m_sources || !m_sources->pluginManager() || m_sources->pluginManager()->plugin(package).state==PluginState::Loaded) return;
        for (const auto &d:m_sources->enabledInstances()) if (d.pluginPackageId==package) sourceChanged(d.sourceInstanceId);
    });
}
PageRepository::~PageRepository() { for (const auto &id:m_groups.keys()) cancel(id); }

QUuid PageRepository::requestPage(const PageQueryV2 &query,quint64 generation)
{
    const auto id=QUuid::createUuid();
    auto group=std::make_shared<Group>(); group->key.query=query; group->generation=generation;
    m_groups.insert(id,group);
    QTimer::singleShot(0,this,[this,id] { begin(id); });
    return id;
}
void PageRepository::begin(const QUuid &id)
{
    auto g=m_groups.value(id); if (!g) return;
    const auto &q=g->key.query;
    if (!m_sources || !m_composer) { fail(id,error(SourceErrorKindV2::Unavailable)); return; }
    if (q.limit<1 || q.limit>1000 || int(q.page)<0 || int(q.page)>int(MusicPageKindV2::Search)
        || int(q.section)<0 || int(q.section)>int(PageSectionKindV2::SearchResults)) {
        fail(id,error(SourceErrorKindV2::InvalidRequest)); return;
    }
    for (auto it=q.filters.begin();it!=q.filters.end();++it) {
        switch (it->metaType().id()) {
        case QMetaType::QString: case QMetaType::Bool: case QMetaType::Int:
        case QMetaType::LongLong: case QMetaType::Double: break;
        default: fail(id,error(SourceErrorKindV2::InvalidRequest)); return;
        }
        if (!QStringList{"genre","artistId","albumId","year","sort","favorite"}.contains(it.key())) g->cacheable=false;
    }
    for (const auto &d:m_sources->enabledInstances())
        if (d.enabled && (q.scope.isAggregate() || q.scope.sourceInstanceId==d.sourceInstanceId))
            g->key.sourceInstanceIds.append(d.sourceInstanceId);
    g->key.sourceInstanceIds.sort(); g->key.sourceInstanceIds.removeDuplicates();
    if (!q.scope.isAggregate() && g->key.sourceInstanceIds.isEmpty()) { fail(id,error(SourceErrorKindV2::Unavailable)); return; }
    g->scope=PageCache::queryScope(g->key);
    if (!q.cursor.isEmpty()) {
        g->continuation=m_composer->decodeCursor(q.cursor,g->scope);
        if (!g->continuation) { fail(id,error(SourceErrorKindV2::InvalidRequest)); return; }
        g->cacheable=false; // Session-bound continuations cannot survive disk/restart.
    }
    if (!g->cacheable || g->key.sourceInstanceIds.isEmpty()) { fanOut(id); return; }
    auto *watcher=new QFutureWatcher<std::optional<CachedPageV2>>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,id,watcher] {
        auto cached=watcher->result(); watcher->deleteLater();
        auto g=m_groups.value(id); if (!g) return;
        QPointer<PageRepository> guard(this);
        if (cached && !g->cacheInvalidated) emit pageReady(id,g->generation,cached->page);
        if (guard && m_groups.contains(id)) fanOut(id);
    });
    watcher->setFuture(QtConcurrent::run(musicCacheIoPool(),[cache=m_cache,key=g->key] {
        return cache->lookup(key,QDateTime::currentDateTimeUtc(),std::chrono::minutes(5));
    }));
}
void PageRepository::fanOut(const QUuid &id)
{
    auto g=m_groups.value(id); if (!g) return;
    g->fannedOut=true;
    // Populate every source before invoking a potentially inline provider.
    for (const auto &source:g->key.sourceInstanceIds) {
        auto req=std::make_shared<Request>(); req->result.sourceInstanceId=source;
        if (g->continuation) {
            for (const auto &prior:g->continuation->buffered) if (prior.sourceInstanceId==source) {
                req->result=prior;
                bool buffered=false;
                for (const auto &s:prior.page.sections) buffered|=!s.items.isEmpty();
                req->done=buffered || g->continuation->exhaustedSources.contains(source);
            }
        }
        g->requests.insert(source,req);
    }
    for (const auto &source:g->key.sourceInstanceIds) {
        if (!m_groups.contains(id)) return;
        if (!g->requests[source]->done) dispatch(id,source);
    }
    finish(id);
}
void PageRepository::dispatch(const QUuid &id,const QString &source)
{
    auto g=m_groups.value(id); if (!g) return;
    auto req=g->requests.value(source); if (!req || req->done || req->dispatched) return;
    if (!eligible(m_sources,source)) { receive(id,source,{},error(SourceErrorKindV2::Unavailable)); return; }
    if (!req->session) {
        req->session=m_sources->sessionFor(source); // borrowed; NEVER delete/reparent
        if (!m_groups.contains(id) || req->done) return;
        if (!req->session) { receive(id,source,{},error(SourceErrorKindV2::Unavailable)); return; }
        auto session=req->session;
        req->connections.append(connect(session,&QObject::destroyed,this,[this,id,source] { receive(id,source,{},error(SourceErrorKindV2::Unavailable)); }));
        req->connections.append(connect(session,&IMusicSourceSessionV2::stateChanged,this,[this,id,source](SourceSessionStateV2 state) {
            if (state==SourceSessionStateV2::Ready) dispatch(id,source);
            else if (!liveState(state)) receive(id,source,{},error(SourceErrorKindV2::Unavailable));
        }));
    }
    if (!liveState(req->session->state())) { receive(id,source,{},error(SourceErrorKindV2::Unavailable)); return; }
    if (req->session->state()==SourceSessionStateV2::Connecting) return;
    auto provider=qobject_cast<IPageProviderV2 *>(req->session.data());
    if (!provider) { receive(id,source,{},error(SourceErrorKindV2::Unsupported)); return; }
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::requestStarted,this,[req](QUuid pid) {
        if (req->invoking && req->providerId.isNull()) req->providerId=pid;
    }));
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::pageReady,this,[this,id,source,req](QUuid pid,PageResultV2 page) {
        if (!req->providerId.isNull() && pid==req->providerId) receive(id,source,page);
    }));
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::requestFailed,this,[this,id,source,req](QUuid pid,SourceErrorV2 e) {
        if (!req->providerId.isNull() && pid==req->providerId) receive(id,source,{},error(e.kind));
    }));
    auto query=g->key.query; query.scope.sourceInstanceId=source;
    query.cursor=g->continuation?g->continuation->sourceCursors.value(source):QString{};
    req->dispatched=true; req->invoking=true;
    const auto returned=provider->fetchPage(query);
    req->invoking=false;
    if (!req->done && m_groups.contains(id) && (returned.isNull() || returned!=req->providerId))
        receive(id,source,{},error(SourceErrorKindV2::InvalidRequest));
}
void PageRepository::receive(const QUuid &id,const QString &source,PageResultV2 page,
                              std::optional<SourceErrorV2> failure)
{
    auto g=m_groups.value(id); if (!g) return;
    auto req=g->requests.value(source); if (!req || req->done) return;
    // One terminal counts as one query even when the initial response contains
    // multiple standard sections. Ruling12 continuations are section-specific.
    if (!failure && (!page.complete || page.cached)) failure=error(SourceErrorKindV2::InvalidRequest);
    QSet<PageSectionKindV2> kinds;
    if (!failure) for (const auto &s:page.sections) {
        if (int(s.kind)<0 || int(s.kind)>int(PageSectionKindV2::SearchResults)
            || kinds.contains(s.kind) || (s.hasMore && s.nextCursor.isEmpty())
            || (!g->key.query.cursor.isEmpty() && s.kind!=g->key.query.section))
            failure=error(SourceErrorKindV2::InvalidRequest);
        kinds.insert(s.kind);
        for (const auto &i:s.items) if (i.ref.sourceInstanceId!=source) failure=error(SourceErrorKindV2::InvalidRequest);
    }
    req->done=true; disconnectAll(req->connections);
    req->result.error=failure;
    if (!failure) {
        req->result.page=PageCache::sanitized(page); // drops QObject-bearing/unknown metadata
        for (int i=0;i<req->result.page.sections.size();++i) {
            auto &clean=req->result.page.sections[i]; const auto &raw=page.sections[i];
            clean.nextCursor=QString(raw.nextCursor.constData(),raw.nextCursor.size());
            clean.hasMore=raw.hasMore;
        }
    }
    ++g->revision;
    QTimer::singleShot(0,this,[this,id] { finish(id); });
}
void PageRepository::finish(const QUuid &id)
{
    auto g=m_groups.value(id); if (!g || !g->fannedOut || g->finishing) return;
    QList<SourcePageResultV2> inputs;
    int success=0;
    for (const auto &source:g->key.sourceInstanceIds) {
        auto req=g->requests.value(source); if (!req || !req->done) return;
        inputs.append(req->result); if (!req->result.error) ++success;
    }
    if (!inputs.isEmpty() && !success) { fail(id,inputs.first().error.value()); return; }
    QHash<PageSectionKindV2,QString> sectionScopes;
    for (const auto &input:inputs) for (const auto &section:input.page.sections) {
        auto key=g->key;
        key.query.section=section.kind;
        sectionScopes.insert(section.kind,PageCache::queryScope(key));
    }
    auto result=m_composer->compose(inputs,g->key.query.limit,g->key.query.cursor,g->scope,sectionScopes);
    if (!result.complete) { fail(id,error(SourceErrorKindV2::InvalidRequest)); return; }
    result.cached=false; result.complete=true;
    if (!g->cacheable || inputs.isEmpty()) {
        m_groups.remove(id); emit pageReady(id,g->generation,result); return;
    }
    g->finishing=true;
    auto *watcher=new QFutureWatcher<bool>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,id,watcher,result,revision=g->revision] {
        watcher->deleteLater(); auto g=m_groups.value(id); if (!g) return;
        g->finishing=false;
        if (g->revision!=revision) { finish(id); return; }
        m_groups.remove(id); emit pageReady(id,g->generation,result);
    });
    watcher->setFuture(QtConcurrent::run(musicCacheIoPool(),[cache=m_cache,key=g->key,page=PageCache::sanitized(result)] {
        return cache->store(key,page,QDateTime::currentDateTimeUtc());
    }));
}
void PageRepository::cancel(const QUuid &id)
{
    auto g=m_groups.take(id); if (!g) return;
    for (const auto &req:g->requests) {
        disconnectAll(req->connections);
        if (!req->done && req->session && !req->providerId.isNull()) req->session->cancel(req->providerId);
    }
}
void PageRepository::fail(const QUuid &id,SourceErrorV2 e)
{
    auto g=m_groups.value(id); if (!g) return;
    const auto generation=g->generation;
    cancel(id); emit pageFailed(id,generation,e);
}
void PageRepository::sourceChanged(const QString &source)
{
    if (m_composer) m_composer->invalidateSource(source);
    (void)QtConcurrent::run(musicCacheIoPool(),[cache=m_cache,source] { cache->invalidateSource(source); });
    for (const auto &id:m_groups.keys()) {
        auto g=m_groups.value(id); if (!g) continue;
        if (g->key.sourceInstanceIds.contains(source)) g->cacheInvalidated=true;
        auto req=g->requests.value(source); if (!req) continue;
        if (eligible(m_sources,source) && ((!req->session && !req->dispatched)
            || (req->session && liveState(req->session->state())))) continue;
        const auto session=req->session; const auto providerId=req->providerId;
        const bool pending=!req->done;
        disconnectAll(req->connections);
        req->done=false; req->result.page={};
        receive(id,source,{},error(SourceErrorKindV2::Unavailable));
        if (pending && session && !providerId.isNull()) session->cancel(providerId);
    }
}
