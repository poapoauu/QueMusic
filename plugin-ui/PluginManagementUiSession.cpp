#include "PluginManagementUiSession.h"

#include "HostPluginUiSettingsBridge.h"
#include "PluginManifest.h"
#include "SourceAccountStore.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QTimer>

PluginManagementUiSession::PluginManagementUiSession(PluginManager *manager,
    SourceAccountStore *store, SourceRegistry *registry, const PluginUiContextData &identity,
    const SettingsSchemaV2 &schema, QObject *parent)
    : QObject(parent)
{
    if (!manager || !store || identity.pluginPackageId.isEmpty()) {
        fail(QStringLiteral("plugin.ui.invalidIdentity"));
        return;
    }
    const PluginSpec spec = manager->plugin(identity.pluginPackageId);
    if (spec.state != PluginState::Loaded || spec.sourceId != identity.sourceId) {
        fail(QStringLiteral("plugin.ui.unavailable"));
        return;
    }
    m_lease = manager->acquire(identity.pluginPackageId);
    if (!m_lease.isValid()) {
        fail(QStringLiteral("plugin.ui.unavailable"));
        return;
    }
    const PluginManifest manifest = PluginManifest::fromFile(QDir(spec.path).filePath("manifest.json"));
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager->pluginInstance(identity.pluginPackageId));
    if (!manifest.isValid() || !manifest.hasManagementUi() || !provider
        || manifest.id() != identity.pluginPackageId || manifest.sourceId() != identity.sourceId) {
        fail(QStringLiteral("plugin.ui.invalidDescriptor"));
        return;
    }
    const auto descriptor = provider->managementUi();
    if (descriptor.uiApiVersion != QStringLiteral(QUEMUSIC_PLUGIN_UI_API_V1)
        || descriptor.uiApiVersion != manifest.pluginUiApiVersion()
        || descriptor.componentUrl.toString() != manifest.managementUiRelativePath()
        || !descriptor.componentUrl.isRelative() || descriptor.componentUrl.hasQuery()
        || descriptor.componentUrl.hasFragment()) {
        fail(QStringLiteral("plugin.ui.invalidDescriptor"));
        return;
    }
    if ((identity.mode == PluginUiMode::Create && !descriptor.supportsCreate)
        || (identity.mode == PluginUiMode::Edit && !descriptor.supportsEdit)
        || (identity.mode != PluginUiMode::Create && identity.mode != PluginUiMode::Edit)) {
        fail(QStringLiteral("plugin.ui.unsupportedMode"));
        return;
    }
    const QString qmlRoot = QFileInfo(QDir(spec.path).filePath("qml")).canonicalFilePath();
    const QString page = QFileInfo(QDir(spec.path).filePath(manifest.managementUiRelativePath())).canonicalFilePath();
    if (qmlRoot.isEmpty() || page.isEmpty() || !page.startsWith(qmlRoot + QDir::separator())
        || !page.endsWith(QStringLiteral(".qml"))) {
        fail(QStringLiteral("plugin.ui.invalidComponent"));
        return;
    }
    PluginUiContextData data = identity;
    SourceAccountSaveV2 save;
    save.pluginPackageId = data.pluginPackageId;
    save.sourceId = data.sourceId;
    save.schema = schema;
    if (data.mode == PluginUiMode::Create) {
        if (!data.accountId.isEmpty() || !data.sourceInstanceId.isEmpty()) {
            fail(QStringLiteral("plugin.ui.invalidIdentity"));
            return;
        }
        data.accountId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        data.sourceInstanceId = data.sourceId + '/' + data.accountId;
        save.accountId = data.accountId;
        save.configurationVersion = 1;
        save.enabled = true;
    } else {
        if (data.accountId.isEmpty() || data.sourceInstanceId != data.sourceId + '/' + data.accountId) {
            fail(QStringLiteral("plugin.ui.invalidIdentity"));
            return;
        }
        const auto account = store->storedAccount(data.sourceId, data.accountId);
        if (!account || account->recordVersion != 2 || account->pluginPackageId != data.pluginPackageId
            || !account->enabled) {
            fail(QStringLiteral("plugin.ui.invalidIdentity"));
            return;
        }
        save.accountId = data.accountId;
        save.displayName = account->displayName;
        save.enabled = account->enabled;
        save.configurationVersion = qMax(1, account->configurationVersion);
    }
    m_settings = new HostPluginUiSettingsBridge(store, registry, save, this);
    m_host = new HostPluginUiHostServices(this);
    m_context = new PluginUiContext(this);
    m_context->setContext(data, nullptr, m_settings, nullptr, m_host);
    m_backend = provider->createManagementBackend(data, m_context);
    if (m_backend && m_backend->parent() != m_context) {
        // An external owner may still hold plugin code. Never unload its library
        // or delete an object that the Provider did not transfer to this session.
        m_lease.pinLoadedPackage();
        m_backend = nullptr;
        fail(QStringLiteral("plugin.ui.invalidBackendOwner"));
        return;
    }
    if (!m_backend) {
        fail(QStringLiteral("plugin.ui.backendUnavailable"));
        return;
    }
    m_context->setContext(data, m_backend, m_settings, nullptr, m_host);
    m_componentUrl = QUrl::fromLocalFile(page);
    m_state = QStringLiteral("ready");
    emit changed();
}

PluginManagementUiSession::~PluginManagementUiSession()
{
    release();
}

void PluginManagementUiSession::trackPage(QObject *page)
{
    if (!page || m_page || m_released) return;
    m_page = page;
    connect(page, &QObject::destroyed, this, [this] {
        // QObject::destroyed precedes child destruction. Wait for the entire
        // Loader disposal stack to unwind before destroying backend/plugin code.
        m_pageTeardownPending = true;
        QTimer::singleShot(0, this, [this] {
            m_pageTeardownPending = false;
            emit pageDestroyed();
        });
    });
}

void PluginManagementUiSession::fail(const QString &key)
{
    m_errorKey = key;
    m_state = QStringLiteral("error");
    release();
    emit changed();
}

void PluginManagementUiSession::invalidateContext()
{
    if (m_context && m_context->isValid()) m_context->invalidate();
    if (m_settings) m_settings->invalidate();
    if (m_host) m_host->invalidate();
}

void PluginManagementUiSession::release()
{
    if (m_released) return;
    m_released = true;
    invalidateContext();
    delete m_context;
    delete m_backend;
    delete m_settings;
    delete m_host;
    m_lease = {};
    if (m_state == QStringLiteral("ready")) m_state = QStringLiteral("closed");
    emit changed();
}
