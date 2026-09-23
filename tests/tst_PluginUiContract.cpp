#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QTest>

class PluginUiContractTest final : public QObject {
    Q_OBJECT
private slots:
    void freezesVersionedIdentifiers();
    void descriptorDefaultsFailClosed();
    void contextCarriesStableIdentity();
};

void PluginUiContractTest::freezesVersionedIdentifiers()
{
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID),
             QStringLiteral("org.quemusic.SourceManagementUiProvider/1.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLUGIN_UI_API_V1), QStringLiteral("1.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID),
             QStringLiteral("org.quemusic.MusicSourcePlugin/2.0"));
    QCOMPARE(QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI, 2);
}

void PluginUiContractTest::descriptorDefaultsFailClosed()
{
    const ManagementUiDescriptor descriptor;
    QVERIFY(descriptor.uiApiVersion.isEmpty());
    QVERIFY(descriptor.componentUrl.isEmpty());
    QVERIFY(!descriptor.supportsCreate);
    QVERIFY(!descriptor.supportsEdit);
}

void PluginUiContractTest::contextCarriesStableIdentity()
{
    const PluginUiContextData context{QStringLiteral("org.example.source"),
                                      QStringLiteral("example"),
                                      QStringLiteral("example/home"),
                                      QStringLiteral("home"),
                                      PluginUiMode::Edit};
    QCOMPARE(context.pluginPackageId, QStringLiteral("org.example.source"));
    QCOMPARE(context.sourceId, QStringLiteral("example"));
    QCOMPARE(context.sourceInstanceId, QStringLiteral("example/home"));
    QCOMPARE(context.accountId, QStringLiteral("home"));
    QCOMPARE(context.mode, PluginUiMode::Edit);
}

QTEST_MAIN(PluginUiContractTest)
#include "tst_PluginUiContract.moc"
