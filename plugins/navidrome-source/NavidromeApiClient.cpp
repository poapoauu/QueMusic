#include "NavidromeApiClient.h"

#include "v2/SourceSecretsV2.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace {

QString randomSalt()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

SourceErrorV2 error(SourceErrorKindV2 kind, const QString &messageKey,
                    const QString &detail, std::optional<int> status = std::nullopt)
{
    return {kind, messageKey, detail, status, kind == SourceErrorKindV2::Network};
}

SourceErrorV2 subsonicFailure(int code, int status)
{
    const std::optional<int> httpStatus = status > 0 ? std::optional<int>(status) : std::nullopt;
    if (code == 40 || code == 41)
        return error(SourceErrorKindV2::Authentication,
                     QStringLiteral("source.authentication.required"),
                     QStringLiteral("Navidrome authentication failed."), httpStatus);
    if (code == 50)
        return error(SourceErrorKindV2::Authorization,
                     QStringLiteral("source.authorization.denied"),
                     QStringLiteral("Navidrome denied this operation."), httpStatus);
    if (code == 70)
        return error(SourceErrorKindV2::NotFound, QStringLiteral("source.notFound"),
                     QStringLiteral("The requested Navidrome resource was not found."), httpStatus);
    return error(SourceErrorKindV2::InvalidRequest, QStringLiteral("source.response.rejected"),
                 QStringLiteral("Navidrome rejected the request."), httpStatus);
}

} // namespace

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network, QObject *parent)
    : NavidromeApiClient(std::move(configuration), network, randomSalt, parent)
{
}

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network,
                                       SaltGenerator saltGenerator, QObject *parent)
    : NavidromeApiClient(std::move(configuration),network,std::move(saltGenerator),
                         [](QTemporaryFile &file) { return file.flush(); },parent)
{
}

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network,
                                       SaltGenerator saltGenerator,
                                       DownloadFlusher downloadFlusher,QObject *parent)
    : NavidromeApiClient(std::move(configuration),network,std::move(saltGenerator),
                         [](QTemporaryFile &file,const QByteArray &bytes) {
                             return file.write(bytes);
                         },std::move(downloadFlusher),parent)
{
}

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network,
                                       SaltGenerator saltGenerator,
                                       DownloadWriter downloadWriter,
                                       DownloadFlusher downloadFlusher,QObject *parent)
    : QObject(parent), m_configuration(std::move(configuration)), m_network(network),
      m_saltGenerator(std::move(saltGenerator)),
      m_downloadWriter(std::move(downloadWriter)),
      m_downloadFlusher(std::move(downloadFlusher))
{
}

NavidromeApiClient::~NavidromeApiClient()
{
    const QList<QUuid> ids = m_pending.keys();
    for (const QUuid &id : ids)
        cancel(id);
    m_configuration.secret.fill('\0');
    m_configuration.secret.clear();
}

QUuid NavidromeApiClient::get(const QString &operation, const QString &endpoint, QUrlQuery query)
{
    PendingRequest request; request.operation=operation;
    return start(operation,endpoint,std::move(query),std::move(request));
}

QUuid NavidromeApiClient::getBinary(const QString &operation,const QString &endpoint,
                                    QUrlQuery query,qint64 maximumBytes)
{
    PendingRequest request; request.operation=operation; request.mode=PendingRequest::Mode::Binary;
    request.maximumBytes=maximumBytes;
    return start(operation,endpoint,std::move(query),std::move(request));
}

QUuid NavidromeApiClient::downloadToFile(const QString &operation,const QString &endpoint,
                                         QUrlQuery query,const QString &destinationPath)
{
    PendingRequest request; request.operation=operation; request.mode=PendingRequest::Mode::Download;
    request.destinationPath=destinationPath;
    auto *temporary=new QTemporaryFile(destinationPath+QStringLiteral(".quemusic-XXXXXX.part"),this);
    temporary->setAutoRemove(true); request.temporaryFile=temporary;
    if (!temporary->open()) {
        const QUuid id=QUuid::createUuid(); m_pending.insert(id,request);
        scheduleFailure(id,error(SourceErrorKindV2::Unavailable,
                                  QStringLiteral("source.download.ioFailed"),
                                  QStringLiteral("The download temporary file could not be created.")));
        return id;
    }
    return start(operation,endpoint,std::move(query),std::move(request));
}

