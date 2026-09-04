#include "SourceAccountStore.h"
#include "SourceSettingsValidation.h"

#include <QMetaType>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace {

constexpr int kRecordVersion = 1;
const QString kSourcesGroup = QStringLiteral("sources");
const QString kEncodedSourcesGroup = QStringLiteral("sourceAccountsV2");

void setUnavailableError(QString *error)
{
    if (error) {
        *error = QStringLiteral("Secure secret storage is unavailable on this platform");
    }
}

bool hasAccountIdentity(const QString &sourceId, const QString &accountId, QString *error)
{
    if (!sourceId.isEmpty() && !accountId.isEmpty()) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Source ID and account ID are required");
    }
    return false;
}

bool sanitizedParameters(const QVariantMap &parameters, QVariantMap *sanitized, QString *error)
{
    static const QStringList trustedNames{
        QStringLiteral("serverUrl"), QStringLiteral("username")};
    for (auto parameter = parameters.cbegin(); parameter != parameters.cend(); ++parameter) {
        if (!trustedNames.contains(parameter.key())
            || parameter.value().metaType().id() != QMetaType::QString) {
            if (error) {
                *error = QStringLiteral("Only trusted non-sensitive account metadata may be persisted; "
                                        "use SourceAccount::secret for credentials");
            }
            return false;
        }
        sanitized->insert(parameter.key(), parameter.value().toString());
    }
    return true;
}

