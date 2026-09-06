#pragma once

#include "NavidromeApiClient.h"
#include "SourceTypes.h"
#include "v2/IMusicSourceSessionV2.h"

#include <QHash>
#include <QSet>

class NavidromeSourceSession final : public IMusicSourceSessionV2 {
    Q_OBJECT

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

    // Kept concrete during the v2 migration so Task 10 can move the existing
    // behavior onto IPageProviderV2 without losing its regression coverage.
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

    QUuid startLegacy(const QString &operation, const QString &endpoint,
                      QUrlQuery query = {}, QUuid publicId = {});
    QUuid scheduleLegacySuccess(const QString &operation, const QJsonValue &result);
    QUuid scheduleLegacyFailure(const QString &operation, const SourceErrorV2 &error);
    void handleClientSuccess(const QUuid &requestId, const QString &operation,
                             const QJsonObject &response);
    void handleClientFailure(const QUuid &requestId, const SourceErrorV2 &error);
    bool isOpenRequestActive(const QUuid &requestId) const;
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
    QHash<QString, TrackMetadata> m_trackMetadata;
    QHash<QUuid, TrackRef> m_lyricsTracks;
};
