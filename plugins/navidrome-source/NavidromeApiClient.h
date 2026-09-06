#pragma once

#include "v2/SourceV2Types.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrlQuery>

#include <functional>

class NavidromeSourceSession;

class NavidromeApiClient final : public QObject {
    Q_OBJECT

public:
    using SaltGenerator = std::function<QString()>;

    explicit NavidromeApiClient(SourceConfigurationV2 configuration,
                                QNetworkAccessManager *network, QObject *parent = nullptr);
    NavidromeApiClient(SourceConfigurationV2 configuration, QNetworkAccessManager *network,
                       SaltGenerator saltGenerator, QObject *parent = nullptr);
    ~NavidromeApiClient() override;

    QUuid get(const QString &operation, const QString &endpoint, QUrlQuery query = {});
    void cancel(const QUuid &requestId);

signals:
    void succeeded(QUuid requestId, QString operation, QJsonObject subsonicResponse);
    void failed(QUuid requestId, SourceErrorV2 error);

private:
    friend class NavidromeSourceSession;

    struct PendingRequest {
        QString operation;
        QPointer<QNetworkReply> reply;
    };

    bool authenticatedUrl(const QString &endpoint, QUrlQuery query, QUrl *url,
                          SourceErrorV2 *error);
    void finishReply(const QUuid &requestId);
    void scheduleFailure(const QUuid &requestId, const SourceErrorV2 &error);

    SourceConfigurationV2 m_configuration;
    QPointer<QNetworkAccessManager> m_network;
    SaltGenerator m_saltGenerator;
    QHash<QUuid, PendingRequest> m_pending;
};