QUuid NavidromeApiClient::start(const QString &operation,const QString &endpoint,QUrlQuery query,
                                PendingRequest pendingRequest)
{
    const QUuid requestId = QUuid::createUuid();
    QUrl url;
    SourceErrorV2 validationError;
    if (!authenticatedUrl(endpoint, std::move(query), &url, &validationError)) {
        m_pending.insert(requestId, pendingRequest);
        scheduleFailure(requestId, validationError);
        return requestId;
    }
    if (m_network.isNull()) {
        m_pending.insert(requestId, pendingRequest);
        scheduleFailure(requestId,
                        error(SourceErrorKindV2::Unavailable,
                              QStringLiteral("source.network.unavailable"),
                              QStringLiteral("Navidrome networking is unavailable.")));
        return requestId;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(request);
    if (reply == nullptr) {
        m_pending.insert(requestId, pendingRequest);
        scheduleFailure(requestId,
                        error(SourceErrorKindV2::Network,
                              QStringLiteral("source.network.failed"),
                              QStringLiteral("The Navidrome request could not be started.")));
        return requestId;
    }
    pendingRequest.reply=reply;
    m_pending.insert(requestId,pendingRequest);
    connect(reply,&QIODevice::readyRead,this,[this,requestId] { consumeReply(requestId); });
    connect(reply,&QNetworkReply::metaDataChanged,this,[this,requestId] { consumeReply(requestId); });
    connect(reply, &QNetworkReply::finished, this,
            [this, requestId] { finishReply(requestId); });
    connect(reply, &QObject::destroyed, this, [this, requestId] {
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end())
            return;
        const SourceErrorV2 networkError = error(
            SourceErrorKindV2::Network, QStringLiteral("source.network.failed"),
            QStringLiteral("The Navidrome request ended unexpectedly."));
        if (pending->temporaryFile) pending->temporaryFile->deleteLater();
        m_pending.erase(pending);
        emit failed(requestId, networkError);
    });
    if (reply->isFinished())
        QTimer::singleShot(0, this, [this, requestId] { finishReply(requestId); });
    return requestId;
}

void NavidromeApiClient::cancel(const QUuid &requestId)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end())
        return;
    QPointer<QNetworkReply> reply = pending->reply;
    QPointer<QTemporaryFile> temporary=pending->temporaryFile;
    m_pending.erase(pending);
    if (reply) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        delete reply;
    }
    if (temporary) delete temporary;
}

