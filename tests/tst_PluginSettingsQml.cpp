#include <QGuiApplication>
#include <QFile>
#include <QDir>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include "PluginSettingsController.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "v2/SourceSecretsV2.h"

namespace {

QVariantMap plugin(const QString &id, const QString &name, bool loadable = false)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("version"), QStringLiteral("2.4.0")},
            {QStringLiteral("state"), loadable ? QStringLiteral("unloaded")
                                                : QStringLiteral("loaded")},
            {QStringLiteral("loadable"), loadable},
            {QStringLiteral("unloadable"), !loadable},
            {QStringLiteral("reloadable"), !loadable},
            {QStringLiteral("activeLeases"), 0}};
}

QVariantMap field(const QString &id, const QString &labelKey, int type,
                  const QVariant &value = {}, bool required = false, bool secret = false)
{
    QVariantMap result{{QStringLiteral("id"), id},
                       {QStringLiteral("labelKey"), labelKey},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("visible"), true},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("secret"), secret},
                       {QStringLiteral("choices"), QVariantList{}},
                       {QStringLiteral("constraints"), QVariantMap{}},
                       {QStringLiteral("credentialConfigured"), false}};
    if (!secret && type != 1 && value.isValid())
        result.insert(QStringLiteral("value"), value);
    return result;
}

QString longFallbackFieldId()
{
    return QStringLiteral("generic_setting_identifier_without_translation_that_is_deliberately_long_enough_to_require_multiline_wrapping_at_desktop_and_compact_widths_and_continues_with_additional_fallback_safe_words_for_the_widest_supported_settings_layout_without_using_any_provider_translation_catalog");
}

class PluginSettingsControllerDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantList instances READ instances NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantList settingsSections READ settingsSections NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantList settingsActions READ settingsActions NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantList sourceCapabilities READ sourceCapabilities NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantMap selectedPlugin READ selectedPlugin NOTIFY snapshotsChanged)
    Q_PROPERTY(QString selectedPluginId READ selectedPluginId NOTIFY snapshotsChanged)
    Q_PROPERTY(QString selectedInstanceId READ selectedInstanceId NOTIFY snapshotsChanged)
    Q_PROPERTY(QString lastErrorKey READ lastErrorKey NOTIFY snapshotsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY snapshotsChanged)

