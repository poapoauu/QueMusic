#include "PluginManager.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "PluginManagementUiSession.h"
#include "SourceAccountStore.h"
#include "v2/ISourceProvidersV2.h"

#include <QGuiApplication>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>

class ExternalSecrets final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool write(const QString &id, const QByteArray &bytes, QString *) override { values[id] = bytes; return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override {
        return values.contains(id) ? std::optional<QByteArray>(values.value(id)) : std::nullopt;
    }
    bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    PluginManager manager;
    manager.addSearchPath(QString::fromLocal8Bit(argv[1]));
    if (manager.discover() != 1) return 3;
    const QString id = QStringLiteral("org.quemusic.source.external-ui-fixture");
    if (!manager.load(id)) {
        qCritical() << manager.plugin(id).error;
        return 4;
    }
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager.pluginInstance(id));
    if (!provider || provider->managementUi().componentUrl
                         != QUrl(QStringLiteral("qml/ManagementPage.qml"))) {
        return 5;
    }
    QTemporaryDir directory;
    QSettings settings(directory.filePath("accounts.ini"), QSettings::IniFormat);
    ExternalSecrets secrets;
    SourceAccountStore store(&settings, &secrets);
    auto *schema = qobject_cast<IPluginSettingsProviderV2 *>(manager.pluginInstance(id));
    if (!schema) return 8;
    PluginManagementUiSession session(&manager, &store, nullptr,
        {id, QStringLiteral("external-ui-fixture"), {}, {}, PluginUiMode::Create}, schema->settingsSchema());
    if (session.state() != QStringLiteral("ready")) return 9;
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[2])
                         + QStringLiteral("/share/quemusic/qml"));
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])
                                                + QStringLiteral("/package/qml/ManagementPage.qml")));
    if (!component.isReady()) {
        qCritical() << component.errorString();
        return 6;
    }
    QScopedPointer<QObject> page(component.createWithInitialProperties(
        {{QStringLiteral("pluginUiContext"), QVariant::fromValue(session.context())}}));
    if (!page) {
        qCritical() << component.errorString();
        return 7;
    }
    session.trackPage(page.data());
    auto *backend = session.context()->backend();
    if (!QMetaObject::invokeMethod(backend, "requestLogin")) return 10;
    QElapsedTimer timer;
    timer.start();
    while (backend->property("loginState").toString() != QStringLiteral("success") && timer.elapsed() < 2000) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    const auto account = store.storedAccount(session.context()->sourceId(), session.context()->accountId());
    if (!account || !account->configuredSecretFieldIds.contains(QStringLiteral("token"))) return 11;
    if (manager.unload(id) != PluginOperationResult::Busy) return 12;
    session.invalidateContext();
    page.reset();
    QCoreApplication::processEvents();
    session.release();
    if (manager.unload(id) != PluginOperationResult::Success) return 13;
    return 0;
}