QString freshSecretReference()
{
    return QStringLiteral("source-account-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

}

bool UnavailableSecretStore::write(const QString &reference, const QByteArray &secret, QString *error)
{
    Q_UNUSED(reference)
    Q_UNUSED(secret)
    setUnavailableError(error);
    return false;
}

std::optional<QByteArray> UnavailableSecretStore::read(const QString &reference, QString *error) const
{
    Q_UNUSED(reference)
    setUnavailableError(error);
    return std::nullopt;
}

bool UnavailableSecretStore::remove(const QString &reference, QString *error)
{
    Q_UNUSED(reference)
    setUnavailableError(error);
    return false;
}

SourceAccountStore::SourceAccountStore(QSettings *settings, ISecretStore *secretStore)
    : m_settings(settings)
    , m_secretStore(secretStore)
{
}

std::optional<SourceAccountStore::PreparedV2> SourceAccountStore::prepareV2(
    const SourceAccountSaveV2 &request, bool resolveUnchanged, QString *error)
{
    if (error) error->clear();
    auto fail = [error](const char *key) -> std::optional<PreparedV2> {
        if (error) *error = QString::fromLatin1(key);
        return std::nullopt;
    };
    if (request.pluginPackageId.isEmpty() || request.sourceId.isEmpty()
        || request.accountId.isEmpty() || request.sourceId.contains('/')
        || request.configurationVersion <= 0) return fail("source.settings.invalidIdentity");

    // First pass validates every schema/default/constraint and supplied value
    // before even reading metadata, so invalid drafts never reach storage IO.
    const QString draftError = validateSourceSettingsDraftV2(request.schema, request.draft);
    if (!draftError.isEmpty()) {
        if (error) *error = draftError;
        return std::nullopt;
    }
    if (!m_settings) return fail("source.settings.storageUnavailable");
    QStringList secretIds, requiredIds;
    for (const auto &section : request.schema) {
        for (const auto &field : section.fields) {
            if (field.secret || field.type == SettingsFieldTypeV2::Secret) {
                secretIds.append(field.id);
            }
        }
    }
    secretIds.sort();
    auto supplied = [&request](const QString &id) { return !request.draft.value(id).toString().isEmpty(); };
    auto allSupplied = [&supplied](const QStringList &ids) {
        for (const auto &id : ids) if (!supplied(id)) return false;
        return !ids.isEmpty();
    };
    const bool allReplaced = allSupplied(secretIds);
    const QString matchingGroup = matchingGroupFor(request.sourceId, request.accountId);
    const QString group = matchingGroup.isEmpty() ? groupFor(request.sourceId, request.accountId) : matchingGroup;
    const QVariantMap previousValues = recordValues(group);
    const auto previous = matchingGroup.isEmpty() ? std::optional<StoredSourceAccount>() : storedAccountForGroup(group);
    if (!previous && !previousValues.isEmpty()) return fail("source.settings.identityConflict");
    // A legacy raw group can be an ancestor of a different account's record.
    // Never let the existing remove-and-restore transaction erase that child.
    for (auto it = previousValues.cbegin(); it != previousValues.cend(); ++it)
        if (it.key().endsWith("/version")) return fail("source.settings.identityConflict");
    if (previous && previous->recordVersion == 2 && previous->pluginPackageId != request.pluginPackageId)
        return fail("source.settings.identityConflict");
    const auto publicValues = sourceSettingsPublicValuesV2(request.schema, request.draft,
        previous ? previous->parameters : QVariantMap());
    for (const auto &section : request.schema) for (const auto &field : section.fields)
        if (secretIds.contains(field.id) && field.required
            && sourceSettingsFieldVisibleV2(field, publicValues)) requiredIds.append(field.id);
    const QString oldReference = previous ? previous->secretReference : QString();
    QString format = previous ? previous->secretFormat : QStringLiteral("none");
    QStringList configured = previous ? previous->configuredSecretFieldIds : QStringList();
    bool discardPrevious = allReplaced;
    if (!oldReference.isEmpty()) {
        if (secretIds.isEmpty()) return fail("source.settings.credentialsReentryRequired");
        if (format == "legacyRaw") {
            if (secretIds.size() == 1) {
                if (!configured.isEmpty() && configured != secretIds && !allReplaced)
                    return fail("source.settings.credentialsReentryRequired");
                configured = secretIds;
            } else {
                // The raw bytes have no field names. Explicit re-entry supplies
                // all required credentials; absent optional credentials stay absent.
                if (!allReplaced && !allSupplied(requiredIds))
                    return fail("source.settings.credentialsReentryRequired");
                discardPrevious = true;
                configured.clear();
            }
        } else if (format != "namedEnvelopeV2") {
            return fail("source.settings.credentialsReentryRequired");
        }
        for (const auto &id : configured)
            if (!secretIds.contains(id) && !discardPrevious)
                return fail("source.settings.credentialsReentryRequired");
    }
    if (discardPrevious) configured.clear();
    const auto validated = validateSourceSettingsV2(request.schema, request.draft,
        previous ? previous->parameters : QVariantMap(), configured, previous.has_value());
    if (!validated.errorKey.isEmpty()) {
        if (error) *error = validated.errorKey;
        return std::nullopt;
    }

    QByteArray preparedSecret;
    const bool rotating = !validated.secretUpdates.isEmpty();
    if (rotating) {
        if (!m_secretStore) return fail("source.settings.secureStorageUnavailable");
        SourceNamedSecretsV2 secrets;
        if (!oldReference.isEmpty() && !discardPrevious) {
            const auto bytes = m_secretStore->read(oldReference, nullptr);
            if (!bytes) return fail("source.settings.secureReadFailed");
            if (format == "legacyRaw") {
                // An explicit raw marker does not make a damaged envelope raw.
                if (secretIds.size() != 1 || sourceSecretInputKindV2(*bytes) != SourceSecretInputKindV2::LegacyRaw)
                    return fail("source.settings.credentialsReentryRequired");
                secrets.insert(secretIds.first(), *bytes);
            } else {
                const auto decoded = decodeSourceSecretsV2(*bytes);
                if (!decoded) return fail("source.settings.credentialsReentryRequired");
                secrets = *decoded;
                QStringList actualIds;
                for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
                    if (it->isEmpty()) return fail("source.settings.credentialsReentryRequired");
                    actualIds.append(it.key());
                }
                if (actualIds != configured) return fail("source.settings.credentialsReentryRequired");
            }
        }
        for (auto it = validated.secretUpdates.cbegin(); it != validated.secretUpdates.cend(); ++it)
            secrets.insert(it.key(), *it);
        const auto envelope = encodeSourceSecretsV2(secrets);
        if (!envelope) return fail("source.settings.invalidSecretEnvelope");
        preparedSecret = *envelope;
        configured = secrets.keys();
        format = QStringLiteral("namedEnvelopeV2");
    } else if (resolveUnchanged && !oldReference.isEmpty()) {
        if (!m_secretStore) return fail("source.settings.secureStorageUnavailable");
        const auto bytes = m_secretStore->read(oldReference, nullptr);
        if (!bytes) return fail("source.settings.secureReadFailed");
        if (format == "legacyRaw") {
            if (sourceSecretInputKindV2(*bytes) != SourceSecretInputKindV2::LegacyRaw)
                return fail("source.settings.credentialsReentryRequired");
        } else {
            const auto decoded = decodeSourceSecretsV2(*bytes);
            if (!decoded || decoded->keys() != configured)
                return fail("source.settings.credentialsReentryRequired");
            for (const auto &value : *decoded)
                if (value.isEmpty()) return fail("source.settings.credentialsReentryRequired");
        }
        preparedSecret = *bytes;
    }
    return PreparedV2{group, previousValues, oldReference, format, configured,
                      validated.parameters, preparedSecret, rotating};
}

