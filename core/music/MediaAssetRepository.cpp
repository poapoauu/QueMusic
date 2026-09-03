#include "MediaAssetRepository.h"
#include "PageCache.h"
#include "v2/ISourceProvidersV2.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QTimer>
#include <QtConcurrentRun>

namespace {
QString mediaKey(const MediaRefV2 &media)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject::fromVariantMap(mediaRefV2ToVariantMap(media)))
        .toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
bool eligible(SourceRegistry *registry,const QString &source)
{
    if (!registry) return false;
    for (const auto &d:registry->enabledInstances()) if (d.sourceInstanceId==source)
        return d.enabled && registry->pluginManager()
            && registry->pluginManager()->plugin(d.pluginPackageId).state==PluginState::Loaded;
    return false;
}
bool live(SourceSessionStateV2 state) { return state==SourceSessionStateV2::Ready || state==SourceSessionStateV2::Connecting; }
void disconnectAll(QList<QMetaObject::Connection> &connections)
{
    for (auto c:connections) QObject::disconnect(c);
    connections.clear();
}
}
struct MediaAssetRepository::Request {
    MediaRefV2 media;
    SourceActionV2 action;
    QPointer<IMusicSourceSessionV2> session;
    QUuid providerId;
    QList<QMetaObject::Connection> connections;
    bool invoking=false;
    bool dispatched=false;
    bool terminal=false;
    bool failureQueued=false;
    bool cacheInvalidated=false;
};
MediaAssetRepository::MediaAssetRepository(SourceRegistry *sources,ArtworkCache *artwork,QObject *parent)
    :QObject(parent),m_sources(sources),m_artwork(artwork?std::make_shared<ArtworkCache>(*artwork):std::make_shared<ArtworkCache>())
{
    if (!sources) return;
    connect(sources,&SourceRegistry::instanceChanged,this,&MediaAssetRepository::sourceChanged);
    connect(sources,&QObject::destroyed,this,[this] {
        m_lyrics.clear();
        for (const auto &id:m_requests.keys()) fail(id,SourceErrorKindV2::Unavailable);
    });
    if (sources->pluginManager()) connect(sources->pluginManager(),&PluginManager::pluginChanged,this,[this](const QString &package) {
        if (!m_sources || !m_sources->pluginManager() || m_sources->pluginManager()->plugin(package).state==PluginState::Loaded) return;
        for (const auto &d:m_sources->enabledInstances()) if (d.pluginPackageId==package) sourceChanged(d.sourceInstanceId);
    });
}
MediaAssetRepository::~MediaAssetRepository() { for (const auto &id:m_requests.keys()) cancel(id); }
QUuid MediaAssetRepository::requestArtwork(const MediaRefV2 &media) { return request(media,SourceActionV2::Artwork); }
QUuid MediaAssetRepository::requestLyrics(const MediaRefV2 &media) { return request(media,SourceActionV2::Lyrics); }
QUuid MediaAssetRepository::request(const MediaRefV2 &media,SourceActionV2 action)
{
    const auto id=QUuid::createUuid(); auto req=std::make_shared<Request>(); req->media=media; req->action=action;
    m_requests.insert(id,req);
    QTimer::singleShot(0,this,[this,id] { begin(id); });
    return id;
}
void MediaAssetRepository::begin(const QUuid &id)
{
    auto req=m_requests.value(id); if (!req) return;
    if (req->media.entityId.isEmpty() || req->media.accountId.isEmpty() || req->media.sourcePluginId.isEmpty()
        || int(req->media.entityType)<0 || int(req->media.entityType)>int(MediaEntityTypeV2::Directory)) {
        fail(id,SourceErrorKindV2::InvalidRequest); return;
    }
    if (!eligible(m_sources,req->media.sourceInstanceId)) { fail(id,SourceErrorKindV2::Unavailable); return; }
    const auto key=mediaKey(req->media);
    if (req->action==SourceActionV2::Lyrics) {
        const auto cached=m_lyrics.value(req->media.sourceInstanceId);
        if (cached.contains(key)) {
            m_requests.remove(id); emit lyricsReady(id,req->media,cached.value(key)); return;
        }
        dispatch(id); return;
    }
    auto *watcher=new QFutureWatcher<std::optional<QUrl>>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,id,watcher] {
        const auto url=watcher->result(); watcher->deleteLater();
        auto req=m_requests.value(id); if (!req || req->failureQueued) return;
        if (!eligible(m_sources,req->media.sourceInstanceId)) { fail(id,SourceErrorKindV2::Unavailable); return; }
        if (url && !req->cacheInvalidated) { m_requests.remove(id); emit artworkReady(id,req->media,*url); }
        else dispatch(id);
    });
    watcher->setFuture(QtConcurrent::run(musicCacheIoPool(),[cache=m_artwork,source=req->media.sourceInstanceId,key] { return cache->lookup(source,key); }));
}
void MediaAssetRepository::dispatch(const QUuid &id)
{
    auto req=m_requests.value(id); if (!req || req->dispatched || req->failureQueued) return;
    if (!eligible(m_sources,req->media.sourceInstanceId)) { fail(id,SourceErrorKindV2::Unavailable); return; }
    if (!req->session) {
        req->session=m_sources->sessionFor(req->media.sourceInstanceId);
        if (!m_requests.contains(id)) return;
        if (!req->session) { fail(id,SourceErrorKindV2::Unavailable); return; }
        auto identity=req->session->identity();
        if (identity.sourcePluginId!=req->media.sourcePluginId || identity.accountId!=req->media.accountId
            || identity.sourceInstanceId!=req->media.sourceInstanceId) { fail(id,SourceErrorKindV2::InvalidRequest); return; }
        req->connections.append(connect(req->session,&QObject::destroyed,this,[this,id] { fail(id,SourceErrorKindV2::Unavailable); }));
        req->connections.append(connect(req->session,&IMusicSourceSessionV2::stateChanged,this,[this,id](SourceSessionStateV2 state) {
            if (state==SourceSessionStateV2::Ready) dispatch(id);
            else if (!live(state)) fail(id,SourceErrorKindV2::Unavailable);
        }));
    }
    if (!live(req->session->state())) { fail(id,SourceErrorKindV2::Unavailable); return; }
    if (req->session->state()==SourceSessionStateV2::Connecting) return;
    auto provider=qobject_cast<IPlaybackProviderV2 *>(req->session.data());
    if (!provider) { fail(id,SourceErrorKindV2::Unsupported); return; }
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::requestStarted,this,[req](QUuid pid) {
        if (req->invoking && req->providerId.isNull()) req->providerId=pid;
    }));
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::actionCompleted,this,[this,id,req](QUuid pid,ActionResultV2 result) {
        if (!req->providerId.isNull() && req->providerId==pid && !req->terminal) complete(id,result);
    }));
    req->connections.append(connect(req->session,&IMusicSourceSessionV2::requestFailed,this,[this,id,req](QUuid pid,SourceErrorV2 error) {
        if (!req->providerId.isNull() && req->providerId==pid && !req->terminal) {
            req->terminal=true;
            fail(id,error.kind);
        }
    }));
    req->dispatched=true; req->invoking=true;
    const auto returned=req->action==SourceActionV2::Artwork ? provider->fetchArtwork(req->media) : provider->fetchLyrics(req->media);
    req->invoking=false;
    if (m_requests.contains(id) && !req->terminal && (returned.isNull() || returned!=req->providerId)) fail(id,SourceErrorKindV2::InvalidRequest);
}
void MediaAssetRepository::complete(const QUuid &id,const ActionResultV2 &result)
{
    auto req=m_requests.value(id); if (!req || req->terminal) return;
    req->terminal=true; disconnectAll(req->connections);
    if (result.action!=req->action || result.subject!=req->media) { fail(id,SourceErrorKindV2::InvalidRequest); return; }
    if (req->action==SourceActionV2::Lyrics) {
        auto value=result.payload.value(MediaAssetPayloadV2::Lyrics);
        if (value.metaType().id()!=QMetaType::QString) { fail(id,SourceErrorKindV2::InvalidRequest); return; }
        const auto text=value.toString();
        MediaAssetPayloadV2::LyricsValue lyrics{QString(text.constData(),text.size())};
        // Defer even inline terminals: plugin code must unwind before consumer
        // callbacks can close the registry/reload the package.
        QTimer::singleShot(0,this,[this,id,lyrics] {
            auto req=m_requests.value(id); if (!req || req->failureQueued) return;
            m_requests.remove(id);
            m_lyrics[req->media.sourceInstanceId].insert(mediaKey(req->media),lyrics.lyrics);
            emit lyricsReady(id,req->media,lyrics.lyrics);
        });
        return;
    }
    auto bytes=result.payload.value(MediaAssetPayloadV2::Bytes), mime=result.payload.value(MediaAssetPayloadV2::MimeType);
    if (bytes.metaType().id()!=QMetaType::QByteArray || mime.metaType().id()!=QMetaType::QString) { fail(id,SourceErrorKindV2::InvalidRequest); return; }
    auto raw=bytes.toByteArray(); auto type=mime.toString();
    MediaAssetPayloadV2::ArtworkValue value{QByteArray(raw.constData(),raw.size()),QString(type.constData(),type.size())};
    auto *watcher=new QFutureWatcher<QUrl>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,id,watcher] {
        auto url=watcher->result(); watcher->deleteLater();
        auto req=m_requests.value(id); if (!req || req->failureQueued) return;
        if (url.isEmpty()) { fail(id,SourceErrorKindV2::Unavailable); return; }
        m_requests.remove(id); emit artworkReady(id,req->media,url);
    });
    watcher->setFuture(QtConcurrent::run(musicCacheIoPool(),[cache=m_artwork,source=req->media.sourceInstanceId,key=mediaKey(req->media),value] {
        return cache->store(source,key,value.bytes,value.mimeType);
    }));
}
void MediaAssetRepository::cancel(const QUuid &id)
{
    auto req=m_requests.take(id); if (!req) return;
    disconnectAll(req->connections);
    if (!req->terminal && req->session && !req->providerId.isNull()) req->session->cancel(req->providerId);
}
void MediaAssetRepository::fail(const QUuid &id,SourceErrorKindV2 kind)
{
    auto req=m_requests.value(id); if (!req || req->failureQueued) return;
    req->failureQueued=true;
    disconnectAll(req->connections);
    const bool pending=!req->terminal;
    req->terminal=true;
    QPointer<MediaAssetRepository> guard(this);
    if (pending && req->session && !req->providerId.isNull()) req->session->cancel(req->providerId);
    if (!guard) return;
    // Keep the queued failure cancellable until the public callback consumes it.
    QTimer::singleShot(0,this,[this,id,kind] {
        if (!m_requests.take(id)) return;
        emit failed(id,{kind,QStringLiteral("music.assetRequestFailed"),{}, {},kind!=SourceErrorKindV2::InvalidRequest});
    });
}
void MediaAssetRepository::sourceChanged(const QString &source)
{
    m_lyrics.remove(source);
    (void)QtConcurrent::run(musicCacheIoPool(),[cache=m_artwork,source] { cache->invalidateSource(source); });
    for (const auto &id:m_requests.keys()) {
        auto req=m_requests.value(id); if (!req || req->media.sourceInstanceId!=source) continue;
        req->cacheInvalidated=true;
        if (!eligible(m_sources,source) || (req->session && !live(req->session->state()))
            || (req->dispatched && !req->session)) fail(id,SourceErrorKindV2::Unavailable);
    }
}