public:
    PluginSettingsControllerDouble()
    {
        m_plugins = {plugin(QStringLiteral("fixture.alpha"),
                            QStringLiteral("Fixture source with a deliberately long translated name")),
                     plugin(QStringLiteral("fixture.beta"), QStringLiteral("No-schema fixture"), true)};
        m_selectedPluginId = QStringLiteral("fixture.alpha");
        m_selectedPlugin = m_plugins.constFirst().toMap();
        m_selectedInstanceId = QStringLiteral("fixture.alpha/account-a");
        m_instances = {
            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("fixture.alpha/account-a")},
                        {QStringLiteral("accountId"), QStringLiteral("account-a")},
                        {QStringLiteral("displayName"), QStringLiteral("Home account")},
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("state"), 2},
                        {QStringLiteral("credentialConfigured"), true},
                        {QStringLiteral("configuredSecretFieldIds"), QStringList{QStringLiteral("password")}}},
            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("fixture.alpha/account-b")},
                        {QStringLiteral("accountId"), QStringLiteral("account-b")},
                        {QStringLiteral("displayName"), QStringLiteral("Disconnected account")},
                        {QStringLiteral("enabled"), false},
                        {QStringLiteral("state"), 0},
                        {QStringLiteral("credentialConfigured"), false},
                        {QStringLiteral("configuredSecretFieldIds"), QStringList{}}}
        };

        QVariantMap text = field(QStringLiteral("alias"),
                                 QStringLiteral("source.settings.field.deliberately.long.alias.label"),
                                 0, QStringLiteral("Home"), true);
        text[QStringLiteral("constraints")] = QVariantMap{
            {QStringLiteral("minLength"), 2}, {QStringLiteral("maxLength"), 32},
            {QStringLiteral("pattern"), QStringLiteral("^[A-Za-z ]+$")}};
        QVariantMap longFallback = field(longFallbackFieldId(),
                                         QStringLiteral("source.settings.field.untranslated.long"),
                                         0, QStringLiteral("fallback value"));
        QVariantMap secret = field(QStringLiteral("password"),
                                   QStringLiteral("source.settings.field.password"), 1,
                                   {}, true, true);
        secret[QStringLiteral("credentialConfigured")] = true;
        QVariantMap url = field(QStringLiteral("endpoint"), QStringLiteral("source.settings.field.url"), 2,
                                QStringLiteral("https://music.example.test"), true);
        QVariantMap integer = field(QStringLiteral("port"), QStringLiteral("source.settings.field.port"),
                                    3, 4533, true);
        integer[QStringLiteral("constraints")] = QVariantMap{
            {QStringLiteral("min"), 1}, {QStringLiteral("max"), 65535}};
        QVariantMap boolean = field(QStringLiteral("tls"), QStringLiteral("source.settings.field.tls"),
                                    4, true);
        QVariantMap choice = field(QStringLiteral("quality"), QStringLiteral("source.settings.field.quality"),
                                   5, 320, true);
        choice[QStringLiteral("choices")] = QVariantList{128, 320};
        QVariantMap directory = field(QStringLiteral("library"),
                                      QStringLiteral("source.settings.field.library"), 6,
                                      QStringLiteral("/tmp/music"));
        QVariantMap secretText = field(QStringLiteral("token"),
                                       QStringLiteral("source.settings.field.token"), 0,
                                       {}, false, true);
        secretText[QStringLiteral("credentialConfigured")] = true;
        QVariantMap hidden = field(QStringLiteral("hidden"), QStringLiteral("source.settings.field.hidden"), 0,
                                   QStringLiteral("preserved"));
        hidden[QStringLiteral("visible")] = false;
        m_sections = {QVariantMap{{QStringLiteral("id"), QStringLiteral("connection")},
                                  {QStringLiteral("titleKey"), QStringLiteral("source.settings.section.connection")},
                                  {QStringLiteral("fields"), QVariantList{text, longFallback, secret, url, integer,
                                                                           boolean, choice, directory,
                                                                           secretText, hidden}}}};
        m_actions = {
            QVariantMap{{QStringLiteral("id"), QStringLiteral("unsupported")},
                        {QStringLiteral("labelKey"), QStringLiteral("source.settings.action.unsupported")},
                        {QStringLiteral("requiresConfirmation"), false},
                        {QStringLiteral("state"), 0},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.unsupported")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("refresh")},
                        {QStringLiteral("labelKey"), QStringLiteral("source.settings.action.refresh")},
                        {QStringLiteral("requiresConfirmation"), false},
                        {QStringLiteral("state"), 2},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.unavailable")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("unavailable")},
                        {QStringLiteral("labelKey"), QStringLiteral("source.settings.action.unavailable")},
                        {QStringLiteral("requiresConfirmation"), false},
                        {QStringLiteral("state"), 2},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.unavailable")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("forbidden")},
                        {QStringLiteral("labelKey"), QStringLiteral("source.settings.action.forbidden")},
                        {QStringLiteral("requiresConfirmation"), false},
                        {QStringLiteral("state"), 3},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.forbidden")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("reset")},
                        {QStringLiteral("labelKey"), QStringLiteral("source.settings.action.reset")},
                        {QStringLiteral("requiresConfirmation"), true},
                        {QStringLiteral("state"), 2},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.unavailable")}}
        };
        m_capabilities = {
            QVariantMap{{QStringLiteral("action"), 0}, {QStringLiteral("pluginState"), 1},
                        {QStringLiteral("serverState"), 2}, {QStringLiteral("accountState"), 2},
                        {QStringLiteral("state"), 2},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.unavailable")}},
            QVariantMap{{QStringLiteral("action"), 3}, {QStringLiteral("pluginState"), 3},
                        {QStringLiteral("serverState"), 2}, {QStringLiteral("accountState"), 2},
                        {QStringLiteral("state"), 3},
                        {QStringLiteral("reasonKey"), QStringLiteral("source.settings.forbidden")}}
        };
    }

    QVariantList plugins() const { return m_plugins; }
    QVariantList instances() const { return m_instances; }
    QVariantList settingsSections() const { return m_selectedPluginId == QStringLiteral("fixture.alpha") ? m_sections : QVariantList{}; }
    QVariantList settingsActions() const { return m_selectedPluginId == QStringLiteral("fixture.alpha") ? m_actions : QVariantList{}; }
    QVariantList sourceCapabilities() const { return m_capabilities; }
    QVariantMap selectedPlugin() const { return m_selectedPlugin; }
    QString selectedPluginId() const { return m_selectedPluginId; }
    QString selectedInstanceId() const { return m_selectedInstanceId; }
    QString lastErrorKey() const { return m_lastErrorKey; }
    bool busy() const { return m_busy; }

    void setBusy(bool busy) { m_busy = busy; emit snapshotsChanged(); }
    void emitStatusOnlySnapshot() { emit snapshotsChanged(); }
    void refreshMetadata()
    {
        QVariantMap section = m_sections.constFirst().toMap();
        section[QStringLiteral("titleKey")] = QStringLiteral("source.settings.section.refreshed");
        QVariantList fields = section.value(QStringLiteral("fields")).toList();
        QVariantMap alias = fields[0].toMap();
        alias[QStringLiteral("value")] = QStringLiteral("server replacement");
        alias[QStringLiteral("labelKey")] = QStringLiteral("source.settings.field.refreshed.long.alias");
        fields[0] = alias;
        QVariantMap hidden = fields.last().toMap();
        hidden[QStringLiteral("visible")] = true;
        fields[fields.size() - 1] = hidden;
        section[QStringLiteral("fields")] = fields;
        m_sections = {section};
        emit snapshotsChanged();
    }
    void setPasswordVisible(bool visible)
    {
        QVariantMap section = m_sections.constFirst().toMap();
        QVariantList fields = section.value(QStringLiteral("fields")).toList();
        for (int index = 0; index < fields.size(); ++index) {
            QVariantMap item = fields[index].toMap();
            if (item.value(QStringLiteral("id")).toString() == QStringLiteral("password")) {
                item[QStringLiteral("visible")] = visible;
                fields[index] = item;
            }
        }
        section[QStringLiteral("fields")] = fields;
        m_sections = {section};
        emit snapshotsChanged();
    }
    void setSelectedPluginState(const QString &state)
    {
        m_selectedPlugin[QStringLiteral("state")] = state;
        m_plugins[0] = m_selectedPlugin;
        emit snapshotsChanged();
    }
    void completeConnectionTest(bool success)
    {
        for (int index = 0; index < m_actions.size(); ++index) {
            QVariantMap action = m_actions[index].toMap();
            const QString id = action.value(QStringLiteral("id")).toString();
            if (id == QStringLiteral("refresh") || id == QStringLiteral("reset")) {
                action[QStringLiteral("state")] = success ? 1 : 2;
                action[QStringLiteral("reasonKey")] = success
                    ? QString() : QStringLiteral("source.settings.unavailable");
                m_actions[index] = action;
            }
        }
        if (!m_capabilities.isEmpty()) {
            QVariantMap capability = m_capabilities[0].toMap();
            capability[QStringLiteral("serverState")] = 2;
            capability[QStringLiteral("accountState")] = success ? 1 : 2;
            capability[QStringLiteral("state")] = 2;
            capability[QStringLiteral("reasonKey")] = QStringLiteral("source.settings.unavailable");
            m_capabilities[0] = capability;
        }
        emit snapshotsChanged();
        emit connectionTestFinished(lastTestRequest,
                                    {{QStringLiteral("success"), success},
                                     {QStringLiteral("state"), success ? 1 : 2},
                                     {QStringLiteral("reasonKey"), success ? QString()
                                                                          : QStringLiteral("source.settings.unavailable")}});
    }
    void emitDraftReset() { emit draftReset(); }
    void setManyPlugins()
    {
        m_plugins.clear();
        for (int index = 0; index < 12; ++index) {
            m_plugins.append(plugin(QStringLiteral("fixture.%1").arg(index),
                                    QStringLiteral("Long fixture plugin name %1").arg(index)));
        }
        m_plugins[0] = plugin(QStringLiteral("fixture.alpha"),
                              QStringLiteral("Fixture source with a deliberately long translated name"));
        m_selectedPlugin = m_plugins.constFirst().toMap();
        emit snapshotsChanged();
    }
    void clearPlugins()
    {
        m_plugins.clear();
        m_selectedPlugin.clear();
        m_selectedPluginId.clear();
        emit snapshotsChanged();
    }

    Q_INVOKABLE bool selectPlugin(const QString &packageId)
    {
        ++selectPluginCalls;
        lastSelectedPlugin = packageId;
        m_selectedPluginId = packageId;
        for (const QVariant &candidate : std::as_const(m_plugins)) {
            if (candidate.toMap().value(QStringLiteral("id")).toString() == packageId) {
                m_selectedPlugin = candidate.toMap();
                break;
            }
        }
        emit snapshotsChanged();
        return true;
    }
    Q_INVOKABLE bool selectInstance(const QString &instanceId)
    {
        ++selectInstanceCalls;
        lastSelectedInstance = instanceId;
        m_selectedInstanceId = instanceId;
        emit snapshotsChanged();
        return true;
    }
    Q_INVOKABLE bool setDraftValues(const QVariantMap &publicDraft)
    {
        ++setDraftCalls;
        lastPublicDraft = publicDraft;
        if (!draftAccepted)
            return false;
        QVariantMap section = m_sections.constFirst().toMap();
        QVariantList fields = section.value(QStringLiteral("fields")).toList();
        bool draftChanged = false;
        for (int index = 0; index < fields.size(); ++index) {
            QVariantMap item = fields[index].toMap();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!item.value(QStringLiteral("secret")).toBool()
                && item.value(QStringLiteral("type")).toInt() != 1
                && publicDraft.contains(id)) {
                draftChanged = draftChanged || item.value(QStringLiteral("value")) != publicDraft.value(id);
                item[QStringLiteral("value")] = publicDraft.value(id);
                fields[index] = item;
            }
        }
        section[QStringLiteral("fields")] = fields;
        m_sections = {section};
        for (int index = 0; draftChanged && index < m_actions.size(); ++index) {
            QVariantMap action = m_actions[index].toMap();
            const QString id = action.value(QStringLiteral("id")).toString();
            if (id == QStringLiteral("refresh") || id == QStringLiteral("reset")) {
                action[QStringLiteral("state")] = 2;
                action[QStringLiteral("reasonKey")] = QStringLiteral("source.settings.unavailable");
                m_actions[index] = action;
            }
        }
        if (draftChanged && !m_capabilities.isEmpty()) {
            QVariantMap capability = m_capabilities[0].toMap();
            capability[QStringLiteral("serverState")] = 2;
            capability[QStringLiteral("accountState")] = 2;
            capability[QStringLiteral("state")] = 2;
            m_capabilities[0] = capability;
        }
        emit snapshotsChanged();
        return true;
    }
    Q_INVOKABLE bool setDirectoryField(const QString &fieldId, const QUrl &localFolder)
    {
        ++directoryCalls;
        lastDirectoryField = fieldId;
        lastDirectoryUrl = localFolder;
        if (!directoryAccepted) {
            emit snapshotsChanged();
            return false;
        }
        QVariantMap section = m_sections.constFirst().toMap();
        QVariantList fields = section.value(QStringLiteral("fields")).toList();
        for (int index = 0; index < fields.size(); ++index) {
            QVariantMap item = fields[index].toMap();
            if (item.value(QStringLiteral("id")).toString() == fieldId) {
                item[QStringLiteral("value")] = QStringLiteral("/controller/converted folder");
                fields[index] = item;
            }
        }
        section[QStringLiteral("fields")] = fields;
        m_sections = {section};
        emit snapshotsChanged();
        return true;
    }
    Q_INVOKABLE bool saveInstance(const QString &displayName, const QVariantMap &secretDraft)
    {
        ++saveCalls;
        lastDisplayName = displayName;
        lastSecretDraft = secretDraft;
        return saveAccepted;
    }
    Q_INVOKABLE bool removeInstance(const QString &instanceId)
    {
        ++removeCalls;
        lastRemovedInstance = instanceId;
        return true;
    }
    Q_INVOKABLE bool setInstanceEnabled(const QString &instanceId, bool enabled)
    {
        ++enableCalls;
        lastEnabledInstance = instanceId;
        lastEnabledValue = enabled;
        return true;
    }
    Q_INVOKABLE QUuid testConnection(const QVariantMap &secretDraft)
    {
        ++testCalls;
        lastSecretDraft = secretDraft;
        lastTestRequest = QUuid::createUuid();
        return lastTestRequest;
    }
    Q_INVOKABLE QUuid runSettingsAction(const QString &actionId, const QVariantMap &secretDraft,
                                        bool confirmed = false)
    {
        ++actionCalls;
        lastActionId = actionId;
        lastActionConfirmed = confirmed;
        lastSecretDraft = secretDraft;
        return QUuid::createUuid();
    }
    Q_INVOKABLE void cancelOperation() { ++cancelCalls; }
    Q_INVOKABLE void discoverPlugins() { ++discoverCalls; }
    Q_INVOKABLE bool loadPlugin(const QString &id) { ++loadCalls; lastLifecycleId = id; return true; }
    Q_INVOKABLE bool unloadPlugin(const QString &id)
    {
        ++unloadCalls;
        lastLifecycleId = id;
        m_selectedPlugin[QStringLiteral("state")] = QStringLiteral("unloaded");
        m_selectedPlugin[QStringLiteral("loadable")] = true;
        m_selectedPlugin[QStringLiteral("unloadable")] = false;
        m_selectedPlugin[QStringLiteral("reloadable")] = true;
        m_plugins[0] = m_selectedPlugin;
        m_sections.clear();
        m_actions.clear();
        m_capabilities.clear();
        emit snapshotsChanged();
        return true;
    }
    Q_INVOKABLE bool reloadPlugin(const QString &id) { ++reloadCalls; lastLifecycleId = id; return true; }

    bool draftAccepted = true;
    bool saveAccepted = true;
    bool directoryAccepted = true;
    int selectPluginCalls = 0;
    int selectInstanceCalls = 0;
    int setDraftCalls = 0;
    int directoryCalls = 0;
    int saveCalls = 0;
    int removeCalls = 0;
    int enableCalls = 0;
    int testCalls = 0;
    int actionCalls = 0;
    int cancelCalls = 0;
    int discoverCalls = 0;
    int loadCalls = 0;
    int unloadCalls = 0;
    int reloadCalls = 0;
    QString lastSelectedPlugin;
    QString lastSelectedInstance;
    QString lastDisplayName;
    QString lastRemovedInstance;
    QString lastEnabledInstance;
    QString lastDirectoryField;
    QString lastActionId;
    QString lastLifecycleId;
    bool lastEnabledValue = false;
    bool lastActionConfirmed = false;
    QUrl lastDirectoryUrl;
    QUuid lastTestRequest;
    QVariantMap lastPublicDraft;
    QVariantMap lastSecretDraft;

