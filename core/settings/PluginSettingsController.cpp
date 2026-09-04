#include "PluginSettingsController.h"
#include "PluginSettingsOperation.h"
#include "SettingsSchemaPresentation.h"
#include "SourceSettingsValidation.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QTimer>
#include <utility>

namespace {
bool isSecret(const SettingsFieldV2 &f) { return f.secret || f.type == SettingsFieldTypeV2::Secret; }
QString stateName(PluginState state)
{
    switch (state) {
    case PluginState::Loaded: return QStringLiteral("loaded");
    case PluginState::Failed: return QStringLiteral("failed");
    case PluginState::Unloaded: return QStringLiteral("unloaded");
    default: return QStringLiteral("discovered");
    }
}
}

struct PluginSettingsController::State : std::enable_shared_from_this<State> {
    QPointer<PluginSettingsController> owner;
    QPointer<PluginManager> manager;
    QPointer<SourceRegistry> registry;
    SourceAccountStore *store = nullptr; // Injected service must outlive the controller.
    struct Cache {
        QPointer<QObject> object;
        SourceDescriptorV2 descriptor;
        SettingsSchemaV2 schema;
        bool available = false;
        QString reason;
    };
    QHash<QString, Cache> caches;
    QString pluginId, instanceId, accountId, error;
    QVariantMap draft, snapshot;
    QVariantList probeActions, probeMusic;
    bool probed = false, refreshing = false, queued = false, refreshAgain = false;
    bool selectedWasLoaded = false;
    QPointer<QObject> selectedObject;
    quint64 generation = 0;
    std::shared_ptr<PluginSettingsOperation> operation;

