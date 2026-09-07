#pragma once

#include "v2/SourceV2Types.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryFile>
#include <QUrlQuery>

#include <functional>

class NavidromeSourceSession;

class NavidromeApiClient final : public QObject {
    Q_OBJECT

public:
    using SaltGenerator = std::function<QString()>;
    using DownloadWriter = std::function<qint64(QTemporaryFile &,const QByteArray &)>;
    using DownloadFlusher = std::function<bool(QTemporaryFile &)>;

    explicit NavidromeApiClient(SourceConfigurationV2 configuration,
                                QNetworkAccessManager *network, QObject *parent = nullptr);
    NavidromeApiClient(SourceConfigurationV2 configuration, QNetworkAccessManager *network,
                       SaltGenerator saltGenerator, QObject *parent = nullptr);
    NavidromeApiClient(SourceConfigurationV2 configuration, QNetworkAccessManager *network,
                       SaltGenerator saltGenerator, DownloadFlusher downloadFlusher,
                       QObject *parent = nullptr);
    NavidromeApiClient(SourceConfigurationV2 configuration, QNetworkAccessManager *network,
                       SaltGenerator saltGenerator, DownloadWriter downloadWriter,
                       DownloadFlusher downloadFlusher, QObject *parent = nullptr);
    ~NavidromeApiClient() override;

    QUuid get(const QString &operation, const QString &endpoint, QUrlQuery query = {});
    QUuid getBinary(const QString &operation, const QString &endpoint, QUrlQuery query,
                    qint64 maximumBytes);
    QUuid downloadToFile(const QString &operation, const QString &endpoint, QUrlQuery query,
                         const QString &destinationPath);
    void cancel(const QUuid &requestId);

signals:
    void succeeded(QUuid requestId, QString operation, QJsonObject subsonicResponse);
    void binarySucceeded(QUuid requestId, QString operation, QByteArray bytes, QString mimeType);
    void downloadSucceeded(QUuid requestId, QString operation);
    void failed(QUuid requestId, SourceErrorV2 error);

private:
    friend class NavidromeSourceSession;

    struct PendingRequest {
        enum class Mode { Json, Binary, Download };
        QString operation;
        QPointer<QNetworkReply> reply;
        Mode mode = Mode::Json;
        QByteArray bytes;
        qint64 maximumBytes = 0;
        QPointer<QTemporaryFile> temporaryFile;
        QString destinationPath;
    };

    bool authenticatedUrl(const QString &endpoint, QUrlQuery query, QUrl *url,
                          SourceErrorV2 *error);
    void finishReply(const QUuid &requestId);
    void consumeReply(const QUuid &requestId);
    void failTransfer(const QUuid &requestId, const SourceErrorV2 &error);
    QUuid start(const QString &operation, const QString &endpoint, QUrlQuery query,
                PendingRequest request);
    void scheduleFailure(const QUuid &requestId, const SourceErrorV2 &error);

    SourceConfigurationV2 m_configuration;
    QPointer<QNetworkAccessManager> m_network;
    SaltGenerator m_saltGenerator;
    DownloadWriter m_downloadWriter;
    DownloadFlusher m_downloadFlusher;
    QHash<QUuid, PendingRequest> m_pending;
};
