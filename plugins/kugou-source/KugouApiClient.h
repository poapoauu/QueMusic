#pragma once

#include "v2/SourceV2Types.h"
#include "KugouQrRequests.h"
#include "KugouRankProtocol.h"
#include "KugouArtistProtocol.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUuid>
#include <QUrlQuery>

class QNetworkReply;

// Instance-owned JSON transport. QR requests use their explicit private
// protocol builder; no caller-supplied URL or Host cookies are accepted.
class KugouApiClient final : public QObject {
    Q_OBJECT
public:
    enum class Endpoint { SearchTracks, ResolveTrack };
    static constexpr qint64 MaximumBodyBytes = 1024 * 1024;

    explicit KugouApiClient(QObject *parent = nullptr);
    KugouApiClient(QNetworkAccessManager *network, QObject *parent);
    ~KugouApiClient() override;

    QUuid get(Endpoint endpoint, const QUrlQuery &query = {});
    QUuid getRank(KugouRankProtocol::Operation operation, const QString &rankId = {},
                  int page = 1, int limit = 20);
    QUuid getArtist(KugouArtistProtocol::Operation operation, const QString &artistId = {},
                    int page = 1, int limit = 20);
    QUuid getQr(KugouQrRequests::Operation operation,
                const KugouQrRequests::DeviceIdentity &identity,
                qint64 epochSeconds, const QString &code = {});
    void cancel(const QUuid &requestId);
    void cancelAll();

signals:
    void succeeded(QUuid requestId, QJsonObject response);
    void failed(QUuid requestId, SourceErrorV2 error);

private:
    struct Pending {
        QPointer<QNetworkReply> reply;
        QByteArray bytes;
    };
    void consume(const QUuid &id);
    void dispatch(const QUuid &id, QNetworkRequest request);
    void finish(const QUuid &id);
    void fail(const QUuid &id, const SourceErrorV2 &error);
    void scheduleFailure(const QUuid &id, const SourceErrorV2 &error);
    void release(const QUuid &id);

    QPointer<QNetworkAccessManager> m_network;
    QHash<QUuid, Pending> m_pending;
};