signals:
    void snapshotsChanged();
    void connectionTestFinished(QUuid requestId, QVariantMap result);
    void settingsActionFinished(QUuid requestId, QVariantMap result);
    void draftReset();

private:
    QVariantList m_plugins;
    QVariantList m_instances;
    QVariantList m_sections;
    QVariantList m_actions;
    QVariantList m_capabilities;
    QVariantMap m_selectedPlugin;
    QString m_selectedPluginId;
    QString m_selectedInstanceId;
    QString m_lastErrorKey;
    bool m_busy = false;
};

QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
{
    if (root == nullptr)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = findVisualItem(child, name))
            return match;
    }
    return nullptr;
}

struct PanelHarness {
    QQmlEngine engine;
    QQuickWindow window;
    QQmlComponent component;
    QPointer<QQuickItem> panel;

    PanelHarness(QObject *controller, QSize size = QSize(1200, 800))
        : component(&engine)
    {
        engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
        component.loadUrl(QUrl(QStringLiteral("qrc:/QueMusic/components/PluginSettingsPanel.qml")));
        window.resize(size);
        QObject *created = component.createWithInitialProperties({
            {QStringLiteral("width"), size.width()},
            {QStringLiteral("height"), size.height()},
            {QStringLiteral("containX"), 0},
            {QStringLiteral("standWidth"), size.width()},
            {QStringLiteral("controller"), QVariant::fromValue(controller)}
        });
        panel = qobject_cast<QQuickItem *>(created);
        if (panel) {
            panel->setParentItem(window.contentItem());
            window.show();
            QTest::qWait(30);
        }
    }

    ~PanelHarness() { delete panel; }

    QObject *named(const QString &name) const
    {
        if (!panel)
            return nullptr;
        if (QQuickItem *visual = findVisualItem(panel, name))
            return visual;
        return panel->findChild<QObject *>(name, Qt::FindChildrenRecursively);
    }
};

class UiSecrets final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool write(const QString &id, const QByteArray &bytes, QString *) override { values.insert(id, bytes); return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override {
        return values.contains(id) ? std::optional<QByteArray>(values.value(id)) : std::nullopt;
    }
    bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};
struct RealPanelHarness {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    UiSecrets secrets;
    SourceAccountStore store{&settings, &secrets};
    PluginManager manager;
    SourceRegistry registry{&manager, &store};
    std::unique_ptr<PluginSettingsController> controller;
    std::unique_ptr<PanelHarness> panel;
    const QString id = QStringLiteral("org.quemusic.source.ui-valid");
    bool load(const QString &packages = QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT)) {
        manager.addSearchPath(packages);
        if (manager.discover() < 1 || !manager.load(id)) return false;
        controller = std::make_unique<PluginSettingsController>(&manager, &registry, &store);
        if (!controller->selectPlugin(id)) return false;
        panel = std::make_unique<PanelHarness>(controller.get());
        return panel->panel;
    }
};

bool click(QObject *object)
{
    return object && QMetaObject::invokeMethod(object, "click", Qt::DirectConnection);
}

bool editText(QObject *object, const QString &text)
{
    if (!object || !object->setProperty("text", text))
        return false;
    return QMetaObject::invokeMethod(object, "textEdited", Qt::DirectConnection);
}

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

} // namespace

class PluginSettingsQmlTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void nullControllerShowsUnavailable();
    void selectionAndMultiInstanceCrud();
    void schemaTypesVisibilityAndCredentialPlaceholder();
    void invalidVisibleDraftBlocksStaleSubmission();
    void nestedStringConstraintsAndActualUnload();
    void failedSynchronizationAndSaveRetainDraft();
    void sameInstanceMetadataRefreshPreservesPrivateDraft();
    void conditionalSecretHideShowPreservesPrivateDraft();
    void secretsClearOnSelectionCancelAndSuccessfulSave();
    void connectionTestRetainsUnsavedPasswordOnlyInInput();
    void actionConfirmationAndAvailability();
    void runtimeTransitionUsesProbeProvenance();
    void localEditsRequireFreshExplicitProbe();
    void inFlightProbeCannotAuthorizeEditedDraft();
    void musicDiagnosticsAreNotMediaPermission();
    void directorySelectionUsesLocalUrlAdapter();
    void rejectedDirectoryConversionPreservesLocalOverride();
    void lifecycleBusyAndNoSchemaStates();
    void longFallbackLabelWraps_data();
    void longFallbackLabelWraps();
    void responsiveGeometryAndTheme_data();
    void responsiveGeometryAndTheme();
    void clearsPageBeforeReleasingLease();
    void schemaOnlyPluginRetainsCurrentSettingsFlow();
    void customUiReceivesOnlyPluginUiContext();
    void loadFailureShowsGenericErrorState();
    void qrLoginSavesSecretAndIgnoresCancelledCompletion();
    void managementLifecycleOperations_data();
    void managementLifecycleOperations();
    void managementNotificationIsDisplayed();
    void publicFormIgnoresUnrelatedCompletion();
    void publicFormVisibilityUsesUnsavedDraft();

