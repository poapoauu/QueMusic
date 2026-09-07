#pragma once

#include "NavidromeApiClient.h"
#include "SourceTypes.h"
#include "v2/IMusicSourceSessionV2.h"
#include "v2/ISourceProvidersV2.h"

#include <QHash>
#include <QSet>

class NavidromeSourceSession final : public IMusicSourceSessionV2,
                                     public IPageProviderV2,
                                     public IPlaybackProviderV2,
                                     public IDownloadProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2 IDownloadProviderV2)

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

    // Transitional concrete seams retain the pre-v2 regression coverage while
    // callers migrate to the provider interfaces above.
    QUuid ping();
    QUuid search(const SearchQuery &query);
    QUuid browse(const BrowseQuery &query);
    QUuid resolveStream(const TrackRef &track);
    QUuid fetchArtwork(const TrackRef &track);
    QUuid fetchLyrics(const TrackRef &track);

signals:
    void legacyRequestSucceeded(QUuid requestId, QString operation, QJsonValue result);
    void legacyRequestFailed(QUuid requestId, SourceErrorV2 error);

private:
    struct LegacyRequest {
        QUuid publicId;
        QString operation;
        QString stage;
    };
    struct TrackMetadata { QString artist; QString title; };
    struct V2Request {
        QUuid publicId;
        QString operation;
        QString endpoint;
        PageQueryV2 pageQuery;
        MediaRefV2 media;
        QUrl destination;
        int offset = 0;
    };

    QUuid startLegacy(const QString &operation, const QString &endpoint,
                      QUrlQuery query = {}, QUuid publicId = {});
    QUuid scheduleLegacySuccess(const QString &operation, const QJsonValue &result);
    QUuid scheduleLegacyFailure(const QString &operation, const SourceErrorV2 &error);
    void handleClientSuccess(const QUuid &requestId, const QString &operation,
                             const QJsonObject &response);
    void handleClientFailure(const QUuid &requestId, const SourceErrorV2 &error);
    void finishPage(const V2Request &request, const QJsonObject &response);
    QUuid scheduleV2Failure(const SourceErrorV2 &error, QUuid publicId = {});
    bool isOpenRequestActive(const QUuid &requestId) const;
    bool validMedia(const MediaRefV2 &media) const;
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
    QHash<QUuid, LegacyRequest> m_legacyRequests;
    QSet<QUuid> m_localLegacyRequests;
    QSet<QUuid> m_localV2Requests;
    QHash<QUuid, V2Request> m_v2Requests;
    bool m_songLyricsExtension = false;
    QHash<QString, TrackMetadata> m_trackMetadata;
    QHash<QUuid, TrackRef> m_lyricsTracks;
};