bool NavidromeApiClient::authenticatedUrl(const QString &endpoint, QUrlQuery query,
                                          QUrl *url, SourceErrorV2 *validationError)
{
    const QVariant serverValue = m_configuration.parameters.value(QStringLiteral("serverUrl"));
    const QVariant usernameValue = m_configuration.parameters.value(QStringLiteral("username"));
    const QUrl serverUrl(serverValue.toString());
    const QString username = usernameValue.toString();
    if (serverValue.metaType().id() != QMetaType::QString
        || usernameValue.metaType().id() != QMetaType::QString
        || !serverUrl.isValid() || serverUrl.host().isEmpty()
        || !serverUrl.userInfo().isEmpty()
        || (serverUrl.scheme() != QStringLiteral("http")
            && serverUrl.scheme() != QStringLiteral("https"))
        || username.isEmpty() || endpoint.isEmpty() || endpoint.contains(QLatin1Char('/'))) {
        *validationError = error(SourceErrorKindV2::InvalidRequest,
                                 QStringLiteral("source.configuration.invalid"),
                                 QStringLiteral("Navidrome connection settings are invalid."));
        return false;
    }

    QByteArray password;
    switch (sourceSecretInputKindV2(m_configuration.secret)) {
    case SourceSecretInputKindV2::LegacyRaw:
        password = m_configuration.secret;
        break;
    case SourceSecretInputKindV2::NamedEnvelope: {
        const auto secrets = decodeSourceSecretsV2(m_configuration.secret);
        if (secrets && secrets->contains(QStringLiteral("password")))
            password = secrets->value(QStringLiteral("password"));
        else
            password.clear();
        if (!secrets || !secrets->contains(QStringLiteral("password")) || password.isEmpty()) {
            password.fill('\0');
            *validationError = error(SourceErrorKindV2::Authentication,
                                     QStringLiteral("source.authentication.invalidCredentials"),
                                     QStringLiteral("Navidrome credentials are unavailable."));
            return false;
        }
        break;
    }
    case SourceSecretInputKindV2::MalformedEnvelope:
        *validationError = error(SourceErrorKindV2::Authentication,
                                 QStringLiteral("source.authentication.invalidCredentials"),
                                 QStringLiteral("Navidrome credentials are unavailable."));
        return false;
    }
    if (m_configuration.secret.isEmpty()) {
        *validationError = error(SourceErrorKindV2::Authentication,
                                 QStringLiteral("source.authentication.invalidCredentials"),
                                 QStringLiteral("Navidrome credentials are unavailable."));
        return false;
    }

    *url = serverUrl;
    url->setQuery(QString());
    url->setFragment({});
    QString path = url->path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (!path.endsWith(QStringLiteral("/rest")))
        path += QStringLiteral("/rest");
    url->setPath(path + QLatin1Char('/') + endpoint + QStringLiteral(".view"));

    const QString salt = m_saltGenerator ? m_saltGenerator() : QString();
    if (salt.isEmpty()) {
        password.fill('\0');
        *validationError = error(SourceErrorKindV2::Unavailable,
                                 QStringLiteral("source.authentication.unavailable"),
                                 QStringLiteral("Navidrome authentication is unavailable."));
        return false;
    }
    const QByteArray token = QCryptographicHash::hash(password + salt.toUtf8(),
                                                       QCryptographicHash::Md5).toHex();
    password.fill('\0');
    for (const QString &reserved : {QStringLiteral("u"), QStringLiteral("p"),
                                    QStringLiteral("t"), QStringLiteral("s"),
                                    QStringLiteral("v"), QStringLiteral("c"),
                                    QStringLiteral("f")})
        query.removeAllQueryItems(reserved);
    query.addQueryItem(QStringLiteral("u"), username);
    query.addQueryItem(QStringLiteral("t"), QString::fromLatin1(token));
    query.addQueryItem(QStringLiteral("s"), salt);
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("1.16.1"));
    query.addQueryItem(QStringLiteral("c"), QStringLiteral("QueMusic"));
    query.addQueryItem(QStringLiteral("f"), QStringLiteral("json"));
    url->setQuery(query);
    return true;
}

