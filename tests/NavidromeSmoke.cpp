#include "NavidromeSourceSession.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QTimer>

#include <utility>

namespace {

QString errorKindName(SourceErrorKind kind)
{
    switch (kind) {
    case SourceErrorKind::Network:
        return QStringLiteral("Network");
    case SourceErrorKind::Authentication:
        return QStringLiteral("Authentication");
    case SourceErrorKind::Authorization:
        return QStringLiteral("Authorization");
    case SourceErrorKind::NotFound:
        return QStringLiteral("NotFound");
    case SourceErrorKind::RateLimited:
        return QStringLiteral("RateLimited");
    case SourceErrorKind::InvalidRequest:
        return QStringLiteral("InvalidRequest");
    case SourceErrorKind::Unavailable:
        return QStringLiteral("Unavailable");
    case SourceErrorKind::Unsupported:
        return QStringLiteral("Unsupported");
    case SourceErrorKind::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

void printResult(QTextStream &stream, const QString &operation, const QString &outcome,
                 const QString &kind, qint64 elapsedMs)
{
    stream << operation << ' ' << outcome << ' ' << kind << ' ' << elapsedMs << "ms\n";
    stream.flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QByteArray serverUrl = qgetenv("QUEMUSIC_NAVIDROME_URL");
    const QByteArray username = qgetenv("QUEMUSIC_NAVIDROME_USER");
    QByteArray password = qgetenv("QUEMUSIC_NAVIDROME_PASSWORD");
    QTextStream output(stdout);
    QTextStream errors(stderr);
    QElapsedTimer elapsed;
    elapsed.start();

    if (serverUrl.isEmpty() || username.isEmpty() || password.isEmpty()) {
        printResult(errors, QStringLiteral("configuration"), QStringLiteral("failure"),
                    QStringLiteral("InvalidRequest"), elapsed.elapsed());
        password.fill('\0');
        return 64;
    }

    SourceAccount account{QStringLiteral("navidrome"),
                          QString::fromUtf8(username),
                          QStringLiteral("Navidrome smoke test"),
                          {{QStringLiteral("serverUrl"), QString::fromUtf8(serverUrl)},
                           {QStringLiteral("username"), QString::fromUtf8(username)}},
                          std::move(password)};
    QNetworkAccessManager network;
    NavidromeSourceSession session(std::move(account), &network);
    QHash<QUuid, QString> operations;
    TrackRef selectedTrack;
    QTimer timeout;
    timeout.setSingleShot(true);

    const auto finish = [&app, &timeout](int exitCode) {
        timeout.stop();
        app.exit(exitCode);
    };
    const auto start = [&operations](const QString &operation, const QUuid &requestId) {
        operations.insert(requestId, operation);
    };

    QObject::connect(&timeout, &QTimer::timeout, &app, [&] {
        printResult(errors, QStringLiteral("timeout"), QStringLiteral("failure"),
                    QStringLiteral("Network"), elapsed.elapsed());
        finish(1);
    });
    QObject::connect(&session, &IMusicSourceSession::requestFailed, &app,
                     [&](const QUuid &requestId, const SourceError &error) {
                         const QString operation = operations.take(requestId);
                         printResult(errors,
                                     operation.isEmpty() ? QStringLiteral("unknown") : operation,
                                     QStringLiteral("failure"), errorKindName(error.kind),
                                     elapsed.elapsed());
                         finish(1);
                     });
    QObject::connect(&session, &IMusicSourceSession::requestSucceeded, &app,
                     [&](const QUuid &requestId, const QString &operation, const QJsonValue &result) {
                         operations.remove(requestId);
                         printResult(output, operation, QStringLiteral("success"),
                                     QStringLiteral("None"), elapsed.elapsed());

                         if (operation == QStringLiteral("ping")) {
                             start(QStringLiteral("search"),
                                   session.search({QStringLiteral("a"), 10}));
                             return;
                         }
                         if (operation == QStringLiteral("search")) {
                             const QJsonArray items = result.toObject()
                                                          .value(QStringLiteral("items"))
                                                          .toArray();
                             for (const QJsonValue &itemValue : items) {
                                 const QJsonObject item = itemValue.toObject();
                                 if (item.value(QStringLiteral("kind")).toString() ==
                                     QStringLiteral("track")) {
                                     selectedTrack = {item.value(QStringLiteral("sourceId")).toString(),
                                                      item.value(QStringLiteral("id")).toString()};
                                     break;
                                 }
                             }
                             start(QStringLiteral("browse"), session.browse({}));
                             return;
                         }
                         if (operation == QStringLiteral("browse")) {
                             if (selectedTrack.nativeId.isEmpty()) {
                                 printResult(errors, QStringLiteral("stream"),
                                             QStringLiteral("failure"), QStringLiteral("NotFound"),
                                             elapsed.elapsed());
                                 finish(2);
                                 return;
                             }
                             start(QStringLiteral("resolveStream"),
                                   session.resolveStream(selectedTrack));
                             return;
                         }
                         if (operation == QStringLiteral("resolveStream")) {
                             start(QStringLiteral("fetchArtwork"),
                                   session.fetchArtwork(selectedTrack));
                             return;
                         }
                         if (operation == QStringLiteral("fetchArtwork")) {
                             start(QStringLiteral("fetchLyrics"), session.fetchLyrics(selectedTrack));
                             return;
                         }
                         if (operation == QStringLiteral("fetchLyrics")) {
                             finish(0);
                         }
                     });

    timeout.start(45000);
    start(QStringLiteral("ping"), session.ping());
    return app.exec();
}
