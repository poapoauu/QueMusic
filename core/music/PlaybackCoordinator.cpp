#include "PlaybackCoordinator.h"
#include "CapabilityResolver.h"
#include "QueueHistoryCodec.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QDir>
#include <QTimer>
#include <cmath>
#include <limits>

namespace {
QString owned(const QString &s) { return QString(s.constData(),s.size()); }
MediaRefV2 copyRef(const MediaRefV2 &r) {
    return {owned(r.sourcePluginId),owned(r.sourceInstanceId),owned(r.accountId),r.entityType,owned(r.entityId)};
}
bool integer(const QVariant &v,qint64 min,qint64 max) {
    switch(v.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Double: break;
    default:return false;
    }
    const long double n=v.metaType().id()==QMetaType::ULongLong?static_cast<long double>(v.toULongLong()):
        v.metaType().id()==QMetaType::Double?static_cast<long double>(v.toDouble()):static_cast<long double>(v.toLongLong());
    return std::isfinite(n)&&std::floor(n)==n&&n>=min&&n<=max;
}
void disconnectAll(QList<QMetaObject::Connection> &connections) {
    const auto old=std::exchange(connections,{});
    for(const auto &c:old) QObject::disconnect(c);
}
bool validStream(const StreamDescriptorV2 &s,const MediaRefV2 &ref) {
    if (!(s.media==ref)||s.url.isEmpty()||!s.url.isValid()||s.url.isRelative()
        ||(s.expiresAt.isValid()&&s.expiresAt<=QDateTime::currentDateTimeUtc())) return false;
    if(s.url.scheme()=="file") return s.url.host().isEmpty()&&s.url.userInfo().isEmpty()
        &&!s.url.hasQuery()&&!s.url.hasFragment()&&QDir::isAbsolutePath(s.url.toLocalFile());
    return (s.url.scheme()=="https"||s.url.scheme()=="http")&&!s.url.host().isEmpty();
}
StreamDescriptorV2 copyStream(const StreamDescriptorV2 &s) {
    StreamDescriptorV2 out; out.media=copyRef(s.media); out.url=QUrl::fromEncoded(s.url.toEncoded());
    out.mimeType=owned(s.mimeType); out.expiresAt=s.expiresAt; out.seekable=s.seekable;
    for(auto it=s.headers.cbegin();it!=s.headers.cend();++it)out.headers.insert(owned(it.key()),owned(it.value()));
    return out;
}
}

struct PlaybackCoordinator::Entry {
    QUuid occurrence=QUuid::createUuid();
    MediaRefV2 ref;
    QHash<SourceActionV2,ActionAvailabilityV2> actions;
    QVariantMap presentation;
    qint64 duration=0;
    QVariantMap map() const { auto out=presentation; out.insert("ref",mediaRefV2ToVariantMap(ref)); out.insert("occurrenceId",occurrence); return out; }
    bool parse(const QVariantMap &m) {
        if(m.value("ref").metaType().id()!=QMetaType::QVariantMap||m.value("availableActions").metaType().id()!=QMetaType::QVariantMap)return false;
        const auto r=m.value("ref").toMap();
        for(auto key:{"sourcePluginId","sourceInstanceId","accountId","entityId"})
            if(r.value(key).metaType().id()!=QMetaType::QString||r.value(key).toString().trimmed().isEmpty())return false;
        if(!integer(r.value("entityType"),int(MediaEntityTypeV2::Track),int(MediaEntityTypeV2::Track)))return false;
        ref=copyRef(mediaRefV2FromVariantMap(r));
        const auto input=m.value("availableActions").toMap();
        for(auto it=input.cbegin();it!=input.cend();++it) {
            bool ok=false; int n=it.key().toInt(&ok);
            if(!ok||n<0||n>int(SourceActionV2::DeleteBookmark)||QString::number(n)!=it.key()||it->metaType().id()!=QMetaType::QVariantMap)return false;
            const auto value=it->toMap();
            if(!integer(value.value("state"),0,int(AvailabilityV2::Forbidden))||value.value("constraints").metaType().id()!=QMetaType::QVariantMap
                ||(value.contains("reasonKey")&&value.value("reasonKey").metaType().id()!=QMetaType::QString))return false;
            auto a=intersectActionAvailabilityV2({{AvailabilityV2(value.value("state").toInt()),{},value.value("constraints").toMap()}});
            a.reasonKey={}; // Plugin reason strings are never retained or exposed.
            // The normalized values are scalars, but their keys may still be
            // QStringLiteral data in an unloadable plugin. Own those as well.
            QVariantMap constraints;
            for(auto c=a.constraints.cbegin();c!=a.constraints.cend();++c)
                constraints.insert(owned(c.key()),c.value());
            a.constraints=std::move(constraints);
            actions[SourceActionV2(n)]=a;
        }
        for(auto key:{"title","subtitle","album"}) {
            if(m.contains(key)&&m.value(key).metaType().id()!=QMetaType::QString)return false;
            if(m.contains(key))presentation[key]=owned(m.value(key).toString());
        }
        if(m.contains("artists")) {
            QStringList artists;
            if(m.value("artists").metaType().id()==QMetaType::QStringList) {
                for(const auto &artist:m.value("artists").toStringList())artists.append(owned(artist));
            } else if(m.value("artists").metaType().id()==QMetaType::QVariantList) {
                for(const auto &artist:m.value("artists").toList()) { if(artist.metaType().id()!=QMetaType::QString)return false; artists.append(owned(artist.toString())); }
            } else return false;
            presentation["artists"]=artists;
        }
        if(m.contains("durationMs")) {
            if(!integer(m.value("durationMs"),std::numeric_limits<qint64>::min(),std::numeric_limits<qint64>::max()))return false;
            duration=m.value("durationMs").toLongLong();
        }
        presentation["durationMs"]=duration;
        return true;
    }
};