void NavidromeApiClient::finishReply(const QUuid &requestId)
{
    consumeReply(requestId);
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end() || pending->reply.isNull())
        return;
    const PendingRequest request = pending.value();
    m_pending.erase(pending);
    QNetworkReply *reply = request.reply;
    disconnect(reply, nullptr, this, nullptr);
    const int rawStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const std::optional<int> status = rawStatus > 0 ? std::optional<int>(rawStatus) : std::nullopt;
    QByteArray payload = request.mode==PendingRequest::Mode::Json ? reply->readAll()
                                                                  : request.bytes;
    const QNetworkReply::NetworkError networkCode = reply->error();
    reply->deleteLater();
    const auto discardTemporary=[&request] {
        if (request.temporaryFile) delete request.temporaryFile.data();
    };

    if (rawStatus == 404 || rawStatus == 405 || rawStatus == 501) {
        discardTemporary();
        emit failed(requestId,
                    error(SourceErrorKindV2::Unsupported,
                          QStringLiteral("source.endpoint.unsupported"),
                          QStringLiteral("This Navidrome endpoint is unavailable."), status));
        return;
    }
    if (rawStatus == 401) {
        discardTemporary();
        emit failed(requestId,
                    error(SourceErrorKindV2::Authentication,
                          QStringLiteral("source.authentication.required"),
                          QStringLiteral("Navidrome authentication failed."), status));
        return;
    }
    if (rawStatus == 403) {
        discardTemporary();
        emit failed(requestId,
                    error(SourceErrorKindV2::Authorization,
                          QStringLiteral("source.authorization.denied"),
                          QStringLiteral("Navidrome denied this operation."), status));
        return;
    }
    if (networkCode != QNetworkReply::NoError || rawStatus < 200 || rawStatus >= 300) {
        discardTemporary();
        emit failed(requestId,
                    error(SourceErrorKindV2::Network, QStringLiteral("source.network.failed"),
                          QStringLiteral("The Navidrome request failed."), status));
        return;
    }

    const QString responseMime=reply->header(QNetworkRequest::ContentTypeHeader).toString()
                                   .section(QLatin1Char(';'),0,0).trimmed();
    if (request.mode!=PendingRequest::Mode::Json
        && responseMime.contains(QStringLiteral("json"),Qt::CaseInsensitive)) {
        const auto responseValue=QJsonDocument::fromJson(payload).object()
                                     .value(QStringLiteral("subsonic-response"));
        const auto response=responseValue.toObject(); discardTemporary();
        if (responseValue.isObject()
            && response.value(QStringLiteral("status")).toString()==QStringLiteral("failed"))
            emit failed(requestId,subsonicFailure(response.value(QStringLiteral("error")).toObject()
                                                      .value(QStringLiteral("code")).toInt(-1),rawStatus));
        else
            emit failed(requestId,error(SourceErrorKindV2::InvalidRequest,
                                        QStringLiteral("source.response.invalid"),
                                        QStringLiteral("Navidrome returned an invalid media response."),status));
        return;
    }

    if (request.mode==PendingRequest::Mode::Binary) {
        emit binarySucceeded(requestId,request.operation,payload,responseMime);
        return;
    }
    if (request.mode==PendingRequest::Mode::Download) {
        if (!request.temporaryFile) {
            emit failed(requestId,error(SourceErrorKindV2::Unavailable,
                                        QStringLiteral("source.download.ioFailed"),
                                        QStringLiteral("The download temporary file was lost.")));
            return;
        }
        if (!m_downloadFlusher || !m_downloadFlusher(*request.temporaryFile)
            || request.temporaryFile->error()!=QFileDevice::NoError) {
            delete request.temporaryFile.data();
            emit failed(requestId,error(SourceErrorKindV2::Unavailable,
                                        QStringLiteral("source.download.ioFailed"),
                                        QStringLiteral("The downloaded bytes could not be flushed.")));
            return;
        }
        request.temporaryFile->close();
        const QString temporaryPath=request.temporaryFile->fileName();
        if (QFileInfo::exists(request.destinationPath)
            || !QFile::rename(temporaryPath,request.destinationPath)) {
            delete request.temporaryFile.data();
            emit failed(requestId,error(SourceErrorKindV2::Unavailable,
                                        QStringLiteral("source.download.commitFailed"),
                                        QStringLiteral("The downloaded file could not be committed.")));
            return;
        }
        request.temporaryFile->setAutoRemove(false); delete request.temporaryFile.data();
        emit downloadSucceeded(requestId,request.operation); return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonValue responseValue = document.object().value(QStringLiteral("subsonic-response"));
    const QJsonObject response = responseValue.toObject();
    if (parseError.error == QJsonParseError::NoError && responseValue.isObject()
        && response.value(QStringLiteral("status")).toString() == QStringLiteral("failed")) {
        emit failed(requestId,
                    subsonicFailure(response.value(QStringLiteral("error")).toObject()
                                        .value(QStringLiteral("code")).toInt(-1),
                                    rawStatus));
        return;
    }
    if (parseError.error != QJsonParseError::NoError || !responseValue.isObject()
        || response.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
        emit failed(requestId,
                    error(SourceErrorKindV2::InvalidRequest,
                          QStringLiteral("source.response.invalid"),
                          QStringLiteral("Navidrome returned an invalid response."), status));
        return;
    }
    emit succeeded(requestId, request.operation, response);
}

