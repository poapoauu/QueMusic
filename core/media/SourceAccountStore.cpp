#include "SourceAccountStore.h"

#include <QSettings>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kRecordVersion = 1;
const QString kSourcesGroup = QStringLiteral("sources");

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

bool hasSensitiveParameterName(const QVariantMap &parameters, QString *error)
{
    static const QStringList sensitiveNames{
        QStringLiteral("password"), QStringLiteral("secret"), QStringLiteral("token"),
        QStringLiteral("credential"), QStringLiteral("apikey"),
        QStringLiteral("authorization"), QStringLiteral("cookie")};
    for (auto parameter = parameters.cbegin(); parameter != parameters.cend(); ++parameter) {
        QString normalized = parameter.key().toCaseFolded();
        normalized.remove(QLatin1Char('-'));
        normalized.remove(QLatin1Char('_'));
        normalized.remove(QLatin1Char('.'));
        if (std::any_of(sensitiveNames.cbegin(), sensitiveNames.cend(),
                        [&normalized](const QString &name) { return normalized.contains(name); })) {
            if (error) {
                *error = QStringLiteral("Secret parameters must be supplied through SourceAccount::secret");
            }
            return true;
        }
    }
    return false;
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
    if (hasSensitiveParameterName(account.parameters, error)) {
        return false;
    }

    const QString group = groupFor(account.sourceId, account.accountId);
    const QVariantMap previousValues = recordValues(group);
    const QString previousReference = previousValues.value(QStringLiteral("secretReference")).toString();
    const QString newReference = freshSecretReference();
    if (!m_secretStore->write(newReference, account.secret, error)) {
        return false;
    }

    if (writeRecord(group, account, enabled, newReference)
        && m_settings->status() == QSettings::NoError) {
        if (!previousReference.isEmpty()) {
            m_secretStore->remove(previousReference);
        }
        return true;
    }

    restoreRecord(group, previousValues);
    QString secretCleanupError;
    m_secretStore->remove(newReference, &secretCleanupError);
    if (error) {
        *error = QStringLiteral("Unable to persist source account metadata");
        if (!secretCleanupError.isEmpty()) {
            *error += QStringLiteral("; unable to remove newly written secret: %1")
                          .arg(secretCleanupError);
        }
    }
    return false;
}

bool SourceAccountStore::remove(const QString &sourceId, const QString &accountId, QString *error)
{
    if (!m_settings || !m_secretStore) {
        if (error) {
            *error = QStringLiteral("Source account storage is not configured");
        }
        return false;
    }
    if (!hasAccountIdentity(sourceId, accountId, error)) {
        return false;
    }

    const QString group = groupFor(sourceId, accountId);
    const QVariantMap previousValues = recordValues(group);
    const QString reference = previousValues.value(QStringLiteral("secretReference")).toString();
    if (!reference.isEmpty() && !m_secretStore->remove(reference, error)) {
        return false;
    }

    m_settings->remove(group);
    m_settings->sync();
    if (m_settings->status() == QSettings::NoError) {
        return true;
    }

    restoreRecord(group, previousValues);
    if (error) {
        *error = QStringLiteral("Unable to remove source account metadata");
    }
    return false;
}

std::optional<StoredSourceAccount> SourceAccountStore::storedAccount(const QString &sourceId,
                                                                       const QString &accountId) const
{
    if (!m_settings || sourceId.isEmpty() || accountId.isEmpty()) {
        return std::nullopt;
    }

    const QString group = groupFor(sourceId, accountId);
    const QVariantMap values = recordValues(group);
    if (values.value(QStringLiteral("version")).toInt() != kRecordVersion) {
        return std::nullopt;
    }

    StoredSourceAccount account;
    account.sourceId = values.value(QStringLiteral("sourceId")).toString();
    account.accountId = values.value(QStringLiteral("accountId")).toString();
    account.displayName = values.value(QStringLiteral("displayName")).toString();
    account.enabled = values.value(QStringLiteral("enabled"), true).toBool();
    account.secretReference = values.value(QStringLiteral("secretReference")).toString();
    for (auto parameter = values.cbegin(); parameter != values.cend(); ++parameter) {
        const QString prefix = QStringLiteral("parameters/");
        if (parameter.key().startsWith(prefix)) {
            account.parameters.insert(parameter.key().mid(prefix.size()), parameter.value());
        }
    }
    if (account.sourceId != sourceId || account.accountId != accountId
        || account.secretReference.isEmpty()) {
        return std::nullopt;
    }
    return account;
}

QList<StoredSourceAccount> SourceAccountStore::accounts() const
{
    QList<StoredSourceAccount> storedAccounts;
    if (!m_settings) {
        return storedAccounts;
    }

    m_settings->beginGroup(kSourcesGroup);
    const QStringList sourceGroups = m_settings->childGroups();
    m_settings->endGroup();
    for (const QString &sourceGroup : sourceGroups) {
        m_settings->beginGroup(kSourcesGroup + QLatin1Char('/') + sourceGroup);
        const QStringList accountGroups = m_settings->childGroups();
        m_settings->endGroup();
        for (const QString &accountGroup : accountGroups) {
            const std::optional<StoredSourceAccount> account =
                storedAccount(sourceGroup, accountGroup);
            if (account.has_value()) {
                storedAccounts.append(*account);
            }
        }
    }
    return storedAccounts;
}

std::optional<SourceAccount> SourceAccountStore::sourceAccount(const QString &sourceId,
                                                                 const QString &accountId,
                                                                 QString *error) const
{
    if (!m_secretStore) {
        if (error) {
            *error = QStringLiteral("Source account storage is not configured");
        }
        return std::nullopt;
    }
    const std::optional<StoredSourceAccount> stored = storedAccount(sourceId, accountId);
    if (!stored.has_value()) {
        if (error) {
            *error = QStringLiteral("Source account metadata is unavailable");
        }
        return std::nullopt;
    }

    const std::optional<QByteArray> secret = m_secretStore->read(stored->secretReference, error);
    if (!secret.has_value()) {
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
    return kSourcesGroup + QLatin1Char('/') + sourceId + QLatin1Char('/') + accountId;
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

void SourceAccountStore::restoreRecord(const QString &group, const QVariantMap &values)
{
    m_settings->remove(group);
    for (auto value = values.cbegin(); value != values.cend(); ++value) {
        m_settings->setValue(group + QLatin1Char('/') + value.key(), value.value());
    }
    m_settings->sync();
}

bool SourceAccountStore::writeRecord(const QString &group, const SourceAccount &account, bool enabled,
                                     const QString &secretReference)
{
    m_settings->remove(group);
    m_settings->setValue(group + QStringLiteral("/version"), kRecordVersion);
    m_settings->setValue(group + QStringLiteral("/sourceId"), account.sourceId);
    m_settings->setValue(group + QStringLiteral("/accountId"), account.accountId);
    m_settings->setValue(group + QStringLiteral("/displayName"), account.displayName);
    m_settings->setValue(group + QStringLiteral("/enabled"), enabled);
    m_settings->setValue(group + QStringLiteral("/secretReference"), secretReference);
    for (auto parameter = account.parameters.cbegin(); parameter != account.parameters.cend(); ++parameter) {
        m_settings->setValue(group + QStringLiteral("/parameters/") + parameter.key(),
                             parameter.value());
    }
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}
