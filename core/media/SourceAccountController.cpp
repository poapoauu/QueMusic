#include "SourceAccountController.h"

#include "SourceAccountStore.h"
#include "SourceManager.h"
#include "SourceSessionRegistry.h"

#include <QUrl>
#include <QUuid>

SourceAccountController::SourceAccountController(SourceAccountStore *accountStore,
                                                 SourceSessionRegistry *registry,
                                                 SourceManager *sourceManager, QObject *parent)
    : QObject(parent)
    , m_accountStore(accountStore)
    , m_registry(registry)
    , m_sourceManager(sourceManager)
{
    if (m_sourceManager != nullptr) {
        connect(m_sourceManager, &SourceManager::sourceChanged, this,
                &SourceAccountController::availableSourcesChanged);
    }
}

QVariantList SourceAccountController::accounts() const
{
    QVariantList result;
    if (m_accountStore == nullptr) {
        return result;
    }
    for (const StoredSourceAccount &account : m_accountStore->accounts()) {
        result.append(QVariantMap{{QStringLiteral("sourceId"), account.sourceId},
                                  {QStringLiteral("accountId"), account.accountId},
                                  {QStringLiteral("displayName"), account.displayName},
                                  {QStringLiteral("enabled"), account.enabled},
                                  {QStringLiteral("serverUrl"),
                                   account.parameters.value(QStringLiteral("serverUrl"))},
                                  {QStringLiteral("username"),
                                   account.parameters.value(QStringLiteral("username"))}});
    }
    return result;
}

QVariantList SourceAccountController::availableSources() const
{
    return m_sourceManager == nullptr ? QVariantList{} : m_sourceManager->availableSources();
}

QString SourceAccountController::lastError() const
{
    return m_lastError;
}

bool SourceAccountController::createNavidromeAccount(const QString &displayName, const QString &serverUrl,
                                                      const QString &username, const QString &password)
{
    const QByteArray secret = password.toUtf8();
    if (!validateNavidromeInput(serverUrl, username, secret, true)) {
        return false;
    }
    return storeNavidromeAccount(QUuid::createUuid().toString(QUuid::WithoutBraces), displayName,
                                 serverUrl, username, secret);
}

bool SourceAccountController::updateNavidromeAccount(const QString &accountId,
                                                      const QString &displayName,
                                                      const QString &serverUrl,
                                                      const QString &username,
                                                      const QString &password)
{
    if (m_accountStore == nullptr || accountId.isEmpty()) {
        setLastError(QStringLiteral("A Navidrome account is required"));
        return false;
    }
    QString error;
    const std::optional<SourceAccount> existing =
        m_accountStore->sourceAccount(QStringLiteral("navidrome"), accountId, &error);
    if (!existing.has_value()) {
        setLastError(error.isEmpty() ? QStringLiteral("Navidrome account is unavailable") : error);
        return false;
    }
    const QByteArray secret = password.isEmpty() ? existing->secret : password.toUtf8();
    if (!validateNavidromeInput(serverUrl, username, secret, false)) {
        return false;
    }
    return storeNavidromeAccount(accountId, displayName, serverUrl, username, secret);
}

bool SourceAccountController::setAccountEnabled(const QString &accountId, bool enabled)
{
    if (m_accountStore == nullptr || accountId.isEmpty()) {
        setLastError(QStringLiteral("A Navidrome account is required"));
        return false;
    }
    QString error;
    if (!m_accountStore->setEnabled(QStringLiteral("navidrome"), accountId, enabled, &error)) {
        setLastError(error);
        return false;
    }
    if (m_registry != nullptr) {
        if (enabled) {
            m_registry->enable(QStringLiteral("navidrome"), accountId);
        } else {
            m_registry->disable(QStringLiteral("navidrome"), accountId);
        }
    }
    setLastError({});
    emit accountsChanged();
    return true;
}

bool SourceAccountController::removeAccount(const QString &accountId)
{
    if (m_accountStore == nullptr || accountId.isEmpty()) {
        setLastError(QStringLiteral("A Navidrome account is required"));
        return false;
    }
    if (m_registry != nullptr) {
        m_registry->remove(QStringLiteral("navidrome"), accountId);
    }
    QString error;
    if (!m_accountStore->remove(QStringLiteral("navidrome"), accountId, &error)) {
        setLastError(error);
        return false;
    }
    setLastError({});
    emit accountsChanged();
    return true;
}

bool SourceAccountController::storeNavidromeAccount(const QString &accountId, const QString &displayName,
                                                     const QString &serverUrl, const QString &username,
                                                     const QByteArray &secret)
{
    if (m_accountStore == nullptr) {
        setLastError(QStringLiteral("Source account storage is unavailable"));
        return false;
    }
    const std::optional<StoredSourceAccount> previous =
        m_accountStore->storedAccount(QStringLiteral("navidrome"), accountId);
    QString error;
    const SourceAccount account{QStringLiteral("navidrome"), accountId,
                                displayName.trimmed().isEmpty() ? username.trimmed()
                                                                : displayName.trimmed(),
                                {{QStringLiteral("serverUrl"), serverUrl.trimmed()},
                                 {QStringLiteral("username"), username.trimmed()}},
                                secret};
    if (!m_accountStore->upsert(account, previous.has_value() ? previous->enabled : true, &error)) {
        setLastError(error);
        return false;
    }
    if (m_registry != nullptr) {
        m_registry->remove(account.sourceId, account.accountId);
        if (previous.has_value() && !previous->enabled) {
            m_registry->disable(account.sourceId, account.accountId);
        }
    }
    setLastError({});
    emit accountsChanged();
    return true;
}

bool SourceAccountController::validateNavidromeInput(const QString &serverUrl, const QString &username,
                                                     const QByteArray &secret, bool requireSecret)
{
    const QUrl url(serverUrl.trimmed());
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty() || !url.userName().isEmpty() || !url.password().isEmpty()
        || !url.fragment().isEmpty()) {
        setLastError(QStringLiteral("Enter an http or https Navidrome server URL without credentials"));
        return false;
    }
    if (username.trimmed().isEmpty()) {
        setLastError(QStringLiteral("A Navidrome username is required"));
        return false;
    }
    if (requireSecret && secret.isEmpty()) {
        setLastError(QStringLiteral("A Navidrome password or token is required"));
        return false;
    }
    return true;
}

void SourceAccountController::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}