void NavidromeApiClient::consumeReply(const QUuid &requestId)
{
    auto pending=m_pending.find(requestId);
    if (pending==m_pending.end() || !pending->reply
        || pending->mode==PendingRequest::Mode::Json) return;
    const int status=pending->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString mime=pending->reply->header(QNetworkRequest::ContentTypeHeader).toString()
                           .section(QLatin1Char(';'),0,0).trimmed();
    const bool json=mime.contains(QStringLiteral("json"),Qt::CaseInsensitive);
    const bool successfulHttp=status>=200 && status<300;
    if (!successfulHttp || json) {
        constexpr qsizetype maximumErrorBytes=64*1024;
        const qsizetype remaining=maximumErrorBytes-pending->bytes.size();
        if (remaining>0) pending->bytes.append(pending->reply->read(remaining));
        while (pending->reply->bytesAvailable()>0)
            pending->reply->read(qMin<qint64>(64*1024,pending->reply->bytesAvailable()));
        return;
    }
    const QVariant declared=pending->reply->header(QNetworkRequest::ContentLengthHeader);
    if (pending->mode==PendingRequest::Mode::Binary && declared.isValid()
        && declared.toLongLong()>pending->maximumBytes) {
        failTransfer(requestId,error(SourceErrorKindV2::Unavailable,
                                     QStringLiteral("source.artwork.tooLarge"),
                                     QStringLiteral("The Navidrome artwork response exceeded 32 MiB.")));
        return;
    }
    const QByteArray chunk=pending->reply->readAll();
    if (pending->mode==PendingRequest::Mode::Binary) {
        if (pending->bytes.size()>pending->maximumBytes-chunk.size()) {
            failTransfer(requestId,error(SourceErrorKindV2::Unavailable,
                                         QStringLiteral("source.artwork.tooLarge"),
                                         QStringLiteral("The Navidrome artwork response exceeded 32 MiB.")));
            return;
        }
        pending->bytes.append(chunk);
    } else if (!chunk.isEmpty()
               && (!pending->temporaryFile
                   || !m_downloadWriter
                   || m_downloadWriter(*pending->temporaryFile,chunk)!=chunk.size()
                   || pending->temporaryFile->error()!=QFileDevice::NoError)) {
        failTransfer(requestId,error(SourceErrorKindV2::Unavailable,
                                     QStringLiteral("source.download.ioFailed"),
                                     QStringLiteral("The downloaded bytes could not be written.")));
    }
}

void NavidromeApiClient::failTransfer(const QUuid &requestId,const SourceErrorV2 &failure)
{
    auto pending=m_pending.find(requestId); if (pending==m_pending.end()) return;
    QPointer<QNetworkReply> reply=pending->reply;
    QPointer<QTemporaryFile> temporary=pending->temporaryFile;
    m_pending.erase(pending);
    if (reply) { disconnect(reply,nullptr,this,nullptr); reply->abort(); reply->deleteLater(); }
    if (temporary) delete temporary.data();
    emit failed(requestId,failure);
}

void NavidromeApiClient::scheduleFailure(const QUuid &requestId, const SourceErrorV2 &failure)
{
    QTimer::singleShot(0, this, [this, requestId, failure] {
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end())
            return;
        if (pending->temporaryFile) delete pending->temporaryFile.data();
        m_pending.erase(pending);
        emit failed(requestId, failure);
    });
}