struct PlaybackCoordinator::Pending {
    QUuid id;
    QSet<QUuid> started;
    struct Terminal { bool success=false; StreamDescriptorV2 stream; };
    QHash<QUuid,Terminal> inlineResults;
    Terminal terminal;
    QList<QMetaObject::Connection> connections;
    bool stream=true,submission=false,invoking=false,done=false,finished=false,queued=false;
    qint64 position=0;
    ~Pending() { disconnectAll(connections); }
};
struct PlaybackCoordinator::Active {
    // Declared first so plugin-backed temporaries are destroyed before the lease.
    PluginLease lease;
    QUuid generation=QUuid::createUuid();
    std::shared_ptr<Entry> entry;
    int index=-1;
    QPointer<SourceRegistry> registry;
    QPointer<PluginManager> plugins;
    QPointer<QObject> root;
    QPointer<IMusicSourceSessionV2> session;
    QPointer<PlaybackSink> sink;
    QString package;
    QList<QMetaObject::Connection> connections;
    QList<std::shared_ptr<Pending>> pending;
    bool ended=false,playing=false,startAttempted=false,submissionAttempted=false;
    quint64 capabilityRevision=0;
    QVariantMap currentActions;
    ~Active() { disconnectAll(connections); }
    void cancel(const std::shared_ptr<Pending> &p) {
        if(p->invoking||p->finished||p->id.isNull()||!session||!registry||session->parent()!=registry)return;
        p->finished=true;
        auto keepCode=lease;
        session->cancel(p->id);
    }
};

