#pragma once

#include "NavidromeApiClient.h"
#include "v2/IMusicSourceSessionV2.h"
#include "v2/ISourceProvidersV2.h"

#include <QHash>
#include <QSet>

class NavidromeSourceSession final : public IMusicSourceSessionV2,
                                     public IPageProviderV2,
                                     public IPlaybackProviderV2,
                                     public IDownloadProviderV2,
                                     public IFavoriteProviderV2,
                                     public IRatingProviderV2,
                                     public IScrobbleProviderV2,
                                     public IPlaylistProviderV2,
                                     public IPlayQueueProviderV2,
                                     public IBookmarkProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2 IDownloadProviderV2
                 IFavoriteProviderV2 IRatingProviderV2 IScrobbleProviderV2 IPlaylistProviderV2
                 IPlayQueueProviderV2 IBookmarkProviderV2)

public:
    NavidromeSourceSession(SourceConfigurationV2 configuration,
                           QNetworkAccessManager *network, QObject *parent = nullptr);
    NavidromeSourceSession(SourceConfigurationV2 configuration,
                           QNetworkAccessManager *network,
                           NavidromeApiClient::SaltGenerator saltGenerator,
                           QObject *parent = nullptr);
    ~NavidromeSourceSession() override;

    SourceIdentityV2 identity() const override;
    SourceSessionStateV2 state() const override;
    CapabilitySetV2 capabilities() const override;
    QUuid open() override;
    void close() override;
    void cancel(const QUuid &requestId) override;
    QUuid fetchPage(const PageQueryV2 &query) override;
    QUuid resolveStream(const MediaRefV2 &media) override;
    QUuid fetchArtwork(const MediaRefV2 &media) override;
    QUuid fetchLyrics(const MediaRefV2 &media) override;
    QUuid download(const MediaRefV2 &media, const QUrl &destination) override;
    QUuid setFavorite(const MediaRefV2 &media, bool favorite) override;
    QUuid setRating(const MediaRefV2 &media, int rating) override;
    QUuid scrobble(const MediaRefV2 &media, qint64 positionMs, bool submission) override;
    QUuid createPlaylist(const QString &name, const QList<MediaRefV2> &tracks) override;
    QUuid updatePlaylist(const MediaRefV2 &playlist, const PlaylistChangeV2 &change) override;
    QUuid deletePlaylist(const MediaRefV2 &playlist) override;
    QUuid fetchPlayQueue() override;
    QUuid savePlayQueue(const QList<MediaRefV2> &items, const MediaRefV2 &current,
                        qint64 positionMs) override;
    QUuid fetchBookmarks() override;
    QUuid createBookmark(const MediaRefV2 &media, qint64 positionMs,
                         const QString &comment) override;
    QUuid deleteBookmark(const MediaRefV2 &media) override;

private:
    struct TrackMetadata { QString artist; QString title; };
    struct V2Request {
        QUuid publicId;
        QString operation;
        QString endpoint;
        PageQueryV2 pageQuery;
        MediaRefV2 media;
        QUrl destination;
        int offset = 0;
        ActionResultV2 result;
        QList<SourceActionV2> attemptedActions;
    };

    bool validEntity(const MediaRefV2 &media, MediaEntityTypeV2 type) const;
    MediaRefV2 collectionSubject() const;
    QUuid startAction(SourceActionV2 action, const MediaRefV2 &subject,
                      const QString &endpoint, const QUrlQuery &query,
                      QVariantMap payload = {}, bool valid = true,
                      QList<SourceActionV2> attempted = {}, bool foreign = false);
    void finishAction(const V2Request &request, const QJsonObject &response);

    void handleClientSuccess(const QUuid &requestId, const QString &operation,
                             const QJsonObject &response);
    void handleClientFailure(const QUuid &requestId, const SourceErrorV2 &error);
    void finishPage(const V2Request &request, const QJsonObject &response);
    QUuid scheduleV2Failure(const SourceErrorV2 &error, QUuid publicId = {});
    bool isOpenRequestActive(const QUuid &requestId) const;
    bool validMedia(const MediaRefV2 &media) const;
    bool validArtworkMedia(const MediaRefV2 &media) const;
    void startExtensions(const QUuid &requestId);
    void startCurrentUser(const QUuid &requestId);
    void finishOpenReady(const QUuid &requestId, bool validUser,
                         const QJsonObject &user = {});
    void failOpen(const QUuid &requestId, const SourceErrorV2 &error);
    void setState(SourceSessionStateV2 state);
    void setCapabilities(const CapabilitySetV2 &capabilities);
    static QHash<SourceActionV2, ActionAvailabilityV2> conservativeServerActions();
    static QHash<SourceActionV2, ActionAvailabilityV2> unavailableAccountActions();
    static QHash<SourceActionV2, ActionAvailabilityV2> accountActions(const QJsonObject &user);

    SourceConfigurationV2 m_configuration;
    NavidromeApiClient *m_client = nullptr;
    SourceSessionStateV2 m_state = SourceSessionStateV2::Closed;
    CapabilitySetV2 m_capabilities;
    QUuid m_openRequestId;
    QUuid m_openClientRequestId;
    QString m_openStage;
    QSet<QUuid> m_localV2Requests;
    QHash<QUuid, V2Request> m_v2Requests;
    bool m_songLyricsExtension = false;
    QHash<QString, TrackMetadata> m_trackMetadata;
};
