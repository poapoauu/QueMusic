#pragma once

#include "PluginManifest.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <vector>

enum class PluginState {
    Discovered,
    Loaded,
    Failed,
    Unloaded,
};

enum class PluginOperationResult {
    Success,
    NotFound,
    Busy,
    Failed,
};

struct PluginSpec {
    QString id;
    QString sourceId;
    QString name;
    QString version;
    PluginCategory category = PluginCategory::Unknown;
    PluginState state = PluginState::Discovered;
    QString error;
    QString busyReason;
    QString path;
    int activeLeases = 0;
};

class PluginLease {
public:
    PluginLease() = default;

    bool isValid() const;

private:
    struct State;

    explicit PluginLease(std::shared_ptr<State> state);

    std::shared_ptr<State> m_state;

    friend class PluginManager;
};

class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    void addSearchPath(const QString &path);
    int discover();

    bool load(const QString &packageId);
    bool failLoadedPlugin(const QString &packageId, const QString &error);
    PluginOperationResult unload(const QString &packageId);
    PluginOperationResult reload(const QString &packageId);
    PluginLease acquire(const QString &packageId);

    PluginSpec plugin(const QString &packageId) const;
    QVariantList plugins() const;
    QObject *pluginInstance(const QString &packageId) const;

    Q_INVOKABLE void discoverPlugins();
    Q_INVOKABLE bool loadPlugin(const QString &packageId);
    Q_INVOKABLE bool unloadPlugin(const QString &packageId);
    Q_INVOKABLE bool reloadPlugin(const QString &packageId);

signals:
    void pluginChanged(QString packageId);
    void pluginsChanged();
    void pluginLoadFailed(QString packageId, QString error);

private:
    struct Entry;

    Entry *findEntry(const QString &packageId);
    const Entry *findEntry(const QString &packageId) const;
    bool supportsRuntime(const PluginManifest &manifest, QString *error) const;
    void fail(Entry &entry, const QString &error);
    void releaseLease(const QString &packageId);

    QStringList m_searchPaths;
    std::vector<std::unique_ptr<Entry>> m_entries;
};