    void invalidate()
    {
        ++generation;
        auto old = std::exchange(operation, {});
        if (old) old->cancel();
        probed = false; probeActions.clear(); probeMusic.clear();
    }
    void freshDraft()
    {
        instanceId.clear(); draft.clear();
        accountId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    void queueRefresh()
    {
        if (!owner) return;
        if (refreshing) { refreshAgain = true; return; }
        if (queued) return;
        queued = true;
        auto weak = weak_from_this();
        QTimer::singleShot(0, owner, [weak] {
            if (auto s = weak.lock()) { s->queued = false; s->refresh(); }
        });
    }
    Cache cacheFor(const PluginSpec &spec)
    {
        if (!manager || spec.state != PluginState::Loaded) { caches.remove(spec.id); return {}; }
        QPointer<QObject> object = manager->pluginInstance(spec.id);
        if (!object) { caches.remove(spec.id); return {}; }
        auto cached = caches.value(spec.id);
        if (cached.object == object) return cached;
        Cache next;
        next.object = object;
        next.reason = QStringLiteral("source.settings.unsupported");
        auto lease = manager->acquire(spec.id);
        if (!owner || !manager || !object || !lease.isValid()) return {};
        // Every provider-owned temporary is destroyed before releasing this lease.
        if (auto *music = qobject_cast<IMusicSourcePluginV2 *>(object.data())) {
            next.descriptor = detachSettingsDescriptorV2(music->descriptor());
            if (!owner || !manager || !object) return {};
            if (next.descriptor.sourceId == spec.sourceId
                && (next.descriptor.pluginPackageId.isEmpty() || next.descriptor.pluginPackageId == spec.id)) {
                if (auto *settings = qobject_cast<IPluginSettingsProviderV2 *>(object.data())) {
                    const auto detached = detachSettingsSchemaV2(settings->settingsSchema());
                    if (detached) { next.schema = *detached; next.available = true; next.reason.clear(); }
                    else next.reason = QStringLiteral("source.settings.invalidSchema");
                }
            } else next.reason = QStringLiteral("source.settings.invalidIdentity");
        }
        caches.insert(spec.id, next); // Cache before release notifications can re-enter.
        return next;
    }
    std::optional<StoredSourceAccount> ownedAccount(const QString &id) const
    {
        if (!manager || !store || pluginId.isEmpty()) return {};
        const auto separator = id.indexOf('/');
        if (separator <= 0 || separator == id.size() - 1) return {};
        const auto source = manager->plugin(pluginId).sourceId;
        if (source.isEmpty() || source != id.left(separator)) return {};
        auto account = store->storedAccount(source, id.mid(separator + 1));
        if (!account || (!account->pluginPackageId.isEmpty() && account->pluginPackageId != pluginId)) return {};
        return account;
    }
    QStringList configured(const StoredSourceAccount &account, const SettingsSchemaV2 &schema) const
    {
        QStringList secrets, result;
        for (const auto &s : schema) for (const auto &f : s.fields) if (isSecret(f)) secrets.append(f.id);
        for (const auto &id : account.configuredSecretFieldIds) if (secrets.contains(id)) result.append(id);
        if (result.isEmpty() && !account.secretReference.isEmpty()
            && (account.recordVersion == 1 || account.secretFormat == "legacyRaw") && secrets.size() == 1)
            result = secrets;
        result.sort();
        return result;
    }
    void refresh()
    {
        auto keep = shared_from_this();
        if (!owner) return;
        if (refreshing) { refreshAgain = true; return; }
        refreshing = true;
        QVariantList plugins, instances, sections, actions, music;
        QVariantMap selected;
        Cache current;
        const auto rawPlugins = manager ? manager->plugins() : QVariantList();
        for (const auto &raw : rawPlugins) {
            if (!owner || !manager) break;
            const auto id = raw.toMap().value("id").toString();
            auto spec = manager->plugin(id);
            auto cache = cacheFor(spec);
            if (!owner || !manager) break;
            spec = manager->plugin(id); // Lease count may have changed while inspecting.
            const bool loaded = spec.state == PluginState::Loaded;
            const bool busy = spec.activeLeases > 0 || !spec.busyReason.isEmpty();
            const QString reason = busy ? QStringLiteral("source.settings.pluginBusy")
                : spec.state == PluginState::Failed ? QStringLiteral("source.settings.pluginFailed")
                : loaded ? cache.reason : QStringLiteral("source.settings.pluginNotLoaded");
            QVariantMap row{{"id", spec.id}, {"sourceId", spec.sourceId}, {"name", spec.name}, {"version", spec.version},
                {"state", stateName(spec.state)}, {"activeLeases", spec.activeLeases},
                {"loadable", !loaded && !busy}, {"unloadable", loaded && !busy}, {"reloadable", loaded && !busy},
                {"settingsAvailable", loaded && cache.available}, {"reasonKey", reason}};
            plugins.append(row);
            if (id == pluginId) { selected = row; current = cache; }
        }
        if (!owner) { refreshing = false; return; }
        auto object = manager ? QPointer<QObject>(manager->pluginInstance(pluginId)) : QPointer<QObject>();
        if (selectedWasLoaded && (!object || object != selectedObject)) {
            invalidate(); freshDraft();
            selectedWasLoaded = false;
            emit owner->draftReset();
            if (!owner) { refreshing = false; return; }
        }
        selectedObject = object;
        selectedWasLoaded = bool(object);
        if (!pluginId.isEmpty() && selected.isEmpty()) {
            invalidate(); pluginId.clear(); freshDraft();
            emit owner->draftReset();
            if (!owner) { refreshing = false; return; }
        }
        auto previous = instanceId.isEmpty() ? std::optional<StoredSourceAccount>() : ownedAccount(instanceId);
        if (!instanceId.isEmpty() && !previous) {
            invalidate(); freshDraft();
            emit owner->draftReset();
            if (!owner) { refreshing = false; return; }
        }
        const auto descriptors = registry ? registry->enabledInstances() : QList<SourceInstanceDescriptorV2>();
        if (store && !pluginId.isEmpty()) {
            for (const auto &account : store->accounts()) {
                const auto id = sourceInstanceId(account.sourceId, account.accountId);
                if (!ownedAccount(id)) continue;
                SourceSessionStateV2 state = SourceSessionStateV2::Closed;
                for (const auto &d : descriptors) if (d.sourceInstanceId == id) state = d.state;
                if (!object) state = SourceSessionStateV2::Failed;
                instances.append(QVariantMap{{"sourceInstanceId", id}, {"accountId", account.accountId},
                    {"displayName", account.displayName}, {"enabled", account.enabled}, {"state", int(state)},
                    {"credentialConfigured", !account.secretReference.isEmpty()},
                    {"configuredSecretFieldIds", configured(account, current.schema)}});
            }
        }
        if (current.available && object) {
            sections = settingsSectionsPresentationV2(current.schema, draft,
                previous ? previous->parameters : QVariantMap(), previous ? configured(*previous, current.schema) : QStringList());
            actions = probed ? probeActions : settingsActionsPresentationV2(current.schema);
        }
        if (object && !current.descriptor.sourceId.isEmpty())
            music = probed ? probeMusic : sourceCapabilitiesPresentationV2(current.descriptor.declaredActions);
        QVariantMap next{{"plugins", plugins}, {"instances", instances}, {"settingsSections", sections},
            {"settingsActions", actions}, {"sourceCapabilities", music}, {"selectedPlugin", selected},
            {"selectedPluginId", pluginId}, {"selectedInstanceId", instanceId}, {"lastErrorKey", error},
            {"busy", bool(operation)}};
        const bool changed = snapshot != next;
        snapshot = next;
        refreshing = false;
        if (std::exchange(refreshAgain, false)) queueRefresh();
        if (changed && owner) emit owner->snapshotsChanged();
    }
    bool fail(const QString &key)
    {
        error = key; refresh(); return false;
    }
    bool usable() const
    {
        const auto cache = caches.value(pluginId);
        return owner && manager && registry && store && cache.available && cache.object
            && manager->pluginInstance(pluginId) == cache.object;
    }
    std::optional<SourceAccountSaveV2> request(const QString &name, const QVariantMap &secrets)
    {
        if (!usable()) { fail(QStringLiteral("source.settings.unavailable")); return {}; }
        const auto cache = caches.value(pluginId);
        QStringList secretIds;
        for (const auto &s : cache.schema) for (const auto &f : s.fields) if (isSecret(f)) secretIds.append(f.id);
        for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
            if (!secretIds.contains(it.key()) || it.value().metaType().id() != QMetaType::QString) {
                fail(QStringLiteral("source.settings.invalidSecretDraft")); return {};
            }
        }
        auto previous = instanceId.isEmpty() ? std::optional<StoredSourceAccount>() : ownedAccount(instanceId);
        if (!instanceId.isEmpty() && !previous) { fail(QStringLiteral("source.settings.invalidIdentity")); return {}; }
        QVariantMap values = draft;
        for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) values.insert(it.key(), it.value());
        return SourceAccountSaveV2{pluginId, cache.descriptor.sourceId, accountId, name,
            previous ? previous->enabled : true, previous ? qMax(1, previous->configurationVersion) : 1,
            cache.schema, values};
    }
    QUuid launch(const QString &action, const QVariantMap &secrets, bool confirmed)
    {
        auto keep = shared_from_this();
        invalidate();
        const auto previous = instanceId.isEmpty() ? std::optional<StoredSourceAccount>() : ownedAccount(instanceId);
        const auto r = request(previous ? previous->displayName : QString(), secrets);
        if (!r) return {};
        QString preparationError;
        const auto config = store->configurationForDraftV2(*r, &preparationError);
        if (!config) { fail(preparationError); return {}; }
        if (!owner || !usable()) return {};
        auto op = std::make_shared<PluginSettingsOperation>(manager, *config, caches.value(pluginId).schema, action, confirmed);
        const auto id = op->requestId();
        operation = op;
        const auto token = generation;
        auto weak = weak_from_this();
        QObject::connect(op.get(), &PluginSettingsOperation::finished, owner,
            [weak, token, action](QUuid id, const QVariantMap &result, const QVariantList &actions, const QVariantList &music) {
                auto s = weak.lock();
                if (!s || !s->owner || s->generation != token || !s->operation || s->operation->requestId() != id) return;
                s->operation.reset();
                if (action.isEmpty() && result.value("success").toBool()) {
                    s->probed = true; s->probeActions = actions; s->probeMusic = music;
                }
                s->error = result.value("reasonKey").toString();
                s->refresh();
                if (!s->owner || s->generation != token) return;
                if (action.isEmpty()) emit s->owner->connectionTestFinished(id, result);
                else emit s->owner->settingsActionFinished(id, result);
            });
        error.clear();
        refresh();
        // Schedule only after synchronous public notifications have unwound.
        if (owner && generation == token && operation == op) op->start();
        return id;
    }
};

