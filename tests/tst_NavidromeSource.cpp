#include "IMusicSourceArtworkSession.h"
#include "NavidromeSourcePlugin.h"

#include <QNetworkAccessManager>
#include <QTest>

class NavidromeSourceTest : public QObject {
    Q_OBJECT

private slots:
    void reportsNavidromeDescriptor();
    void createsConfiguredSession();
};

void NavidromeSourceTest::reportsNavidromeDescriptor()
{
    NavidromeSourcePlugin plugin;
    const SourceDescriptor descriptor = plugin.descriptor();

    QCOMPARE(descriptor.id, QStringLiteral("navidrome"));
    QCOMPARE(descriptor.protocol, QStringLiteral("subsonic"));
    QCOMPARE(descriptor.sdkVersion, QStringLiteral("1.0"));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Search));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Browse));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::StreamAudio));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Artwork));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Lyrics));
}

void NavidromeSourceTest::createsConfiguredSession()
{
    NavidromeSourcePlugin plugin;
    QNetworkAccessManager network;
    SourcePluginContext context{&network, QStringLiteral("/tmp/quemusic-navidrome-test")};
    const SourceAccount account{
        QStringLiteral("navidrome"),
        QStringLiteral("admin"),
        QStringLiteral("Navidrome Admin"),
        {{QStringLiteral("serverUrl"), QStringLiteral("http://example.invalid:8533")},
         {QStringLiteral("username"), QStringLiteral("admin")}},
        QByteArrayLiteral("test-password")};

    QVERIFY(plugin.initialize(context));
    IMusicSourceSession *session = plugin.createSession(account, &plugin);

    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &plugin);
    QVERIFY(qobject_cast<IMusicSourceArtworkSession *>(session) != nullptr);
}

QTEST_MAIN(NavidromeSourceTest)
#include "tst_NavidromeSource.moc"