private:
    QTemporaryDir m_settingsDirectory;
};

void PluginSettingsQmlTest::clearsPageBeforeReleasingLease()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.controller->managementUiSession());
    QTRY_VERIFY(h.panel->named("managementPageLoader"));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    QPointer<QObject> page = h.panel->named("fixtureManagementPage");
    QPointer<QObject> backend = h.controller->managementUiSession()->context()->backend();
    QVERIFY(backend);
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 1);
    bool pageGoneAtLeaseRelease = false;
    connect(&h.manager, &PluginManager::pluginChanged, h.controller.get(), [&] {
        if (h.manager.plugin(h.id).activeLeases == 0) pageGoneAtLeaseRelease = page.isNull();
    });
    QVERIFY(click(h.panel->named("closeManagementUiAction")));
    QTRY_VERIFY(!h.controller->managementUiSession());
    QVERIFY(backend.isNull());
    QVERIFY(pageGoneAtLeaseRelease);
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 0);
}

void PluginSettingsQmlTest::schemaOnlyPluginRetainsCurrentSettingsFlow()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY(harness.panel);
    QVERIFY(harness.named("openManagementUiAction"));
    QVERIFY(!harness.named("openManagementUiAction")->property("visible").toBool());
    QVERIFY(harness.named("schemaSettingsForm"));
    QVERIFY(click(harness.named("saveInstanceAction")));
    QCOMPARE(controller.saveCalls, 1);
}

void PluginSettingsQmlTest::customUiReceivesOnlyPluginUiContext()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    auto *page = h.panel->named("fixtureManagementPage");
    QCOMPARE(page->property("pluginUiContext").value<QObject *>(),
             static_cast<QObject *>(h.controller->managementUiSession()->context()));
    QVERIFY(page->property("controller").isNull());
}

void PluginSettingsQmlTest::loadFailureShowsGenericErrorState()
{
    QTemporaryDir packages;
    QVERIFY(packages.isValid());
    const QString destination = packages.filePath("valid");
    QVERIFY(QDir().mkpath(destination + "/qml"));
    const QDir source(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT) + "/valid");
    for (const auto &name : source.entryList(QDir::Files))
        QVERIFY(QFile::copy(source.filePath(name), destination + '/' + name));
    QFile broken(destination + "/qml/ManagementPage.qml");
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("import QtQuick\nItem { invalid syntax\n");
    broken.close();
    RealPanelHarness h; QVERIFY(h.load(packages.path()));
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    auto *error = h.panel->named("managementPageError");
    QVERIFY(error);
    QTRY_VERIFY(error->property("visible").toBool());
    QVERIFY(!error->property("description").toString().contains(packages.path()));
    QVERIFY(click(h.panel->named("closeManagementUiAction")));
    QTRY_VERIFY(!h.controller->managementUiSession());
}

void PluginSettingsQmlTest::qrLoginSavesSecretAndIgnoresCancelledCompletion()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    auto *context = h.controller->managementUiSession()->context();
    auto *backend = context->backend();
    QVERIFY(QMetaObject::invokeMethod(backend, "failNextLogin"));
    QVERIFY(QMetaObject::invokeMethod(backend, "requestLogin"));
    QTRY_COMPARE(backend->property("loginState").toString(), QString("error"));
    QVERIFY(!h.store.storedAccount(context->sourceId(), context->accountId()));
    QVERIFY(QMetaObject::invokeMethod(backend, "requestLogin"));
    QVERIFY(QMetaObject::invokeMethod(backend, "cancelLogin"));
    QTest::qWait(80);
    QCOMPARE(backend->property("loginState").toString(), QString("idle"));
    QVERIFY(!h.store.storedAccount(context->sourceId(), context->accountId()));
    QVERIFY(QMetaObject::invokeMethod(backend, "requestLogin"));
    QTRY_COMPARE(backend->property("loginState").toString(), QString("success"));
    auto account = h.store.storedAccount(context->sourceId(), context->accountId());
    QVERIFY(account);
    QCOMPARE(decodeSourceSecretsV2(h.secrets.values.value(account->secretReference))->value("token"),
             QByteArray("fixture-native-secret"));
    QVERIFY(!context->settings()->property("publicValues").toMap().contains("token"));
    QVERIFY(click(h.panel->named("closeManagementUiAction")));
    QTRY_VERIFY(!h.controller->managementUiSession());

    QVERIFY(h.controller->selectInstance(""));
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    backend = h.controller->managementUiSession()->context()->backend();
    QVERIFY(QMetaObject::invokeMethod(backend, "requestLogin"));
    QVERIFY(click(h.panel->named("closeManagementUiAction")));
    QTRY_VERIFY(!h.controller->managementUiSession());
    QTest::qWait(80);
    QCOMPARE(h.store.accounts().size(), 1);
}

void PluginSettingsQmlTest::managementNotificationIsDisplayed()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    auto *host = h.controller->managementUiSession()->context()->host();
    QVERIFY(QMetaObject::invokeMethod(host, "notify", Q_ARG(QString, "plugin.ui.saved"), Q_ARG(bool, false)));
    auto *notice = h.panel->named("managementNotification");
    QVERIFY(notice);
    QVERIFY(notice->property("visible").toBool());
    QVERIFY(!notice->property("text").toString().isEmpty());
}

void PluginSettingsQmlTest::publicFormIgnoresUnrelatedCompletion()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureSettingsForm"));
    auto *form = h.panel->named("fixtureSettingsForm");
    auto *settings = h.controller->managementUiSession()->context()->settings();
    QVERIFY(QMetaObject::invokeMethod(form, "savePublic"));
    QVERIFY(form->property("busy").toBool());
    QVERIFY(QMetaObject::invokeMethod(settings, "operationFinished", Qt::DirectConnection,
        Q_ARG(QUuid, QUuid::createUuid()), Q_ARG(bool, false), Q_ARG(QString, "source.settings.invalidDraft")));
    QVERIFY(form->property("busy").toBool());
    QTRY_VERIFY(!form->property("busy").toBool());
    QCOMPARE(form->property("errorKey").toString(), QString());
}

void PluginSettingsQmlTest::publicFormVisibilityUsesUnsavedDraft()
{
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureSettingsForm"));
    auto *form = h.panel->named("fixtureSettingsForm");
    const QVariantMap field{{"visible", false}, {"visibleWhen", QVariantMap{
        {"fieldId", "enabled"}, {"comparison", 0}, {"value", true}}}};
    QVERIFY(form->setProperty("draft", QVariantMap{{"enabled", true}}));
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(form, "fieldVisible", Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, field)));
    QVERIFY(result.toBool());
    QVERIFY(form->setProperty("draft", QVariantMap{{"enabled", false}}));
    QVERIFY(QMetaObject::invokeMethod(form, "fieldVisible", Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, field)));
    QVERIFY(!result.toBool());
}