PluginSettingsController::PluginSettingsController(PluginManager *manager, SourceRegistry *registry,
    SourceAccountStore *store, QObject *parent) : QObject(parent), d(std::make_shared<State>())
{
    d->owner = this; d->manager = manager; d->registry = registry; d->store = store;
    d->freshDraft();
    const auto weak = std::weak_ptr<State>(d);
    if (manager) {
        connect(manager, &PluginManager::pluginsChanged, this, [weak] { if (auto s = weak.lock()) s->queueRefresh(); });
        connect(manager, &QObject::destroyed, this, [weak] { if (auto s = weak.lock()) { s->invalidate(); s->refresh(); } });
    }
    if (registry) {
        connect(registry, &SourceRegistry::instanceChanged, this, [weak](const QString &id) {
            if (auto s = weak.lock()) {
                if (s->instanceId == id) s->invalidate();
                s->refresh();
            }
        });
        connect(registry, &QObject::destroyed, this, [weak] { if (auto s = weak.lock()) { s->invalidate(); s->refresh(); } });
    }
    d->refresh();
}
PluginSettingsController::~PluginSettingsController() { d->owner = nullptr; d->invalidate(); }
QVariantList PluginSettingsController::plugins() const { return d->snapshot.value("plugins").toList(); }
QVariantList PluginSettingsController::instances() const { return d->snapshot.value("instances").toList(); }
QVariantList PluginSettingsController::settingsSections() const { return d->snapshot.value("settingsSections").toList(); }
QVariantList PluginSettingsController::settingsActions() const { return d->snapshot.value("settingsActions").toList(); }
QVariantList PluginSettingsController::sourceCapabilities() const { return d->snapshot.value("sourceCapabilities").toList(); }
QVariantMap PluginSettingsController::selectedPlugin() const { return d->snapshot.value("selectedPlugin").toMap(); }
QString PluginSettingsController::selectedPluginId() const { return d->pluginId; }
QString PluginSettingsController::selectedInstanceId() const { return d->instanceId; }
QString PluginSettingsController::lastErrorKey() const { return d->error; }
bool PluginSettingsController::busy() const { return bool(d->operation); }

