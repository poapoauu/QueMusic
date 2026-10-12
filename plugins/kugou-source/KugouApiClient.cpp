#include "KugouApiClient.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
SourceErrorV2 error(SourceErrorKindV2 kind, const char *key, bool retryable,
                    std::optional<int> status = {})
{
    // Never include response bodies, URLs, headers or Qt's errorString.
    return {kind, QString::fromLatin1(key), {}, status, retryable};
}
QUrl endpointUrl(KugouApiClient::Endpoint endpoint)
{
    switch (endpoint) {
    case KugouApiClient::Endpoint::SearchTracks:
        return QUrl(QStringLiteral("https://mobilecdn.kugou.com/api/v3/search/song"));
    case KugouApiClient::Endpoint::ResolveTrack:
        return QUrl(QStringLiteral("https://m.kugou.com/app/i/getSongInfo.php"));
    }
    return {};
}
}

KugouApiClient::KugouApiClient(QObject *parent)
    : KugouApiClient(new QNetworkAccessManager, parent)
{
    m_network->setParent(this);
}

KugouApiClient::KugouApiClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{
    if (network) {
        connect(network, &QObject::destroyed, this, [this] {
            m_network = nullptr;
            const auto ids = m_pending.keys();
            for (const auto &id : ids)
                scheduleFailure(id, error(SourceErrorKindV2::Unavailable,
                                          "kugou.networkUnavailable", true));
        });
    }
}

KugouApiClient::~KugouApiClient() { cancelAll(); }

QUuid KugouApiClient::get(Endpoint endpoint, const QUrlQuery &query)
{
    const QUuid id = QUuid::createUuid();
    m_pending.insert(id, {});
    QUrl url = endpointUrl(endpoint);
    if (url.isEmpty()) {
        scheduleFailure(id, error(SourceErrorKindV2::InvalidRequest,
                                  "kugou.invalidEndpoint", false));
        return id;
    }
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Referer", "https://www.kugou.com/");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    dispatch(id, request);
    return id;
}

QUuid KugouApiClient::getQr(KugouQrRequests::Operation operation,
                           const KugouQrRequests::DeviceIdentity &identity,
                           qint64 epochSeconds, const QString &code)
{
    const QUuid id = QUuid::createUuid();
    m_pending.insert(id, {});
    const auto request = KugouQrRequests::build(operation, identity, epochSeconds, code);
    if (!request) {
        scheduleFailure(id, error(SourceErrorKindV2::InvalidRequest, "kugou.qrInvalidRequest", false));
        return id;
    }
    dispatch(id, *request);
    return id;
}

QUuid KugouApiClient::getRank(KugouRankProtocol::Operation operation, const QString &rankId,
                            int page, int limit)
{
    const QUuid id = QUuid::createUuid();
    m_pending.insert(id, {});
    const auto request = KugouRankProtocol::build(operation, rankId, page, limit);
    if (!request) {
        scheduleFailure(id, error(SourceErrorKindV2::InvalidRequest, "kugou.rankInvalidRequest", false));
        return id;
    }
    dispatch(id, *request);
    return id;
}

void KugouApiClient::dispatch(const QUuid &id, QNetworkRequest request)
{
    if (!m_network) {
        scheduleFailure(id, error(SourceErrorKindV2::Unavailable, "kugou.networkUnavailable", true));
        return;
    }
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    auto *reply = m_network->get(request);
    m_pending[id].reply = reply;
    reply->setReadBufferSize(MaximumBodyBytes + 1);
    connect(reply, &QIODevice::readyRead, this, [this, id] { consume(id); });
    connect(reply, &QNetworkReply::finished, this, [this, id] { finish(id); });
    connect(reply, &QObject::destroyed, this, [this, id] {
        if (m_pending.contains(id))
            scheduleFailure(id, error(SourceErrorKindV2::Unavailable,
                                      "kugou.replyUnavailable", true));
    });
    // Also support injected managers returning an already completed reply.
    if (reply->isFinished())
        QTimer::singleShot(0, this, [this, id] { finish(id); });
}

void KugouApiClient::consume(const QUuid &id)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end() || !it->reply) return;
    const auto length = it->reply->header(QNetworkRequest::ContentLengthHeader);
    if (length.isValid() && length.toLongLong() > MaximumBodyBytes) {
        fail(id, error(SourceErrorKindV2::Unavailable, "kugou.responseTooLarge", false));
        return;
    }
    it->bytes += it->reply->read(MaximumBodyBytes - it->bytes.size() + 1);
    if (it->bytes.size() > MaximumBodyBytes)
        fail(id, error(SourceErrorKindV2::Unavailable, "kugou.responseTooLarge", false));
}

void KugouApiClient::finish(const QUuid &id)
{
    const QPointer<KugouApiClient> guard(this);
    consume(id);
    if (!guard) return; // A failure observer may destroy its instance.
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend() || !it->reply) return;
    const auto status = it->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300) {
        SourceErrorKindV2 kind = SourceErrorKindV2::Unavailable;
        if (status == 401) kind = SourceErrorKindV2::Authentication;
        else if (status == 403) kind = SourceErrorKindV2::Authorization;
        else if (status == 404) kind = SourceErrorKindV2::NotFound;
        else if (status == 429) kind = SourceErrorKindV2::RateLimited;
        else if (!status) kind = SourceErrorKindV2::Network;
        fail(id, error(kind, "kugou.requestFailed", !status || status == 429 || status >= 500,
                       status ? std::optional<int>(status) : std::nullopt));
        return;
    }
    if (it->reply->error() != QNetworkReply::NoError) {
        fail(id, error(SourceErrorKindV2::Network, "kugou.networkFailed", true, status));
        return;
    }
    auto body = it->bytes.trimmed();
    const QByteArray start("<!--KG_TAG_RES_START-->"), end("<!--KG_TAG_RES_END-->");
    if (body.startsWith(start) && body.endsWith(end))
        body = body.mid(start.size(), body.size() - start.size() - end.size()).trimmed();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(id, error(SourceErrorKindV2::Unavailable, "kugou.invalidResponse", false, status));
        return;
    }
    const auto response = document.object();
    release(id);
    emit succeeded(id, response);
}

void KugouApiClient::release(const QUuid &id)
{
    const auto pending = m_pending.take(id);
    if (pending.reply) {
        disconnect(pending.reply, nullptr, this, nullptr);
        if (!pending.reply->isFinished()) pending.reply->abort();
        pending.reply->deleteLater();
    }
}
void KugouApiClient::cancel(const QUuid &id) { release(id); }
void KugouApiClient::cancelAll()
{
    const auto ids = m_pending.keys();
    for (const auto &id : ids) release(id);
}
void KugouApiClient::fail(const QUuid &id, const SourceErrorV2 &failure)
{
    if (!m_pending.contains(id)) return;
    release(id);
    emit failed(id, failure);
}
void KugouApiClient::scheduleFailure(const QUuid &id, const SourceErrorV2 &failure)
{
    QTimer::singleShot(0, this, [this, id, failure] { fail(id, failure); });
}
