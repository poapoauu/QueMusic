#include "KugouCredentialRecord.h"
#include "PluginManagementUiSession.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SettingsSchemaPresentation.h"
#include "PluginUiSettingsBridge.h"
#include "PluginSettingsController.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/ISourceProvidersV2.h"
#include <QFile>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

namespace {
const QString packageId = QStringLiteral("org.quemusic.source.kugou");
class Secrets final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool write(const QString &id, const QByteArray &bytes, QString *) override { values[id] = bytes; return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override
    { return values.contains(id) ? std::optional<QByteArray>(values.value(id)) : std::nullopt; }
    bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};
struct Fixture {
    QTemporaryDir directory;
    QSettings settings{directory.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager manager;
    SettingsSchemaV2 schema;
    bool load() {
        manager.addSearchPath(QUEMUSIC_KUGOU_PACKAGE_ROOT); manager.discover();
        if (!manager.load(packageId)) return false;
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(manager.pluginInstance(packageId));
        if (!provider) return false;
        const auto detached = detachSettingsSchemaV2(provider->settingsSchema());
        if (!detached) return false; schema = *detached; return true;
    }
};
PluginUiContextData identity(PluginUiContext *context)
{ return {context->pluginPackageId(), context->sourceId(), context->sourceInstanceId(), context->accountId(), context->mode()}; }
}

class KugouManagementUiTest final : public QObject {
    Q_OBJECT
private slots:
    void productionProviderCreatesKitPageAndReleasesLease()
    {
        Fixture f; QVERIFY(f.load());
        PluginSettingsController settings(&f.manager, nullptr, &f.accounts);
        QVERIFY(settings.selectPlugin(packageId)); QVERIFY(settings.managementUiAvailable());
        auto *provider = qobject_cast<ISourceManagementUiProvider *>(f.manager.pluginInstance(packageId)); QVERIFY(provider);
        { const auto descriptor = provider->managementUi(); QCOMPARE(descriptor.uiApiVersion, "1.0");
          QCOMPARE(descriptor.componentUrl, QUrl("qml/ManagementPage.qml")); QVERIFY(descriptor.supportsCreate && descriptor.supportsEdit); }
        QObject wrongOwner;
        QVERIFY(!provider->createManagementBackend({packageId, "kugou", "kugou/one", "one", PluginUiMode::Edit}, &wrongOwner));
        PluginManagementUiSession session(&f.manager, &f.accounts, nullptr,
            {packageId, "kugou", {}, {}, PluginUiMode::Create}, f.schema);
        QCOMPARE(session.state(), "ready"); QVERIFY(session.context()->isValid());
        QCOMPARE(session.context()->backend()->parent(), session.context());
        QCOMPARE(session.context()->backend()->property("state").toString(), "idle");
        auto foreign = identity(session.context()); foreign.accountId = "foreign";
        foreign.sourceInstanceId = "kugou/foreign";
        QVERIFY(!provider->createManagementBackend(foreign, session.context()));
        QVERIFY(session.context()->backend()->findChild<QNetworkAccessManager *>());
        QCOMPARE(f.manager.plugin(packageId).activeLeases, 1);
        auto *bridge = qobject_cast<PluginUiSettingsBridge *>(session.context()->settings()); QVERIFY(bridge);
        QVERIFY(bridge->sections().isEmpty()); QVERIFY(!bridge->secretConfigured("credentialBundle"));
        QQmlEngine engine; engine.addImportPath(QUEMUSIC_QML_IMPORT_DIR);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, session.componentUrl());
        QTRY_VERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> page(component.createWithInitialProperties({{"pluginUiContext", QVariant::fromValue(session.context())}}));
        QVERIFY2(page, qPrintable(component.errorString())); session.trackPage(page.data());
        QCOMPARE(page->objectName(), "kugouManagementPage");
        QVERIFY(page->findChild<QObject *>("kugouQrLogin"));
        QCOMPARE(f.manager.unload(packageId), PluginOperationResult::Busy);
        QVERIFY(f.accounts.accounts().isEmpty()); // No implicit QR, storage or Host import.
        QPointer<QObject> backend = session.context()->backend();
        session.invalidateContext(); QCOMPARE(backend->property("state").toString(), "invalidated");
        QCOMPARE(page->property("authState").toString(), "invalidated");
        QVERIFY(!page->property("backend").value<QObject *>());
        QVERIFY(!page->findChild<QObject *>("pluginQrRefresh")->property("enabled").toBool());
        page.reset(); QTRY_VERIFY(!session.pageAlive()); session.release(); QVERIFY(!backend);
        QCOMPARE(warnings.size(), 0); QCOMPARE(f.manager.plugin(packageId).activeLeases, 0);
        QCOMPARE(f.manager.unload(packageId), PluginOperationResult::Success);
        QVERIFY(!settings.managementUiAvailable());
    }
    void productionEditReadsBindingAndIsolatesCreateContext()
    {
        Fixture f; QVERIFY(f.load());
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto record = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_CANDIDATE"}}); QVERIFY(record);
        QVERIFY(f.accounts.saveValidatedV2({packageId, "kugou", "one", "Candidate", true, 1,
            f.schema, {{"credentialBundle", QString::fromUtf8(*record)}}}));
        PluginManagementUiSession edit(&f.manager, &f.accounts, nullptr,
            {packageId, "kugou", "kugou/one", "one", PluginUiMode::Edit}, f.schema);
        PluginManagementUiSession create(&f.manager, &f.accounts, nullptr,
            {packageId, "kugou", {}, {}, PluginUiMode::Create}, f.schema);
        QCOMPARE(edit.state(), "ready"); QCOMPARE(create.state(), "ready");
        QVERIFY(edit.context()->backend()->property("credentialStored").toBool());
        QVERIFY(!create.context()->backend()->property("credentialStored").toBool());
        QVERIFY(edit.context()->sourceInstanceId() != create.context()->sourceInstanceId());
        const auto before = f.accounts.resolvedAccountV2("kugou", "one"); QVERIFY(before);
        bool cleared = false;
        QVERIFY(QMetaObject::invokeMethod(create.context()->backend(), "clearAccount", Q_RETURN_ARG(bool, cleared))); QVERIFY(cleared);
        QTRY_COMPARE(create.context()->backend()->property("state").toString(), "cleared");
        QCOMPARE(f.accounts.resolvedAccountV2("kugou", "one")->secret, before->secret);
        edit.release(); create.release();
        QCOMPARE(f.manager.plugin(packageId).activeLeases, 0);
        QCOMPARE(f.manager.unload(packageId), PluginOperationResult::Success);
    }
};
QTEST_MAIN(KugouManagementUiTest)
#include "tst_KugouManagementUi.moc"