void PluginSettingsQmlTest::managementLifecycleOperations_data()
{
    QTest::addColumn<QString>("operation");
    for (const auto &operation : {"switch", "hide", "unload", "reload", "destroyController"})
        QTest::newRow(operation) << QString::fromLatin1(operation);
}
void PluginSettingsQmlTest::managementLifecycleOperations()
{
    QFETCH(QString, operation);
    RealPanelHarness h; QVERIFY(h.load());
    QVERIFY(click(h.panel->named("openManagementUiAction")));
    QTRY_VERIFY(h.panel->named("fixtureManagementPage"));
    QPointer<QObject> page = h.panel->named("fixtureManagementPage");
    QPointer<QObject> backend = h.controller->managementUiSession()->context()->backend();
    if (operation == "switch") QVERIFY(h.controller->selectInstance(""));
    else if (operation == "hide") h.panel->panel->setVisible(false);
    else if (operation == "unload") QVERIFY(h.controller->unloadPlugin(h.id));
    else if (operation == "reload") QVERIFY(h.controller->reloadPlugin(h.id));
    else {
        h.controller.reset();
        h.panel->panel->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
    }
    QTRY_VERIFY(page.isNull());
    QTRY_VERIFY(backend.isNull());
    QTRY_COMPARE(h.manager.plugin(h.id).activeLeases, 0);
}

void PluginSettingsQmlTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("QueMusicTask8cTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PluginSettingsQml"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settingsDirectory.path());
    QSettings().clear();
}

void PluginSettingsQmlTest::nullControllerShowsUnavailable()
{
    PanelHarness harness(nullptr);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *placeholder = harness.named(QStringLiteral("pluginUnavailablePlaceholder"));
    QVERIFY(placeholder);
    QVERIFY(placeholder->property("visible").toBool());
    QVERIFY(!harness.named(QStringLiteral("navidromeConfigAction")));
}

void PluginSettingsQmlTest::selectionAndMultiInstanceCrud()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QCOMPARE(harness.panel->property("controller").value<QObject *>(),
             static_cast<QObject *>(&controller));
    QCOMPARE(harness.panel->property("plugins").toList().size(), 2);
    QObject *master = harness.named(QStringLiteral("pluginMasterList"));
    QVERIFY(master);
    QVERIFY(master->property("width").toReal() > 0);
    QVERIFY(master->property("height").toReal() > 0);

    QVERIFY(click(harness.named(QStringLiteral("pluginSelect_fixture.beta"))));
    QCOMPARE(controller.lastSelectedPlugin, QStringLiteral("fixture.beta"));
    QVERIFY(click(harness.named(QStringLiteral("pluginSelect_fixture.alpha"))));
    QVERIFY(click(harness.named(QStringLiteral("newInstanceAction"))));
    QCOMPARE(controller.lastSelectedInstance, QString());

    QVERIFY(controller.selectInstance(QStringLiteral("fixture.alpha/account-a")));
    QCoreApplication::processEvents();
    QVERIFY(click(harness.named(QStringLiteral("instanceSelect_fixture.alpha/account-b"))));
    QCOMPARE(controller.lastSelectedInstance, QStringLiteral("fixture.alpha/account-b"));
    QVERIFY(click(harness.named(QStringLiteral("instanceEnable_fixture.alpha/account-b"))));
    QCOMPARE(controller.lastEnabledInstance, QStringLiteral("fixture.alpha/account-b"));
    QCOMPARE(controller.lastEnabledValue, true);

    QVERIFY(click(harness.named(QStringLiteral("instanceRemove_fixture.alpha/account-b"))));
    QObject *dialog = harness.named(QStringLiteral("instanceRemovalConfirmation"));
    QVERIFY(dialog && dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
    QCOMPARE(controller.lastRemovedInstance, QStringLiteral("fixture.alpha/account-b"));
}

void PluginSettingsQmlTest::schemaTypesVisibilityAndCredentialPlaceholder()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));

    const QStringList visibleFields{QStringLiteral("alias"), QStringLiteral("password"),
                                    QStringLiteral("endpoint"), QStringLiteral("port"),
                                    QStringLiteral("tls"), QStringLiteral("quality"),
                                    QStringLiteral("library"), QStringLiteral("token")};
    for (const QString &id : visibleFields)
        QVERIFY2(harness.named(QStringLiteral("field_%1").arg(id)), qPrintable(id));
    QObject *hidden = harness.named(QStringLiteral("field_hidden"));
    QVERIFY(!hidden || !hidden->property("visible").toBool());

    QObject *password = harness.named(QStringLiteral("field_password"));
    QObject *secretText = harness.named(QStringLiteral("field_token"));
    QCOMPARE(password->property("text").toString(), QString());
    QCOMPARE(secretText->property("text").toString(), QString());
    QVERIFY(password->property("echoMode").toInt() != 0);
    QVERIFY(secretText->property("echoMode").toInt() != 0);
    QVERIFY(password->property("placeholderText").toString().contains(QStringLiteral("configured"),
                                                                        Qt::CaseInsensitive));
    QObject *port = harness.named(QStringLiteral("field_port"));
    QCOMPARE(port->property("from").toInt(), 1);
    QCOMPARE(port->property("to").toInt(), 65535);
    QObject *choice = harness.named(QStringLiteral("field_quality"));
    QCOMPARE(choice->property("model").toList(), QVariantList({128, 320}));
    QCOMPARE(choice->property("currentValue").toInt(), 320);
    QVERIFY(choice->setProperty("currentIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Qt::DirectConnection, Q_ARG(int, 0)));
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.lastPublicDraft.value(QStringLiteral("quality")).toInt(), 128);

    const QVariantList sections = harness.panel->property("formSections").toList();
    QVERIFY(!sections.isEmpty());
    const QVariantList fields = sections.constFirst().toMap().value(QStringLiteral("fields")).toList();
    for (const QVariant &value : fields) {
        const QVariantMap fieldData = value.toMap();
        if (fieldData.value(QStringLiteral("id")).toString() == QStringLiteral("password")
            || fieldData.value(QStringLiteral("id")).toString() == QStringLiteral("token")) {
            QVERIFY(!fieldData.contains(QStringLiteral("value")));
        }
    }
    QCOMPARE(sections.constFirst().toMap().value(QStringLiteral("titleKey")).toString(),
             QStringLiteral("source.settings.section.connection"));
}

void PluginSettingsQmlTest::invalidVisibleDraftBlocksStaleSubmission()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));

    QObject *endpoint = harness.named(QStringLiteral("field_endpoint"));
    QVERIFY(editText(endpoint, QStringLiteral("https://valid.example.test")));
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.saveCalls, 1);
    QCOMPARE(controller.lastPublicDraft.value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("https://valid.example.test"));

    endpoint = harness.named(QStringLiteral("field_endpoint"));
    QVERIFY(editText(endpoint, QStringLiteral("not a URL")));
    QObject *save = harness.named(QStringLiteral("saveInstanceAction"));
    QObject *test = harness.named(QStringLiteral("testConnectionAction"));
    QObject *run = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(!save->property("enabled").toBool());
    QVERIFY(!test->property("enabled").toBool());
    QVERIFY(!run->property("enabled").toBool());
    click(save);
    click(test);
    click(run);
    QCOMPARE(controller.saveCalls, 1);
    QCOMPARE(controller.testCalls, 0);
    QCOMPARE(controller.actionCalls, 0);
    QCOMPARE(controller.setDraftCalls, 1);

    const QStringList invalidUrls{QStringLiteral("ftp://music.example.test"),
                                  QStringLiteral("/relative/path"),
                                  QStringLiteral("https:///missing-host"),
                                  QStringLiteral("https://user:secret@music.example.test")};
    for (const QString &invalid : invalidUrls) {
        QVERIFY(editText(endpoint, invalid));
        QVERIFY2(!save->property("enabled").toBool(), qPrintable(invalid));
    }
    QVERIFY(editText(endpoint, QStringLiteral("http://music.example.test:4533/base")));
    QVERIFY(save->property("enabled").toBool());
}