std::optional<SourceConfigurationV2> SourceAccountStore::configurationForDraftV2(
    const SourceAccountSaveV2 &request, QString *error)
{
    const auto prepared = prepareV2(request, true, error);
    if (!prepared) return std::nullopt;
    return SourceConfigurationV2{request.pluginPackageId, request.sourceId,
        request.sourceId + '/' + request.accountId, request.accountId, request.displayName,
        prepared->parameters, prepared->secret};
}

bool SourceAccountStore::saveValidatedV2(const SourceAccountSaveV2 &request, QString *error)
{
    const auto prepared = prepareV2(request, false, error);
    if (!prepared) return false;
    auto fail = [error](const char *key) {
        if (error) *error = QString::fromLatin1(key);
        return false;
    };
    const auto &group = prepared->group;
    const auto &previousValues = prepared->previousValues;
    const auto &oldReference = prepared->oldReference;
    const bool rotating = prepared->rotating;
    QString newReference = oldReference;
    if (rotating) {
        newReference = freshSecretReference();
        if (!m_secretStore->write(newReference, prepared->secret, nullptr)) {
            if (!m_secretStore->remove(newReference, nullptr)) return fail("source.settings.secretCleanupFailed");
            return fail("source.settings.secureWriteFailed");
        }
    }
    const QString format = newReference.isEmpty() ? QStringLiteral("none") : prepared->format;
    QVariantMap values{
        {"version", 2}, {"pluginPackageId", request.pluginPackageId},
        {"configurationVersion", request.configurationVersion},
        {"sourceId", request.sourceId}, {"accountId", request.accountId},
        {"sourceInstanceId", request.sourceId + '/' + request.accountId},
        {"displayName", request.displayName}, {"enabled", request.enabled},
        {"secretReference", newReference}, {"secretFormat", format},
        {"configuredSecretFieldIds", prepared->configured},
        // A QVariantMap retains primitive types across INI reopen, unlike plain
        // integer/bool INI values that QSettings may read back as strings.
        {"parameters", prepared->parameters}
    };
    if (!restoreRecord(group, values)) {
        const bool restored = restoreRecord(group, previousValues);
        const bool cleaned = !rotating || m_secretStore->remove(newReference, nullptr);
        if (!cleaned) return fail("source.settings.secretCleanupFailed");
        return fail(restored ? "source.settings.metadataWriteFailed" : "source.settings.metadataRestoreFailed");
    }
    if (rotating && !oldReference.isEmpty() && !m_secretStore->remove(oldReference, nullptr)) {
        // Keep the new secret if metadata rollback fails: the persisted record
        // may still reference it. This matches the legacy transaction policy.
        if (!restoreRecord(group, previousValues)) return fail("source.settings.metadataRestoreFailed");
        if (!m_secretStore->remove(newReference, nullptr)) return fail("source.settings.secretCleanupFailed");
        return fail("source.settings.previousSecretCleanupFailed");
    }
    return true;
}

