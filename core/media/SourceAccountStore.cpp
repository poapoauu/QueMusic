#include "SourceAccountStore.h"

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

    const QString group = groupFor(account.sourceId, account.accountId);
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

    m_settings->remove(group);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        restoreRecord(group, previousValues);
        if (error) {
            *error = QStringLiteral("Unable to remove source account metadata");
        }
        return false;
    }
    if (!reference.isEmpty() && !m_secretStore->remove(reference, error)) {
        const bool metadataRestored = restoreRecord(group, previousValues);
        if (error && !metadataRestored) {
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

    const QStringList groups{
        groupFor(sourceId, accountId),
        legacyEncodedGroupFor(sourceId, accountId),
        legacyRawGroupFor(sourceId, accountId)};
    for (const QString &group : groups) {
        const std::optional<StoredSourceAccount> account = storedAccountForGroup(group);
        if (account.has_value() && account->sourceId == sourceId && account->accountId == accountId) {
            return account;
        }
    }
    return std::nullopt;
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
    if (account.sourceId.isEmpty() || account.accountId.isEmpty() || account.secretReference.isEmpty()) {
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