void PluginSettingsQmlTest::nestedStringConstraintsAndActualUnload()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *alias = harness.named(QStringLiteral("field_alias"));
    QObject *save = harness.named(QStringLiteral("saveInstanceAction"));
    QObject *test = harness.named(QStringLiteral("testConnectionAction"));
    QObject *run = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(alias && save && test && run);

    const QStringList invalidAliases{QStringLiteral("A"), QString(33, QLatin1Char('A')),
                                       QStringLiteral("Alpha_1")};
    for (const QString &invalid : invalidAliases) {
        QVERIFY(editText(alias, invalid));
        QVERIFY(!save->property("enabled").toBool());
        QVERIFY(!test->property("enabled").toBool());
        QVERIFY(!run->property("enabled").toBool());
        click(save);
        click(test);
        click(run);
        QCOMPARE(controller.saveCalls, 0);
        QCOMPARE(controller.testCalls, 0);
        QCOMPARE(controller.actionCalls, 0);
        QVERIFY(editText(alias, QStringLiteral("Valid Alias")));
        QVERIFY(save->property("enabled").toBool());
    }

    auto *password = harness.named(QStringLiteral("field_password"));
    QVERIFY(editText(password, QStringLiteral("unload-private-secret")));
    QPointer<QObject> watchedPassword(password);
    QVERIFY(click(harness.named(QStringLiteral("unloadPluginAction"))));
    QCOMPARE(controller.unloadCalls, 1);
    QCoreApplication::processEvents();
    QVERIFY(!harness.named(QStringLiteral("field_password")));
    QVERIFY(watchedPassword.isNull()
            || watchedPassword->property("text").toString().isEmpty());
}

void PluginSettingsQmlTest::failedSynchronizationAndSaveRetainDraft()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *alias = harness.named(QStringLiteral("field_alias"));
    QObject *password = harness.named(QStringLiteral("field_password"));

    controller.draftAccepted = false;
    QVERIFY(editText(alias, QStringLiteral("Local Draft")));
    QVERIFY(editText(password, QStringLiteral("private-draft")));
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.saveCalls, 0);
    QCOMPARE(alias->property("text").toString(), QStringLiteral("Local Draft"));
    QCOMPARE(password->property("text").toString(), QStringLiteral("private-draft"));
    QVERIFY(!harness.named(QStringLiteral("saveInstanceAction"))->property("enabled").toBool());

    controller.draftAccepted = true;
    controller.saveAccepted = false;
    QVERIFY(editText(alias, QStringLiteral("Retry Draft")));
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.saveCalls, 1);
    QCOMPARE(alias->property("text").toString(), QStringLiteral("Retry Draft"));
    QCOMPARE(password->property("text").toString(), QStringLiteral("private-draft"));
}

void PluginSettingsQmlTest::sameInstanceMetadataRefreshPreservesPrivateDraft()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *alias = harness.named(QStringLiteral("field_alias"));
    QObject *password = harness.named(QStringLiteral("field_password"));
    QVERIFY(editText(alias, QStringLiteral("Unsaved Public")));
    QVERIFY(editText(password, QStringLiteral("unsaved-private")));

    controller.refreshMetadata();
    QCoreApplication::processEvents();
    QCOMPARE(alias->property("text").toString(), QStringLiteral("Unsaved Public"));
    QCOMPARE(password->property("text").toString(), QStringLiteral("unsaved-private"));
    QVERIFY(harness.named(QStringLiteral("field_hidden")));
    QObject *label = harness.named(QStringLiteral("fieldLabel_alias"));
    QVERIFY(label);
    QCOMPARE(label->property("presentationKey").toString(),
             QStringLiteral("source.settings.field.refreshed.long.alias"));
}

void PluginSettingsQmlTest::conditionalSecretHideShowPreservesPrivateDraft()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *password = harness.named(QStringLiteral("field_password"));
    QVERIFY(editText(password, QStringLiteral("private-through-visibility")));
    QPointer<QObject> original(password);

    controller.setPasswordVisible(false);
    QCoreApplication::processEvents();
    QVERIFY(!original.isNull());
    QCOMPARE(original->property("text").toString(), QStringLiteral("private-through-visibility"));

    controller.setPasswordVisible(true);
    QCoreApplication::processEvents();
    password = harness.named(QStringLiteral("field_password"));
    QVERIFY(password);
    QCOMPARE(password, original.data());
    QCOMPARE(password->property("text").toString(), QStringLiteral("private-through-visibility"));
}

void PluginSettingsQmlTest::secretsClearOnSelectionCancelAndSuccessfulSave()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *password = harness.named(QStringLiteral("field_password"));
    QVERIFY(editText(password, QStringLiteral("selection-secret")));
    QVERIFY(click(harness.named(QStringLiteral("pluginSelect_fixture.beta"))));
    QVERIFY(click(harness.named(QStringLiteral("pluginSelect_fixture.alpha"))));
    password = harness.named(QStringLiteral("field_password"));
    QCOMPARE(password->property("text").toString(), QString());

    QVERIFY(editText(password, QStringLiteral("cancel-secret")));
    QVERIFY(click(harness.named(QStringLiteral("cancelDraftAction"))));
    QCOMPARE(password->property("text").toString(), QString());

    QVERIFY(editText(password, QStringLiteral("save-secret")));
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.lastSecretDraft.value(QStringLiteral("password")).toString(),
             QStringLiteral("save-secret"));
    QCOMPARE(password->property("text").toString(), QString());
    QVERIFY(!controller.lastPublicDraft.contains(QStringLiteral("password")));
    QVERIFY(!controller.lastPublicDraft.contains(QStringLiteral("token")));

    QVERIFY(editText(password, QStringLiteral("reset-secret")));
    controller.emitDraftReset();
    QCoreApplication::processEvents();
    QCOMPARE(password->property("text").toString(), QString());

    QVERIFY(editText(password, QStringLiteral("status-secret")));
    controller.emitStatusOnlySnapshot();
    QCoreApplication::processEvents();
    QCOMPARE(password->property("text").toString(), QStringLiteral("status-secret"));

    controller.setSelectedPluginState(QStringLiteral("unloaded"));
    QCoreApplication::processEvents();
    QCOMPARE(password->property("text").toString(), QString());
    QObject *stateLabel = harness.named(QStringLiteral("pluginSummaryState"));
    QVERIFY(stateLabel);
    QVERIFY(stateLabel->property("text").toString().contains(QStringLiteral("unloaded"),
                                                               Qt::CaseInsensitive));

    PluginSettingsControllerDouble replacement;
    QVERIFY(harness.panel->setProperty("controller", QVariant::fromValue(static_cast<QObject *>(&replacement))));
    QCoreApplication::processEvents();
    password = harness.named(QStringLiteral("field_password"));
    QCOMPARE(password->property("text").toString(), QString());
    QPointer<QObject> watchedSecret(password);
    delete harness.panel;
    QVERIFY(watchedSecret.isNull());
}

void PluginSettingsQmlTest::connectionTestRetainsUnsavedPasswordOnlyInInput()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *password = harness.named(QStringLiteral("field_password"));
    QVERIFY(editText(password, QStringLiteral("test-only-secret")));
    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QCOMPARE(controller.testCalls, 1);
    QCOMPARE(controller.saveCalls, 0);
    QCOMPARE(controller.lastSecretDraft.value(QStringLiteral("password")).toString(),
             QStringLiteral("test-only-secret"));
    QCOMPARE(password->property("text").toString(), QStringLiteral("test-only-secret"));
    QVERIFY(!controller.lastPublicDraft.contains(QStringLiteral("password")));
}

