#include "RuntimeLoggingPolicy.h"

#include <QLoggingCategory>
#include <QTest>

namespace {
QStringList messages;

void capture(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtDebugMsg || type == QtInfoMsg || type == QtWarningMsg
        || type == QtCriticalMsg)
        messages.append(message);
}
}

class SensitivePlaybackLoggingTest final : public QObject {
    Q_OBJECT
private slots:
    void authenticatedDescriptorDebugOutputIsDisabled();
};

void SensitivePlaybackLoggingTest::authenticatedDescriptorDebugOutputIsDisabled()
{
    messages.clear();
    const QtMessageHandler previous = qInstallMessageHandler(capture);
    RuntimeLoggingPolicy::install();

    QLoggingCategory multimedia("qt.multimedia.ffmpeg");
    QLoggingCategory plugin("qt.multimedia.plugins.ffmpeg");
    qCDebug(multimedia) << "https://nas.invalid/stream.view?u=admin&token=secret-sentinel";
    qCDebug(plugin) << "Authorization: Bearer secret-sentinel";

    qInstallMessageHandler(previous);
    const QString joined = messages.join(QLatin1Char('\n'));
    QVERIFY2(!joined.contains(QStringLiteral("secret-sentinel")), qPrintable(joined));
    QVERIFY(!joined.contains(QStringLiteral("Authorization")));
    QVERIFY(!joined.contains(QStringLiteral("stream.view?")));
}

QTEST_GUILESS_MAIN(SensitivePlaybackLoggingTest)
#include "tst_SensitivePlaybackLogging.moc"