bool SourceAccountStore::upsert(const SourceAccount &account, bool enabled, QString *error)
{
    if (!m_settings || !m_secretStore) {
        if (error) {
            *error = QStringLiteral("Source account storage is not configured");
        }
        return false;
    }
    if (!hasAccountIdentity(account.sourceId, account.accountId, error)) {
        return false;
    }
    QVariantMap parameters;
    if (!sanitizedParameters(account.parameters, &parameters, error)) {
        return false;
    }

    const QString matchingGroup = matchingGroupFor(account.sourceId, account.accountId);
    const QString group = matchingGroup.isEmpty() ? groupFor(account.sourceId, account.accountId)
                                                   : matchingGroup;
    const QVariantMap previousValues = recordValues(group);
    const QString previousReference = previousValues.value(QStringLiteral("secretReference")).toString();
    const QString newReference = freshSecretReference();
    if (!m_secretStore->write(newReference, account.secret, error)) {
        return false;
    }

    if (writeRecord(group, account, parameters, enabled, newReference)
        && m_settings->status() == QSettings::NoError) {
        if (!previousReference.isEmpty()) {
            QString oldSecretCleanupError;
            if (!m_secretStore->remove(previousReference, &oldSecretCleanupError)) {
                const bool metadataRestored = restoreRecord(group, previousValues);
                QString newSecretCleanupError;
                if (metadataRestored) {
                    m_secretStore->remove(newReference, &newSecretCleanupError);
                }
                if (error) {
                    *error = QStringLiteral("Unable to remove the previous source account secret");
                    if (!metadataRestored) {
                        *error += QStringLiteral("; unable to restore previous metadata");
                    }
                    if (!newSecretCleanupError.isEmpty()) {
                        *error += QStringLiteral("; unable to remove newly written secret: %1")
                                      .arg(newSecretCleanupError);
                    }
                }
                return false;
            }
        }
        return true;
    }

    const bool metadataRestored = restoreRecord(group, previousValues);
    QString secretCleanupError;
    m_secretStore->remove(newReference, &secretCleanupError);
    if (error) {
        *error = QStringLiteral("Unable to persist source account metadata");
        if (!metadataRestored) {
            *error += QStringLiteral("; unable to restore previous metadata");
        }
        if (!secretCleanupError.isEmpty()) {
            *error += QStringLiteral("; unable to remove newly written secret: %1")
                          .arg(secretCleanupError);
        }
    }
    return false;
}

bool SourceAccountStore::setEnabled(const QString &sourceId, const QString &accountId, bool enabled,
                                    QString *error)
{
    if (!m_settings || !hasAccountIdentity(sourceId, accountId, error)) {
        return false;
    }
    const QString group = matchingGroupFor(sourceId, accountId);
    if (group.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Source account metadata is unavailable");
        }
        return false;
    }
    m_settings->setValue(group + QStringLiteral("/enabled"), enabled);
    m_settings->sync();
    if (m_settings->status() == QSettings::NoError) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Unable to persist source account state");
    }
    return false;
}

bool SourceAccountStore::remove(const QString &sourceId, const QString &accountId, QString *error)
{
    if (!m_settings) {
        if (error) {
            *error = QStringLiteral("Source account storage is not configured");
        }
        return false;
    }
    if (!hasAccountIdentity(sourceId, accountId, error)) {
        return false;
    }

    const QString matchingGroup = matchingGroupFor(sourceId, accountId);
    const QString group = matchingGroup.isEmpty() ? groupFor(sourceId, accountId) : matchingGroup;
    const QVariantMap previousValues = recordValues(group);
    const QString reference = previousValues.value(QStringLiteral("secretReference")).toString();
    const bool v2 = previousValues.value(QStringLiteral("version")).toInt() == 2;
    if (!reference.isEmpty() && !m_secretStore) {
        if (error) *error = v2 ? QStringLiteral("source.settings.secureStorageUnavailable")
                              : QStringLiteral("Source account storage is not configured");
        return false;
    }

    m_settings->remove(group);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        restoreRecord(group, previousValues);
        if (error) {
            *error = v2 ? QStringLiteral("source.settings.metadataWriteFailed")
                        : QStringLiteral("Unable to remove source account metadata");
        }
        return false;
    }
    if (!reference.isEmpty() && !m_secretStore->remove(reference, v2 ? nullptr : error)) {
        const bool metadataRestored = restoreRecord(group, previousValues);
        if (error && v2) {
            *error = metadataRestored ? QStringLiteral("source.settings.secretCleanupFailed")
                                     : QStringLiteral("source.settings.metadataRestoreFailed");
        } else if (error && !metadataRestored) {
            *error += QStringLiteral("; unable to restore source account metadata");
        }
        return false;
    }
    return true;
}