void PluginSettingsQmlTest::actionConfirmationAndAvailability()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QVERIFY(!harness.named(QStringLiteral("settingsAction_unsupported")));
    QObject *unavailable = harness.named(QStringLiteral("settingsAction_unavailable"));
    QObject *forbidden = harness.named(QStringLiteral("settingsAction_forbidden"));
    QVERIFY(unavailable && !unavailable->property("enabled").toBool());
    QVERIFY(forbidden && !forbidden->property("enabled").toBool());
    QVERIFY(harness.named(QStringLiteral("settingsActionReason_unavailable")));
    QVERIFY(harness.named(QStringLiteral("settingsActionReason_forbidden")));
    QVERIFY(harness.named(QStringLiteral("settingsActionReason_unavailable"))
                ->property("text").toString().contains(QStringLiteral("unavailable"),
                                                        Qt::CaseInsensitive));
    QVERIFY(harness.named(QStringLiteral("settingsActionReason_forbidden"))
                ->property("text").toString().contains(QStringLiteral("forbidden"),
                                                        Qt::CaseInsensitive));

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();

    QVERIFY(click(harness.named(QStringLiteral("settingsAction_refresh"))));
    QCOMPARE(controller.actionCalls, 1);
    QCOMPARE(controller.lastActionId, QStringLiteral("refresh"));
    QCOMPARE(controller.lastActionConfirmed, false);

    QVERIFY(click(harness.named(QStringLiteral("settingsAction_reset"))));
    QCOMPARE(controller.actionCalls, 1);
    QObject *dialog = harness.named(QStringLiteral("settingsActionConfirmation"));
    QVERIFY(dialog && dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
    QCOMPARE(controller.actionCalls, 2);
    QCOMPARE(controller.lastActionId, QStringLiteral("reset"));
    QCOMPARE(controller.lastActionConfirmed, true);
}

void PluginSettingsQmlTest::runtimeTransitionUsesProbeProvenance()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    QObject *server = harness.named(QStringLiteral("capabilityServer_0"));
    QObject *account = harness.named(QStringLiteral("capabilityAccount_0"));
    QVERIFY(refresh && server && account);
    QVERIFY(!refresh->property("enabled").toBool());
    QVERIFY(server->property("text").toString().contains(QStringLiteral("not checked"),
                                                           Qt::CaseInsensitive));
    QVERIFY(account->property("text").toString().contains(QStringLiteral("not checked"),
                                                            Qt::CaseInsensitive));

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QCOMPARE(controller.testCalls, 1);
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    server = harness.named(QStringLiteral("capabilityServer_0"));
    account = harness.named(QStringLiteral("capabilityAccount_0"));
    QVERIFY(refresh->property("enabled").toBool());
    QVERIFY(server->property("text").toString().contains(QStringLiteral("unavailable"),
                                                           Qt::CaseInsensitive));
    QVERIFY(!server->property("text").toString().contains(QStringLiteral("not checked"),
                                                            Qt::CaseInsensitive));
    QVERIFY(account->property("text").toString().contains(QStringLiteral("available"),
                                                            Qt::CaseInsensitive));

    QVERIFY(editText(harness.named(QStringLiteral("field_alias")), QStringLiteral("Failed Probe")));
    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QCOMPARE(controller.testCalls, 2);
    controller.completeConnectionTest(false);
    QCoreApplication::processEvents();
    refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    server = harness.named(QStringLiteral("capabilityServer_0"));
    QVERIFY(!refresh->property("enabled").toBool());
    QVERIFY(server->property("text").toString().contains(QStringLiteral("not checked"),
                                                           Qt::CaseInsensitive));
}

void PluginSettingsQmlTest::localEditsRequireFreshExplicitProbe()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(refresh && refresh->property("enabled").toBool());

    QVERIFY(editText(harness.named(QStringLiteral("field_alias")), QStringLiteral("Changed Draft")));
    QVERIFY(!refresh->property("enabled").toBool());
    QVERIFY(click(harness.named(QStringLiteral("saveInstanceAction"))));
    QCOMPARE(controller.saveCalls, 1);
    QVERIFY(!refresh->property("enabled").toBool());
    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QCOMPARE(controller.testCalls, 2);
    QVERIFY(!refresh->property("enabled").toBool());
    controller.completeConnectionTest(false);
    QCoreApplication::processEvents();
    refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(!refresh->property("enabled").toBool());
    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QCOMPARE(controller.testCalls, 3);
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    refresh = harness.named(QStringLiteral("settingsAction_refresh"));
    QVERIFY(refresh->property("enabled").toBool());

    QVERIFY(editText(harness.named(QStringLiteral("field_password")), QStringLiteral("new secret")));
    QVERIFY(!refresh->property("enabled").toBool());
    controller.completeConnectionTest(true); // stale completion cannot restore the grant
    QCoreApplication::processEvents();
    QVERIFY(!refresh->property("enabled").toBool());
}

void PluginSettingsQmlTest::inFlightProbeCannotAuthorizeEditedDraft()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QVERIFY(editText(harness.named(QStringLiteral("field_alias")), QStringLiteral("Public During Probe")));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    QVERIFY(!harness.named(QStringLiteral("settingsAction_refresh"))->property("enabled").toBool());

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QVERIFY(editText(harness.named(QStringLiteral("field_password")), QStringLiteral("secret-during-probe")));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    QVERIFY(!harness.named(QStringLiteral("settingsAction_refresh"))->property("enabled").toBool());

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    QVERIFY(editText(harness.named(QStringLiteral("instanceDisplayNameField")),
                     QStringLiteral("Display During Probe")));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    QVERIFY(!harness.named(QStringLiteral("settingsAction_refresh"))->property("enabled").toBool());

    QVERIFY(click(harness.named(QStringLiteral("testConnectionAction"))));
    controller.completeConnectionTest(true);
    QCoreApplication::processEvents();
    QVERIFY(harness.named(QStringLiteral("settingsAction_refresh"))->property("enabled").toBool());
    QCOMPARE(controller.testCalls, 4);
}

void PluginSettingsQmlTest::musicDiagnosticsAreNotMediaPermission()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QCOMPARE(controller.testCalls, 0);
    QCOMPARE(controller.actionCalls, 0);
    QObject *diagnostics = harness.named(QStringLiteral("sourceCapabilityDiagnostics"));
    QObject *disclaimer = harness.named(QStringLiteral("mediaPermissionDisclaimer"));
    QVERIFY(diagnostics && disclaimer);
    QVERIFY(disclaimer->property("text").toString().contains(QStringLiteral("media"),
                                                               Qt::CaseInsensitive));
    QObject *runtime = harness.named(QStringLiteral("capabilityServer_0"));
    QObject *account = harness.named(QStringLiteral("capabilityAccount_0"));
    QObject *effective = harness.named(QStringLiteral("capabilityEffective_0"));
    QVERIFY(runtime && account && effective);
    QVERIFY(runtime->property("text").toString().contains(QStringLiteral("not checked"),
                                                            Qt::CaseInsensitive));
    QVERIFY(account->property("text").toString().contains(QStringLiteral("not checked"),
                                                            Qt::CaseInsensitive));
    QVERIFY(effective->property("text").toString().contains(QStringLiteral("unavailable"),
                                                              Qt::CaseInsensitive));
}

void PluginSettingsQmlTest::directorySelectionUsesLocalUrlAdapter()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QVERIFY(click(harness.named(QStringLiteral("directoryBrowse_library"))));
    QObject *dialog = harness.named(QStringLiteral("pluginDirectoryDialog"));
    QVERIFY(dialog && dialog->property("visible").toBool());
    const QUrl localFolder(QStringLiteral("file:///tmp/task8c-fixture"));
    QVERIFY(dialog->setProperty("selectedFolder", localFolder));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection));
    QCOMPARE(controller.directoryCalls, 1);
    QCOMPARE(controller.lastDirectoryField, QStringLiteral("library"));
    QCOMPARE(controller.lastDirectoryUrl, localFolder);
    QCOMPARE(harness.named(QStringLiteral("field_library"))->property("text").toString(),
             QStringLiteral("/controller/converted folder"));
}

