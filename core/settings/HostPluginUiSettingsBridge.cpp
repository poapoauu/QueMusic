#include "HostPluginUiSettingsBridge.h"

#include "SettingsSchemaPresentation.h"
#include "SourceRegistry.h"
#include "SourceSettingsValidation.h"

#include <QSet>
#include <QRegularExpression>
#include <QTimer>

namespace {
bool secretField(const SettingsFieldV2 &field)
{ return field.secret || field.type == SettingsFieldTypeV2::Secret; }
}

HostPluginUiSettingsBridge::HostPluginUiSettingsBridge(SourceAccountStore *store,
    SourceRegistry *registry, const SourceAccountSaveV2 &request, QObject *parent)
    : PluginUiSettingsBridge(parent), m_store(store), m_registry(registry), m_request(request)
{
    const auto detached = detachSettingsSchemaV2(request.schema);
    if (!detached) m_valid = false;
    else m_request.schema = *detached;
    m_request.draft.clear();
}

bool HostPluginUiSettingsBridge::validIdentity() const
{
    if (!m_valid || !m_store) return false;
    const auto existing = m_store->storedAccount(m_request.sourceId, m_request.accountId);
    return !existing || (existing->recordVersion == 2
        && existing->pluginPackageId == m_request.pluginPackageId);
}

bool HostPluginUiSettingsBridge::isSecretField(const QString &id) const
{
    for (const auto &section : m_request.schema)
        for (const auto &field : section.fields)
            if (field.id == id) return secretField(field);
    return false;
}

bool HostPluginUiSettingsBridge::isPublicField(const QString &id) const
{
    for (const auto &section : m_request.schema)
        for (const auto &field : section.fields)
            if (field.id == id) return !secretField(field);
    return false;
}

QVariantMap HostPluginUiSettingsBridge::publicValues() const
{
    if (!validIdentity()) return {};
    const auto existing = m_store->storedAccount(m_request.sourceId, m_request.accountId);
    return sourceSettingsPublicValuesV2(m_request.schema, {}, existing ? existing->parameters : QVariantMap());
}

QVariantList HostPluginUiSettingsBridge::sections() const
{
    if (!validIdentity()) return {};
    const auto existing = m_store->storedAccount(m_request.sourceId, m_request.accountId);
    return settingsSectionsPresentationV2(m_request.schema, {},
        existing ? existing->parameters : QVariantMap(),
        existing ? existing->configuredSecretFieldIds : QStringList());
}

bool HostPluginUiSettingsBridge::secretConfigured(const QString &fieldId) const
{
    if (!validIdentity() || !isSecretField(fieldId)) return false;
    const auto existing = m_store->storedAccount(m_request.sourceId, m_request.accountId);
    return existing && existing->configuredSecretFieldIds.contains(fieldId);
}

QUuid HostPluginUiSettingsBridge::finish(bool success, const QString &reasonKey)
{
    const QUuid id = QUuid::createUuid();
    QTimer::singleShot(0, this, [this, id, success, reasonKey] {
        if (m_valid) emit operationFinished(id, success, reasonKey);
    });
    return id;
}

void HostPluginUiSettingsBridge::notifyChange()
{
    if (m_registry) m_registry->configurationChanged(m_request.sourceId + '/' + m_request.accountId);
    emit changed();
}

QUuid HostPluginUiSettingsBridge::savePublicValues(const QVariantMap &values)
{
    if (!validIdentity()) return finish(false, QStringLiteral("source.settings.identityConflict"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (!isPublicField(it.key()))
            return finish(false, QStringLiteral("source.settings.invalidDraft"));
    auto request = m_request;
    request.draft = values;
    QString error;
    const bool success = m_store->saveValidatedV2(request, &error);
    if (success) notifyChange();
    return finish(success, success ? QString() : error);
}

QUuid HostPluginUiSettingsBridge::saveSecret(const QString &fieldId, const QString &value)
{
    if (!validIdentity()) return finish(false, QStringLiteral("source.settings.identityConflict"));
    if (!isSecretField(fieldId) || value.isEmpty())
        return finish(false, QStringLiteral("source.settings.invalidSecretDraft"));
    auto request = m_request;
    request.draft.insert(fieldId, value);
    QString error;
    const bool success = m_store->saveValidatedV2(request, &error);
    if (success) notifyChange();
    return finish(success, success ? QString() : error);
}

QUuid HostPluginUiSettingsBridge::clearSecret(const QString &fieldId)
{
    if (!validIdentity()) return finish(false, QStringLiteral("source.settings.identityConflict"));
    if (!isSecretField(fieldId))
        return finish(false, QStringLiteral("source.settings.invalidSecretDraft"));
    QString error;
    const bool success = m_store->clearNamedSecretV2(m_request, fieldId, &error);
    if (success) notifyChange();
    return finish(success, success ? QString() : error);
}

void HostPluginUiSettingsBridge::invalidate()
{
    m_valid = false;
}

QUuid HostPluginUiHostServices::requestDirectory()
{
    if (!m_valid) return {};
    const QUuid id = QUuid::createUuid();
    m_pending.insert(id);
    emit directoryRequested(id);
    return id;
}

void HostPluginUiHostServices::completeDirectory(QUuid requestId, const QUrl &localDirectory)
{
    if (!m_valid || !m_pending.remove(requestId) || !localDirectory.isLocalFile()) return;
    emit directorySelected(requestId, localDirectory);
}

void HostPluginUiHostServices::notify(const QString &message, bool isError)
{
    static const QRegularExpression keyPattern(QStringLiteral("^(plugin\\.ui|source\\.settings)\\.[a-z0-9_.-]{1,112}$"));
    if (m_valid && keyPattern.match(message).hasMatch())
        emit notificationRequested(message, isError);
}

void HostPluginUiHostServices::invalidate()
{
    m_valid = false;
    m_pending.clear();
}
