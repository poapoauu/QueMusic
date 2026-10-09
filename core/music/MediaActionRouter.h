#pragma once
#include "CapabilityResolver.h"
#include "SourceRegistry.h"
#include <memory>

// Owner-thread API. Registry and its PluginManager must outlive synchronous calls.
// Map inputs are complete MusicPageModel::itemAt() values. Signals are deferred.
class MediaActionRouter final : public QObject {
    Q_OBJECT
public:
    explicit MediaActionRouter(SourceRegistry *sources, QObject *parent = nullptr);
    ~MediaActionRouter() override;
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &media, bool favorite);
    Q_INVOKABLE QUuid setRating(const QVariantMap &media, int rating);
    Q_INVOKABLE QUuid download(const QVariantMap &media, const QUrl &destination);
    // Host-only: accept cancellation of an owned, still-pending Download.
    // Terminal delivery and provider cancellation remain deferred and lease-protected.
    bool cancelDownload(const QUuid &requestId);
    Q_INVOKABLE QUuid createPlaylist(const QString &sourceInstanceId, const QString &name);
    // change: optional newName:string, tracksToAdd:list<full item>,
    // trackIndexesToRemove:list<nonnegative unique int>. At least one change.
    Q_INVOKABLE QUuid updatePlaylist(const QVariantMap &playlist, const QVariantMap &change);
    Q_INVOKABLE QUuid deletePlaylist(const QVariantMap &playlist);
    Q_INVOKABLE QUuid setBookmark(const QVariantMap &media, qint64 positionMs);
signals:
    void actionSucceeded(QUuid requestId, QVariantMap result);
    void actionFailed(QUuid requestId, QVariantMap error);
private:
    struct Request;
    QUuid submit(std::shared_ptr<Request> request);
    void dispatch(const std::shared_ptr<Request> &request);
    void schedule(const std::shared_ptr<Request> &request);
    void invalidate(const std::shared_ptr<Request> &request);
    QPointer<SourceRegistry> m_sources;
    CapabilityResolver m_resolver;
    QHash<QUuid, std::shared_ptr<Request>> m_requests;
};