void PluginSettingsQmlTest::rejectedDirectoryConversionPreservesLocalOverride()
{
    PluginSettingsControllerDouble controller;
    controller.directoryAccepted = false;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QObject *directory = harness.named(QStringLiteral("field_library"));
    QVERIFY(editText(directory, QStringLiteral("/user/local directory draft")));
    QVERIFY(click(harness.named(QStringLiteral("directoryBrowse_library"))));
    QObject *dialog = harness.named(QStringLiteral("pluginDirectoryDialog"));
    QVERIFY(dialog && dialog->property("visible").toBool());
    const QUrl rejectedFolder(QStringLiteral("file:///tmp/rejected-directory"));
    QVERIFY(dialog->setProperty("selectedFolder", rejectedFolder));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection));
    QCOMPARE(controller.directoryCalls, 1);
    QCOMPARE(controller.lastDirectoryUrl, rejectedFolder);
    QCOMPARE(directory->property("text").toString(), QStringLiteral("/user/local directory draft"));
}

void PluginSettingsQmlTest::lifecycleBusyAndNoSchemaStates()
{
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller);
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    QVERIFY(click(harness.named(QStringLiteral("discoverPluginsAction"))));
    QVERIFY(click(harness.named(QStringLiteral("unloadPluginAction"))));
    QVERIFY(click(harness.named(QStringLiteral("reloadPluginAction"))));
    QCOMPARE(controller.discoverCalls, 1);
    QCOMPARE(controller.unloadCalls, 1);
    QCOMPARE(controller.reloadCalls, 1);

    QVERIFY(click(harness.named(QStringLiteral("pluginSelect_fixture.beta"))));
    QObject *noSchema = harness.named(QStringLiteral("noSchemaPlaceholder"));
    QVERIFY(noSchema && noSchema->property("visible").toBool());
    QVERIFY(click(harness.named(QStringLiteral("loadPluginAction"))));
    QCOMPARE(controller.loadCalls, 1);

    controller.setBusy(true);
    QCoreApplication::processEvents();
    QVERIFY(!harness.named(QStringLiteral("discoverPluginsAction"))->property("enabled").toBool());
    QVERIFY(!harness.named(QStringLiteral("loadPluginAction"))->property("enabled").toBool());
    controller.setBusy(false);
    controller.clearPlugins();
    QCoreApplication::processEvents();
    QObject *empty = harness.named(QStringLiteral("emptyPluginPlaceholder"));
    QVERIFY(empty && empty->property("visible").toBool());
    QVERIFY(harness.panel->setProperty("controller", QVariant::fromValue(static_cast<QObject *>(nullptr))));
    QCoreApplication::processEvents();
    QVERIFY(harness.named(QStringLiteral("pluginUnavailablePlaceholder"))->property("visible").toBool());
}

void PluginSettingsQmlTest::longFallbackLabelWraps_data()
{
    QTest::addColumn<int>("width");
    QTest::newRow("desktop") << 1200;
    QTest::newRow("compact") << 480;
}

void PluginSettingsQmlTest::longFallbackLabelWraps()
{
    QFETCH(int, width);
    PluginSettingsControllerDouble controller;
    PanelHarness harness(&controller, QSize(width, 800));
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));
    auto *form = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("schemaSettingsForm")));
    auto *label = qobject_cast<QQuickItem *>(
        harness.named(QStringLiteral("fieldLabel_%1").arg(longFallbackFieldId())));
    QVERIFY(form && label);
    QCOMPARE(label->property("text").toString(), longFallbackFieldId());
    QVERIFY(label->property("wrapMode").toInt() != 0);
    QVERIFY(label->property("lineCount").toInt() >= 2);
    QVERIFY(label->property("paintedWidth").toReal() <= label->width() + 0.5);
    QVERIFY(label->height() > label->property("font").value<QFont>().pixelSize() * 1.5);
    const QRectF formRect = sceneRect(form);
    const QRectF labelRect = sceneRect(label);
    QVERIFY(labelRect.left() >= formRect.left() - 0.5);
    QVERIFY(labelRect.right() <= formRect.right() + 0.5);
}

void PluginSettingsQmlTest::responsiveGeometryAndTheme_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<bool>("dark");
    QTest::newRow("desktop-light") << 1200 << false;
    QTest::newRow("desktop-dark") << 1200 << true;
    QTest::newRow("compact-light") << 480 << false;
    QTest::newRow("compact-dark") << 480 << true;
}

void PluginSettingsQmlTest::responsiveGeometryAndTheme()
{
    QFETCH(int, width);
    QFETCH(bool, dark);
    QSettings settings;
    settings.setValue(QStringLiteral("Style/theme"), dark ? 1 : 0);
    settings.sync();

    PluginSettingsControllerDouble controller;
    controller.setManyPlugins();
    PanelHarness harness(&controller, QSize(width, 800));
    QVERIFY2(harness.panel, qPrintable(harness.component.errorString()));

    auto *header = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("pluginPanelHeader")));
    auto *notice = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("pluginPanelNotice")));
    auto *master = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("pluginMasterList")));
    auto *detail = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("pluginDetailPane")));
    auto *form = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("schemaSettingsForm")));
    auto *formActions = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("formActionArea")));
    auto *settingsActions = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("settingsActionsArea")));
    auto *longLabel = qobject_cast<QQuickItem *>(harness.named(QStringLiteral("fieldLabel_alias")));
    QVERIFY(header && notice && master && detail && form && formActions && settingsActions && longLabel);
    QVERIFY(!harness.named(QStringLiteral("navidromeConfigAction")));

    const QRectF panelRect = sceneRect(harness.panel);
    const QRectF headerRect = sceneRect(header);
    const QRectF noticeRect = sceneRect(notice);
    const QRectF masterRect = sceneRect(master);
    const QRectF detailRect = sceneRect(detail);
    const QRectF formRect = sceneRect(form);
    const QRectF formActionsRect = sceneRect(formActions);
    const QRectF settingsActionsRect = sceneRect(settingsActions);
    const QRectF longLabelRect = sceneRect(longLabel);
    for (const QRectF &rect : {headerRect, noticeRect, masterRect, detailRect, formRect,
                              formActionsRect, settingsActionsRect, longLabelRect}) {
        QVERIFY(rect.width() > 0);
        QVERIFY(rect.height() > 0);
        QVERIFY(rect.left() >= panelRect.left() - 0.5);
        QVERIFY(rect.right() <= panelRect.right() + 0.5);
    }
    QVERIFY(headerRect.bottom() <= noticeRect.top() + 0.5);
    QVERIFY(noticeRect.bottom() <= masterRect.top() + 0.5);
    QVERIFY(noticeRect.bottom() <= detailRect.top() + 0.5);
    if (width >= 760)
        QVERIFY(masterRect.right() <= detailRect.left() + 0.5);
    else
        QVERIFY(masterRect.bottom() <= detailRect.top() + 0.5);
    QVERIFY(formRect.top() >= detailRect.top() - 0.5);
    QVERIFY(formRect.bottom() <= formActionsRect.top() + 0.5);
    QVERIFY(formActionsRect.bottom() <= settingsActionsRect.top() + 0.5);
    QVERIFY(longLabelRect.height() > 0);
    QVERIFY(longLabelRect.width() <= formRect.width() + 0.5);
    QVERIFY(master->property("contentHeight").toReal() > master->height());
    QVERIFY(detail->property("contentHeight").toReal() > detail->height());

    const QColor panelColor = harness.panel->property("surfaceColor").value<QColor>();
    const QColor textColor = harness.panel->property("primaryTextColor").value<QColor>();
    QVERIFY(panelColor.isValid());
    QVERIFY(textColor.isValid());
    QVERIFY(panelColor != QColor(Qt::white) || textColor != QColor(Qt::black));
    QCOMPARE(harness.panel->property("darkTheme").toBool(), dark);
    const QColor controlColor = harness.panel->property("controlColor").value<QColor>();
    QVERIFY(controlColor.isValid());
    QVERIFY(controlColor != panelColor || textColor != controlColor);
}

QTEST_MAIN(PluginSettingsQmlTest)
#include "tst_PluginSettingsQml.moc"