std::optional<StoredSourceAccount> SourceAccountStore::storedAccount(const QString &sourceId,
                                                                       const QString &accountId) const
{
    if (!m_settings || sourceId.isEmpty() || accountId.isEmpty()) {
        return std::nullopt;
    }

    const QString group = matchingGroupFor(sourceId, accountId);
    return group.isEmpty() ? std::nullopt : storedAccountForGroup(group);
}

QList<StoredSourceAccount> SourceAccountStore::accounts() const
{
    QList<StoredSourceAccount> storedAccounts;
    QSet<QString> identities;
    for (const QString &root : {kEncodedSourcesGroup, kSourcesGroup}) {
        for (const StoredSourceAccount &account : accountsForRoot(root)) {
            const QString identity = QString::number(account.sourceId.size()) + QLatin1Char(':') +
                                     account.sourceId + account.accountId;
            if (!identities.contains(identity)) {
                identities.insert(identity);
                storedAccounts.append(account);
            }
        }
    }
    return storedAccounts;
}

std::optional<StoredSourceAccount> SourceAccountStore::storedAccountForGroup(const QString &group) const
{
    const QVariantMap values = recordValues(group);
    const int version = values.value(QStringLiteral("version")).toInt();
    if (version != kRecordVersion && version != 2) {
        return std::nullopt;
    }

    StoredSourceAccount account;
    account.sourceId = values.value(QStringLiteral("sourceId")).toString();
    account.accountId = values.value(QStringLiteral("accountId")).toString();
    account.displayName = values.value(QStringLiteral("displayName")).toString();
    account.enabled = values.value(QStringLiteral("enabled"), true).toBool();
    account.secretReference = values.value(QStringLiteral("secretReference")).toString();
    account.recordVersion = version;
    account.sourceInstanceId = account.sourceId + '/' + account.accountId;
    account.secretFormat = QStringLiteral("legacyRaw");
    if (version == 2) {
        account.pluginPackageId = values.value(QStringLiteral("pluginPackageId")).toString();
        account.configurationVersion = values.value(QStringLiteral("configurationVersion")).toInt();
        account.configuredSecretFieldIds = values.value(QStringLiteral("configuredSecretFieldIds")).toStringList();
        account.secretFormat = values.value(QStringLiteral("secretFormat")).toString();
        account.parameters = values.value(QStringLiteral("parameters")).toMap();
        if (account.pluginPackageId.isEmpty() || account.configurationVersion <= 0
            || account.sourceId.contains('/')
            || values.value(QStringLiteral("sourceInstanceId")).toString() != account.sourceInstanceId)
            return std::nullopt;
    }
    for (auto parameter = values.cbegin(); parameter != values.cend(); ++parameter) {
        const QString prefix = QStringLiteral("parameters/");
        if (version == 1 && parameter.key().startsWith(prefix)) {
            account.parameters.insert(parameter.key().mid(prefix.size()), parameter.value());
        }
    }
    if (account.sourceId.isEmpty() || account.accountId.isEmpty()
        || (version == 1 && account.secretReference.isEmpty())) {
        return std::nullopt;
    }
    return account;
}

QList<StoredSourceAccount> SourceAccountStore::accountsForRoot(const QString &root) const
{
    QList<StoredSourceAccount> storedAccounts;
    if (!m_settings) {
        return storedAccounts;
    }

    m_settings->beginGroup(root);
    const QStringList keys = m_settings->allKeys();
    m_settings->endGroup();
    QSet<QString> groups;
    for (const QString &key : keys) {
        if (key == QStringLiteral("version")) {
            groups.insert(root);
        } else if (key.endsWith(QStringLiteral("/version"))) {
            groups.insert(root + QLatin1Char('/') + key.left(key.size() - 8));
        }
    }
    for (const QString &group : groups) {
        const std::optional<StoredSourceAccount> account = storedAccountForGroup(group);
        if (account.has_value()) {
            storedAccounts.append(*account);
        }
    }
    return storedAccounts;
}