PlaybackCoordinator::PlaybackCoordinator(SourceRegistry *sources,PlaybackSink *sink,QObject *parent)
    :QObject(parent),m_sources(sources),m_sink(sink) {
    if(sink)connect(sink,&QObject::destroyed,this,[this] { if(auto a=m_active)end(a,QStringLiteral("music.playbackUnavailable")); });
    if(sources)connect(sources,&QObject::destroyed,this,[this] { if(auto a=m_active)end(a,QStringLiteral("music.playbackUnavailable")); });
    if(sources)connect(sources,&SourceRegistry::instanceChanged,this,[this](const QString &) {
        QTimer::singleShot(0,this,[this] { emit queueChanged(); });
    });
}
PlaybackCoordinator::~PlaybackCoordinator() {
    m_destroying=true;
    if(auto a=m_active)end(a);
}
void PlaybackCoordinator::notifyCurrent() { QTimer::singleShot(0,this,[this] { emit currentChanged(); }); }
QVariantMap PlaybackCoordinator::publicEntry(const std::shared_ptr<Entry> &entry) const {
    auto out=entry->map();
    QString label=entry->ref.sourcePluginId;
    bool available=false;
    if(m_sources) {
        int matches=0;
        for(const auto &source:m_sources->enabledInstances()) {
            if(source.sourceInstanceId!=entry->ref.sourceInstanceId)continue;
            ++matches;
            if(source.sourceId==entry->ref.sourcePluginId && source.accountId==entry->ref.accountId
                && !source.displayName.isEmpty())label=source.displayName;
            if(source.sourceId==entry->ref.sourcePluginId && source.accountId==entry->ref.accountId
                && source.enabled && source.state==SourceSessionStateV2::Ready) {
                available=true;
            }
        }
        if(matches!=1)available=false;
    }
    out.insert("sourceLabel",label);
    // Presentation only: avoid invoking plugin capability code from a QML
    // property read. allowed() rechecks current rights before resolving media.
    out.insert("unavailable",!available
        || entry->actions.value(SourceActionV2::Play).state!=AvailabilityV2::Available);
    return out;
}
QVariantList PlaybackCoordinator::queue() const { QVariantList out; for(const auto &e:m_queue)out.append(publicEntry(e)); return out; }
QVariantMap PlaybackCoordinator::currentItem() const { return m_active?publicEntry(m_active->entry):QVariantMap{}; }
QVariantMap PlaybackCoordinator::currentActionItem() const {
    if (!m_active) return {};
    auto item = publicEntry(m_active->entry);
    if (item.value("unavailable").toBool()) return {};
    item.insert("availableActions", m_active->currentActions);
    return item;
}
int PlaybackCoordinator::currentIndex() const { return m_active?m_active->index:-1; }
QUuid PlaybackCoordinator::currentGeneration() const { return m_active?m_active->generation:QUuid{}; }
QUuid PlaybackCoordinator::currentOccurrence() const { return m_active?m_active->entry->occurrence:QUuid{}; }
QList<QueueOccurrence> PlaybackCoordinator::exportQueue() const {
    QList<QueueOccurrence> out;
    for(const auto &entry:m_queue) {
        QueueOccurrence item;
        item.occurrenceId=entry->occurrence;
        item.ref=copyRef(entry->ref);
        item.title=entry->presentation.value("title").toString();
        item.artists=entry->presentation.value("artists").toStringList();
        item.album=entry->presentation.value("album").toString();
        item.durationMs=entry->duration;
        item.playableAtEnqueue=entry->actions.value(SourceActionV2::Play).state==AvailabilityV2::Available;
        out.append(std::move(item));
    }
    return out;
}
bool PlaybackCoordinator::restoreQueue(const QList<QueueOccurrence> &items) {
    if(m_destroying||m_active)return false;
    QueueHistorySnapshot snapshot;
    snapshot.queue=items;
    if(!QueueHistoryCodec::encode(snapshot))return false;
    QList<std::shared_ptr<Entry>> restored;
    restored.reserve(items.size());
    for(const auto &item:items) {
        auto entry=std::make_shared<Entry>();
        entry->occurrence=item.occurrenceId;
        entry->ref=copyRef(item.ref);
        entry->duration=item.durationMs;
        entry->presentation.insert("title",owned(item.title));
        QStringList artists;
        for(const auto &artist:item.artists)artists.append(owned(artist));
        entry->presentation.insert("artists",artists);
        entry->presentation.insert("album",owned(item.album));
        entry->presentation.insert("durationMs",item.durationMs);
        if(item.playableAtEnqueue)
            entry->actions.insert(SourceActionV2::Play,{AvailabilityV2::Available,{},{}});
        restored.append(std::move(entry));
    }
    m_queue=std::move(restored);
    QTimer::singleShot(0,this,[this] { emit queueChanged(); });
    return true;
}
bool PlaybackCoordinator::removeOccurrence(const QUuid &id) {
    if(m_destroying||id.isNull())return false;
    for(int i=0;i<m_queue.size();++i) {
        if(m_queue.at(i)->occurrence!=id)continue;
        if(m_active&&m_active->entry==m_queue.at(i))return false;
        m_queue.removeAt(i);
        if(m_active&&m_active->index>i) {
            --m_active->index;
            notifyCurrent();
        }
        QTimer::singleShot(0,this,[this] { emit queueChanged(); });
        return true;
    }
    return false;
}
QUuid PlaybackCoordinator::enqueue(const QVariantMap &map) {
    if(m_destroying)return {};
    auto e=std::make_shared<Entry>(); if(!e->parse(map))return {};
    m_queue.append(e); QTimer::singleShot(0,this,[this] { emit queueChanged(); }); return e->occurrence;
}
QUuid PlaybackCoordinator::play(const QVariantMap &map) {
    if(enqueue(map).isNull())return {};
    return playQueueEntry(m_queue.size()-1);
}
bool PlaybackCoordinator::current(const std::shared_ptr<Active> &a) const {
    return m_active==a&&!a->ended&&a->registry&&a->plugins&&a->root&&a->session&&a->sink
        &&a->session->parent()==a->registry&&a->plugins->pluginInstance(a->package)==a->root
        &&a->plugins->plugin(a->package).state==PluginState::Loaded;
}
QUuid PlaybackCoordinator::playQueueEntry(int index) {
    if(m_destroying||index<0||index>=m_queue.size())return {};
    const auto entry=m_queue[index]; const QPointer<PlaybackCoordinator> guard(this);
    stop(); if(!guard||m_active)return {}; // A sink stop may have started a newer play.
    auto a=std::make_shared<Active>(); a->entry=entry; a->index=index; a->registry=m_sources; a->sink=m_sink;
    m_active=a; const auto id=a->generation; notifyCurrent();
    resolve(a);
    return id;
}
bool PlaybackCoordinator::allowed(const std::shared_ptr<Active> &a,SourceActionV2 action) {
    const QPointer<PlaybackCoordinator> guard(this);
    auto usable=[&] { return guard&&current(a); };
    if(!usable())return false;
    const auto revision=a->capabilityRevision;
    const auto owners=a->registry->enabledInstances(); if(!usable())return false;
    int matches=0;
    for(const auto &o:owners)if(o.sourceInstanceId==a->entry->ref.sourceInstanceId) {
        if(!o.enabled||o.state!=SourceSessionStateV2::Ready||o.pluginPackageId!=a->package
            ||o.sourceId!=a->entry->ref.sourcePluginId||o.accountId!=a->entry->ref.accountId)return false;
        ++matches;
    }
    if(matches!=1)return false;
    const auto identity=a->session->identity(); if(!usable())return false;
    if(identity.sourcePluginId!=a->entry->ref.sourcePluginId||identity.sourceInstanceId!=a->entry->ref.sourceInstanceId||identity.accountId!=a->entry->ref.accountId)return false;
    auto plugin=qobject_cast<IMusicSourcePluginV2 *>(a->root); if(!plugin)return false;
    const auto descriptor=plugin->descriptor(); if(!usable())return false;
    if(descriptor.sourceId!=a->entry->ref.sourcePluginId||descriptor.pluginPackageId!=a->package)return false;
    const auto caps=a->session->capabilities(); if(!usable()||revision!=a->capabilityRevision)return false;
    const auto resolved=CapabilityResolver{}.resolve(action,descriptor.declaredActions.value(action),caps.serverAction(action),caps.accountAction(action),a->entry->actions.value(action));
    return resolved.state==AvailabilityV2::Available&&!resolved.constraints.contains("maxBitrate");
}
bool PlaybackCoordinator::refreshCurrentActions(const std::shared_ptr<Active> &a) {
    const QPointer<PlaybackCoordinator> guard(this);
    const auto revision = a->capabilityRevision;
    QVariantMap actions;
    for (auto action : {SourceActionV2::Favorite, SourceActionV2::Unfavorite, SourceActionV2::Download}) {
        const bool provider = action == SourceActionV2::Download
            ? bool(qobject_cast<IDownloadProviderV2 *>(a->session.data()))
            : bool(qobject_cast<IFavoriteProviderV2 *>(a->session.data()));
        const bool available = provider && allowed(a, action);
        if (!guard || !current(a) || revision != a->capabilityRevision) return false;
        actions.insert(QString::number(int(action)), QVariantMap{
            {"state", int(available ? AvailabilityV2::Available : AvailabilityV2::Unavailable)},
            {"reasonKey", QString{}}, {"constraints", QVariantMap{}}});
    }
    a->currentActions = actions;
    notifyCurrent();
    return true;
}
void PlaybackCoordinator::resolve(const std::shared_ptr<Active> &a) {
    const QPointer<PlaybackCoordinator> guard(this);
    auto fail=[&] { if(guard)end(a,QStringLiteral("music.playbackUnavailable")); };
    if(!a->registry||!a->sink||!a->registry->pluginManager()) { fail(); return; }
    a->plugins=a->registry->pluginManager();
    const auto owners=a->registry->enabledInstances(); if(!guard||a->ended||!a->registry||!a->plugins)return;
    int matches=0;
    for(const auto &o:owners)if(o.sourceInstanceId==a->entry->ref.sourceInstanceId) {
        if(!o.enabled||o.sourceId!=a->entry->ref.sourcePluginId||o.accountId!=a->entry->ref.accountId) { fail();return; }
        a->package=owned(o.pluginPackageId); ++matches;
    }
    if(matches!=1) { fail();return; }
    a->lease=a->plugins->acquire(a->package);
    if(!guard||a->ended||!a->registry||!a->plugins||!a->lease.isValid()) { fail();return; }
    a->root=a->plugins->pluginInstance(a->package);
    a->session=a->registry->sessionFor(a->entry->ref.sourceInstanceId);
    if(!guard||!current(a)) { fail();return; }
    a->connections.append(connect(a->session,&QObject::destroyed,this,[this,a] { end(a,QStringLiteral("music.playbackUnavailable")); }));
    a->connections.append(connect(a->session,&IMusicSourceSessionV2::stateChanged,this,[this,a](SourceSessionStateV2 state) {
        if(state!=SourceSessionStateV2::Ready)end(a,QStringLiteral("music.playbackUnavailable"));
    }));
    a->connections.append(connect(a->registry,&SourceRegistry::instanceChanged,this,[this,a](const QString &id) {
        if(id==a->entry->ref.sourceInstanceId)end(a,QStringLiteral("music.playbackUnavailable"));
    }));
    a->connections.append(connect(a->session,&IMusicSourceSessionV2::capabilitiesChanged,this,[this,a] {
        ++a->capabilityRevision;
        a->currentActions.clear();
        notifyCurrent();
        // Scrobble-only permission downgrades must not abort playback.
        QTimer::singleShot(0,this,[this,a] {
            const QPointer<PlaybackCoordinator> guard(this);
            if (a->ended) return;
            const bool playable = allowed(a,SourceActionV2::Play);
            if (!guard || !current(a)) return;
            if (!playable) end(a,QStringLiteral("music.playbackUnavailable"));
            else refreshCurrentActions(a);
        });
    }));
    if(!qobject_cast<IPlaybackProviderV2 *>(a->session)||!allowed(a,SourceActionV2::Play)) { fail();return; }
    if(!guard||!current(a))return;
    if (!refreshCurrentActions(a) || !guard || !current(a)) return;
    invoke(a,std::make_shared<Pending>());
}
void PlaybackCoordinator::invoke(const std::shared_ptr<Active> &a,const std::shared_ptr<Pending> &p) {
    const QPointer<PlaybackCoordinator> guard(this);
    a->pending.append(p);
    // Independent observer survives deletion of the coordinator inside a provider.
    QObject observer;
    connect(a->session,&IMusicSourceSessionV2::requestStarted,&observer,[p](QUuid id) {
        if(p->invoking&&!id.isNull()&&p->started.size()<16)p->started.insert(id);
    });
    auto terminal=[this,a,p](QUuid id,Pending::Terminal result) {
        if(a->ended||p->done||id.isNull())return;
        if(p->invoking) {
            if(p->started.contains(id)&&p->inlineResults.size()<16&&!p->inlineResults.contains(id))p->inlineResults.insert(id,std::move(result));
        } else if(id==p->id) {
            p->finished=true; p->done=true; p->terminal=std::move(result); settle(a,p);
        }
    };
    p->connections.append(connect(a->session,&IMusicSourceSessionV2::streamReady,this,[a,p,terminal](QUuid id,const StreamDescriptorV2 &stream) {
        const bool success=p->stream&&validStream(stream,a->entry->ref);
        terminal(id,{success,success?copyStream(stream):StreamDescriptorV2{}});
    }));
    p->connections.append(connect(a->session,&IMusicSourceSessionV2::actionCompleted,this,[a,p,terminal](QUuid id,const ActionResultV2 &result) {
        terminal(id,{!p->stream&&result.action==SourceActionV2::Scrobble&&result.subject==a->entry->ref,{}});
    }));
    p->connections.append(connect(a->session,&IMusicSourceSessionV2::requestFailed,this,[terminal](QUuid id,const SourceErrorV2 &) { terminal(id,{}); }));
    p->invoking=true;
    const auto returned=p->stream?qobject_cast<IPlaybackProviderV2 *>(a->session)->resolveStream(a->entry->ref):
        qobject_cast<IScrobbleProviderV2 *>(a->session)->scrobble(a->entry->ref,p->position,p->submission);
    p->invoking=false;
    p->id=p->started.contains(returned)?returned:QUuid{};
    if(p->id.isNull()) { p->done=true; }
    else if(p->inlineResults.contains(returned)) { p->done=true;p->finished=true;p->terminal=p->inlineResults.value(returned); }
    p->inlineResults.clear(); p->started.clear();
    if(!guard||a->ended) { a->cancel(p); disconnectAll(p->connections); return; }
    if(!current(a)) { end(a,QStringLiteral("music.playbackUnavailable"));return; }
    if(p->done)settle(a,p);
    else {
        // Each playback owns at most one resolve plus two scrobbles; timeout
        // captures weak state so completed requests/URLs are not retained.
        std::weak_ptr<Active> weakA=a; std::weak_ptr<Pending> weakP=p;
        QTimer::singleShot(30000,this,[this,weakA,weakP] {
            auto a=weakA.lock(); auto p=weakP.lock(); if(!a||!p||a->ended||p->done)return;
            p->done=true; settle(a,p);
        });
    }
}
void PlaybackCoordinator::settle(const std::shared_ptr<Active> &a,const std::shared_ptr<Pending> &p) {
    if(p->invoking||p->queued||a->ended)return; p->queued=true;
    QTimer::singleShot(0,this,[this,a,p] {
        const QPointer<PlaybackCoordinator> guard(this);
        disconnectAll(p->connections); a->pending.removeOne(p); a->cancel(p);
        if(!guard||!current(a))return;
        if(!p->stream) { emit scrobbleFinished(a->generation,p->submission,p->terminal.success); return; }
        if(!p->terminal.success||!validStream(p->terminal.stream,a->entry->ref)) { end(a,QStringLiteral("music.playbackInvalidStream"));return; }
        if(!allowed(a,SourceActionV2::Play)) { if(guard)end(a,QStringLiteral("music.playbackUnavailable"));return; }
        if(!guard||!current(a))return;
        // Never keep a resolved URL in active playback or public state.
        auto stream=std::exchange(p->terminal.stream,{});
        const bool prepared=a->sink->prepare(std::move(stream),a->generation);
        if(!guard||!current(a))return;
        if(!prepared) { end(a,QStringLiteral("music.playbackPrepareFailed"));return; }
        scrobble(a,0,false);
        if(!guard||!current(a))return;
        // Scrobble can synchronously revoke Play or close the owner.
        if(!allowed(a,SourceActionV2::Play)) { if(guard)end(a,QStringLiteral("music.playbackUnavailable"));return; }
        if(!guard||!current(a))return;
        a->playing=true; a->sink->play(a->generation);
        if(!guard||!current(a))return;
        emit playbackStarted(a->generation);
    });
}
void PlaybackCoordinator::scrobble(const std::shared_ptr<Active> &a,qint64 position,bool submission) {
    bool &attempted=submission?a->submissionAttempted:a->startAttempted;
    if(attempted)return; attempted=true;
    const QPointer<PlaybackCoordinator> guard(this);
    if(!current(a)||!qobject_cast<IScrobbleProviderV2 *>(a->session)||!allowed(a,SourceActionV2::Scrobble))return;
    if(!guard||!current(a))return;
    auto p=std::make_shared<Pending>(); p->stream=false;p->submission=submission;p->position=position;invoke(a,p);
}
bool PlaybackCoordinator::reportPosition(QUuid generation,qint64 positionMs) {
    auto a=m_active;
    if(!a||a->generation!=generation||!a->playing||positionMs<0)return false;
    const qint64 threshold=a->entry->duration>0?qMin(a->entry->duration/2,qint64(240000)):240000;
    if(positionMs>=threshold)scrobble(a,positionMs,true);
    return true;
}
bool PlaybackCoordinator::reportStopped(QUuid generation) {
    auto a=m_active;if(!a||a->generation!=generation)return false;end(a);return true;
}
bool PlaybackCoordinator::stop() { auto a=m_active;if(!a)return false;end(a);return true; }
void PlaybackCoordinator::end(const std::shared_ptr<Active> &a,const QString &error) {
    if(a->ended)return; a->ended=true;
    const QPointer<PlaybackCoordinator> guard(this);
    if(m_active==a)m_active.reset();
    disconnectAll(a->connections);
    const auto pending=std::exchange(a->pending,{});
    for(const auto &p:pending)disconnectAll(p->connections);
    if(a->sink)a->sink->stop(a->generation);
    for(const auto &p:pending)a->cancel(p);
    // a is also held by every invocation/terminal frame, preserving callable
    // code through nested stop, deletion, event processing, and cancellation.
    if(!guard)return;
    notifyCurrent();
    const auto id=a->generation;
    QTimer::singleShot(0,this,[this,id,error] {
        if(error.isEmpty())emit playbackStopped(id);else emit playbackFailed(id,error);
    });
}