bool PluginSettingsController::selectPlugin(const QString &id)
{
    auto s = d;
    if (!s->manager || s->manager->plugin(id).id != id || id.isEmpty()) return s->fail(QStringLiteral("source.settings.unknownPlugin"));
    s->invalidate(); s->pluginId = id; s->freshDraft(); s->error.clear();
    s->selectedWasLoaded = false; s->selectedObject = nullptr;
    emit draftReset();
    if (s->owner) s->refresh();
    return true;
}
bool PluginSettingsController::selectInstance(const QString &id)
{
    auto s = d;
    if (s->pluginId.isEmpty() || (!id.isEmpty() && !s->ownedAccount(id))) return s->fail(QStringLiteral("source.settings.invalidIdentity"));
    s->invalidate(); s->freshDraft();
    if (!id.isEmpty()) { s->instanceId = id; s->accountId = s->ownedAccount(id)->accountId; }
    s->error.clear(); emit draftReset();
    if (s->owner) s->refresh();
    return true;
}
bool PluginSettingsController::setDraftValues(const QVariantMap &values)
{
    auto s = d;
    if (!s->usable()) return s->fail(QStringLiteral("source.settings.unavailable"));
    const auto schema = s->caches.value(s->pluginId).schema;
    for (const auto &section : schema) for (const auto &f : section.fields)
        if (isSecret(f) && values.contains(f.id)) return s->fail(QStringLiteral("source.settings.secretInPublicDraft"));
    const auto error = validateSourceSettingsDraftV2(schema, values);
    if (!error.isEmpty()) return s->fail(error);
    if (s->draft != values) { s->invalidate(); s->draft = values; }
    s->error.clear(); s->refresh(); return true;
}
bool PluginSettingsController::setDirectoryField(const QString &id, const QUrl &folder)
{
    auto s = d;
    if (!s->usable() || !folder.isLocalFile() || folder.toLocalFile().isEmpty()
        || (!folder.host().isEmpty() && folder.host() != "localhost"))
        return s->fail(QStringLiteral("source.settings.invalidDirectory"));
    for (const auto &section : s->caches.value(s->pluginId).schema) for (const auto &f : section.fields) {
        if (f.id == id && !isSecret(f) && f.type == SettingsFieldTypeV2::Directory) {
            auto values = s->draft; values.insert(id, folder.toLocalFile());
            return setDraftValues(values);
        }
    }
    return s->fail(QStringLiteral("source.settings.invalidDirectory"));
}
bool PluginSettingsController::saveInstance(const QString &name, const QVariantMap &secrets)
{
    auto s = d;
    const auto r = s->request(name, secrets);
    if (!r) return false;
    QString error;
    if (!s->store->saveValidatedV2(*r, &error)) return s->fail(error);
    s->invalidate();
    s->instanceId = sourceInstanceId(r->sourceId, r->accountId);
    s->draft.clear(); s->error.clear();
    if (s->registry) s->registry->configurationChanged(s->instanceId);
    if (s->owner) emit s->owner->draftReset();
    if (s->owner) s->refresh();
    return true;
}
bool PluginSettingsController::removeInstance(const QString &id)
{
    auto s = d; const auto account = s->ownedAccount(id);
    if (!account) return s->fail(QStringLiteral("source.settings.invalidIdentity"));
    QString error;
    if (!s->store->remove(account->sourceId, account->accountId, &error))
        return s->fail(QStringLiteral("source.settings.removeFailed"));
    s->invalidate();
    if (s->instanceId == id) { s->freshDraft(); if (s->owner) emit s->owner->draftReset(); }
    s->error.clear();
    if (s->registry) s->registry->configurationChanged(id);
    if (s->owner) s->refresh();
    return true;
}
bool PluginSettingsController::setInstanceEnabled(const QString &id, bool enabled)
{
    auto s = d;
    if (!s->registry || !s->ownedAccount(id)) return s->fail(QStringLiteral("source.settings.invalidIdentity"));
    const bool success = enabled ? s->registry->enableInstance(id) : s->registry->disableInstance(id);
    if (!success) return s->fail(QStringLiteral("source.settings.updateFailed"));
    s->error.clear(); if (s->owner) s->refresh(); return true;
}
QUuid PluginSettingsController::testConnection(const QVariantMap &secrets) { auto s = d; return s->launch({}, secrets, false); }
QUuid PluginSettingsController::runSettingsAction(const QString &action, const QVariantMap &secrets, bool confirmed)
{
    auto s = d;
    if (action.isEmpty()) { s->fail(QStringLiteral("source.settings.unsupported")); return {}; }
    return s->launch(action, secrets, confirmed);
}
void PluginSettingsController::cancelOperation() { auto s = d; s->invalidate(); s->refresh(); }
void PluginSettingsController::discoverPlugins() { auto s = d; if (s->manager) s->manager->discover(); if (s->owner) s->refresh(); }
bool PluginSettingsController::loadPlugin(const QString &id)
{
    auto s = d; const bool success = s->manager && s->manager->load(id);
    if (!success) return s->fail(QStringLiteral("source.settings.pluginLoadFailed"));
    s->error.clear(); if (s->owner) s->refresh(); return true;
}
bool PluginSettingsController::unloadPlugin(const QString &id)
{
    auto s = d;
    const auto result = s->manager ? s->manager->unload(id) : PluginOperationResult::NotFound;
    if (result != PluginOperationResult::Success) return s->fail(result == PluginOperationResult::Busy
        ? QStringLiteral("source.settings.pluginBusy") : QStringLiteral("source.settings.pluginUnloadFailed"));
    s->error.clear(); if (s->owner) s->refresh(); return true;
}
bool PluginSettingsController::reloadPlugin(const QString &id)
{
    auto s = d;
    const auto result = s->manager ? s->manager->reload(id) : PluginOperationResult::NotFound;
    if (result != PluginOperationResult::Success) return s->fail(result == PluginOperationResult::Busy
        ? QStringLiteral("source.settings.pluginBusy") : QStringLiteral("source.settings.pluginReloadFailed"));
    s->error.clear(); if (s->owner) s->refresh(); return true;
}