std::optional<SourceAccount> SourceAccountStore::sourceAccount(const QString &sourceId,
                                                                 const QString &accountId,
                                                                 QString *error) const
{
    const std::optional<StoredSourceAccount> stored = storedAccount(sourceId, accountId);
    if (!stored.has_value()) {
        if (error) {
            *error = QStringLiteral("Source account metadata is unavailable");
        }
        return std::nullopt;
    }

    if (stored->recordVersion == 2 && stored->secretReference.isEmpty()) {
        return SourceAccount{stored->sourceId, stored->accountId, stored->displayName,
                             stored->parameters, {}};
    }
    const bool v2 = stored->recordVersion == 2;
    if (!m_secretStore) {
        if (error) *error = v2 ? QStringLiteral("source.settings.secureStorageUnavailable")
                              : QStringLiteral("Source account storage is not configured");
        return std::nullopt;
    }
    const std::optional<QByteArray> secret = m_secretStore->read(stored->secretReference, v2 ? nullptr : error);
    if (!secret.has_value()) {
        if (error && v2) *error = QStringLiteral("source.settings.secureReadFailed");
        return std::nullopt;
    }
    return SourceAccount{stored->sourceId, stored->accountId, stored->displayName,
                         stored->parameters, *secret};
}

QString SourceAccountStore::secretReference(const QString &sourceId, const QString &accountId) const
{
    const std::optional<StoredSourceAccount> stored = storedAccount(sourceId, accountId);
    return stored.has_value() ? stored->secretReference : QString();
}

QString SourceAccountStore::groupFor(const QString &sourceId, const QString &accountId) const
{
    const QString encodedSourceId = QString::fromLatin1(QUrl::toPercentEncoding(sourceId));
    const QString encodedAccountId = QString::fromLatin1(QUrl::toPercentEncoding(accountId));
    return kEncodedSourcesGroup + QLatin1Char('/') + encodedSourceId + QLatin1Char('/') +
           encodedAccountId;
}

QString SourceAccountStore::legacyEncodedGroupFor(const QString &sourceId,
                                                   const QString &accountId) const
{
    const QString encodedSourceId = QString::fromLatin1(QUrl::toPercentEncoding(sourceId));
    const QString encodedAccountId = QString::fromLatin1(QUrl::toPercentEncoding(accountId));
    return kSourcesGroup + QLatin1Char('/') + encodedSourceId + QLatin1Char('/') + encodedAccountId;
}

QString SourceAccountStore::legacyRawGroupFor(const QString &sourceId, const QString &accountId) const
{
    return kSourcesGroup + QLatin1Char('/') + sourceId + QLatin1Char('/') + accountId;
}

QString SourceAccountStore::matchingGroupFor(const QString &sourceId, const QString &accountId) const
{
    const QStringList groups{
        groupFor(sourceId, accountId),
        legacyEncodedGroupFor(sourceId, accountId),
        legacyRawGroupFor(sourceId, accountId)};
    for (const QString &group : groups) {
        const std::optional<StoredSourceAccount> account = storedAccountForGroup(group);
        if (account.has_value() && account->sourceId == sourceId && account->accountId == accountId) {
            return group;
        }
    }
    return {};
}

QVariantMap SourceAccountStore::recordValues(const QString &group) const
{
    QVariantMap values;
    m_settings->beginGroup(group);
    const QStringList keys = m_settings->allKeys();
    for (const QString &key : keys) {
        values.insert(key, m_settings->value(key));
    }
    m_settings->endGroup();
    return values;
}

bool SourceAccountStore::restoreRecord(const QString &group, const QVariantMap &values)
{
    m_settings->remove(group);
    for (auto value = values.cbegin(); value != values.cend(); ++value) {
        m_settings->setValue(group + QLatin1Char('/') + value.key(), value.value());
    }
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool SourceAccountStore::writeRecord(const QString &group, const SourceAccount &account,
                                     const QVariantMap &parameters, bool enabled,
                                     const QString &secretReference)
{
    m_settings->remove(group);
    m_settings->setValue(group + QStringLiteral("/version"), kRecordVersion);
    m_settings->setValue(group + QStringLiteral("/sourceId"), account.sourceId);
    m_settings->setValue(group + QStringLiteral("/accountId"), account.accountId);
    m_settings->setValue(group + QStringLiteral("/displayName"), account.displayName);
    m_settings->setValue(group + QStringLiteral("/enabled"), enabled);
    m_settings->setValue(group + QStringLiteral("/secretReference"), secretReference);
    for (auto parameter = parameters.cbegin(); parameter != parameters.cend(); ++parameter) {
        m_settings->setValue(group + QStringLiteral("/parameters/") + parameter.key(),
                             parameter.value());
    }
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}
