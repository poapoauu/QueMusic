#pragma once
#include <QObject>
#include <QUuid>
#include <QUrl>
#include <QVariantMap>
#include <memory>
class PluginManager;
class SourceRegistry;
class SourceAccountStore;

class PluginSettingsController final : public QObject {
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
    PluginSettingsController(PluginManager *, SourceRegistry *, SourceAccountStore *, QObject *parent = nullptr);
    ~PluginSettingsController() override;
    QVariantList plugins() const;
    QVariantList instances() const;
    QVariantList settingsSections() const;
    QVariantList settingsActions() const;
    QVariantList sourceCapabilities() const;
    QVariantMap selectedPlugin() const;
    QString selectedPluginId() const;
    QString selectedInstanceId() const;
    QString lastErrorKey() const;
    bool busy() const;
    Q_INVOKABLE bool selectPlugin(const QString &packageId);
    Q_INVOKABLE bool selectInstance(const QString &instanceId);
    Q_INVOKABLE bool setDraftValues(const QVariantMap &publicDraft);
    Q_INVOKABLE bool setDirectoryField(const QString &fieldId, const QUrl &localFolder);
    Q_INVOKABLE bool saveInstance(const QString &displayName, const QVariantMap &secretDraft);
    Q_INVOKABLE bool removeInstance(const QString &instanceId);
    Q_INVOKABLE bool setInstanceEnabled(const QString &instanceId, bool enabled);
    Q_INVOKABLE QUuid testConnection(const QVariantMap &secretDraft);
    Q_INVOKABLE QUuid runSettingsAction(const QString &actionId, const QVariantMap &secretDraft, bool confirmed = false);
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void discoverPlugins();
    Q_INVOKABLE bool loadPlugin(const QString &packageId);
    Q_INVOKABLE bool unloadPlugin(const QString &packageId);
    Q_INVOKABLE bool reloadPlugin(const QString &packageId);
signals:
    void snapshotsChanged();
    void connectionTestFinished(QUuid requestId, QVariantMap result);
    void settingsActionFinished(QUuid requestId, QVariantMap result);
    void draftReset();
private:
    struct State;
    std::shared_ptr<State> d;
};
