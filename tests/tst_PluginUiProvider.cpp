#include "PluginManager.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QTest>

namespace {
QString packageId(const QString &variant)
{
    return QStringLiteral("org.quemusic.source.ui-") + variant;
}
}

class PluginUiProviderTest final : public QObject {
    Q_OBJECT
private slots:
    void loadsMatchingProvider();
    void rejectsMismatch_data();
    void rejectsMismatch();
    void nullBackendIsAllowed();
};

void PluginUiProviderTest::loadsMatchingProvider()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT));
    QCOMPARE(manager.discover(), 7);
    const QString id = packageId(QStringLiteral("valid"));
    QVERIFY(manager.load(id));
    QObject *plugin = manager.pluginInstance(id);
    QVERIFY(qobject_cast<IMusicSourcePluginV2 *>(plugin));
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(plugin);
    QVERIFY(provider);
    const auto descriptor = provider->managementUi();
    QCOMPARE(descriptor.uiApiVersion, QStringLiteral("1.0"));
    QCOMPARE(descriptor.componentUrl, QUrl(QStringLiteral("qml/ManagementPage.qml")));
    QVERIFY(descriptor.supportsCreate);
    QVERIFY(descriptor.supportsEdit);
}

void PluginUiProviderTest::rejectsMismatch_data()
{
    QTest::addColumn<QString>("variant");
    QTest::addColumn<QString>("errorFragment");
    QTest::newRow("declared-without-provider") << QStringLiteral("provider_missing")
        << QStringLiteral("does not implement");
    QTest::newRow("provider-without-declaration") << QStringLiteral("undeclared")
        << QStringLiteral("without manifest declaration");
    QTest::newRow("wrong-version") << QStringLiteral("wrong_version")
        << QStringLiteral("does not match manifest");
    QTest::newRow("wrong-path") << QStringLiteral("wrong_path")
        << QStringLiteral("does not match manifest");
    QTest::newRow("no-mode") << QStringLiteral("no_mode")
        << QStringLiteral("does not match manifest");
}

void PluginUiProviderTest::rejectsMismatch()
{
    QFETCH(QString, variant);
    QFETCH(QString, errorFragment);
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT));
    QCOMPARE(manager.discover(), 7);
    const QString id = packageId(variant);
    QVERIFY(!manager.load(id));
    const auto spec = manager.plugin(id);
    QCOMPARE(spec.state, PluginState::Failed);
    QVERIFY2(spec.error.contains(errorFragment), qPrintable(spec.error));
    QCOMPARE(spec.activeLeases, 0);
    QVERIFY(manager.pluginInstance(id) == nullptr);
}

void PluginUiProviderTest::nullBackendIsAllowed()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT));
    QCOMPARE(manager.discover(), 7);
    const QString id = packageId(QStringLiteral("null_backend"));
    QVERIFY(manager.load(id));
    auto lease = manager.acquire(id);
    QVERIFY(lease.isValid());
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager.pluginInstance(id));
    QVERIFY(provider);
    QObject owner;
    PluginUiContextData context;
    context.pluginPackageId = id;
    QCOMPARE(provider->createManagementBackend(context, &owner), nullptr);
}

QTEST_MAIN(PluginUiProviderTest)
#include "tst_PluginUiProvider.moc"
