#include "SourceSessionRegistry.h"

#include "IMusicSourceSession.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"

#include <algorithm>

SourceSessionRegistry::SourceSessionRegistry(SourceManager *sourceManager,
                                             SourceAccountStore *accountStore, QObject *parent)
    : QObject(parent)
    , m_sourceManager(sourceManager)
    , m_accountStore(accountStore)
{
    if (m_sourceManager != nullptr) {
        connect(m_sourceManager, &SourceManager::sourceChanged, this,
                &SourceSessionRegistry::invalidateMissingSources);
        connect(m_sourceManager, &QObject::destroyed, this, [this] {
            m_sourceManager = nullptr;
            const auto keys = m_sessions.keys();
            for (const SessionKey &key : keys) {
                release(key);
            }
        });
    }
}

SourceSessionRegistry::~SourceSessionRegistry()
{
    const auto keys = m_sessions.keys();
    for (const SessionKey &key : keys) {
        release(key);
    }
}

IMusicSourceSession *SourceSessionRegistry::sessionFor(const MediaId &id)
{
    const SessionKey key = keyFor(id);
    if (key.sourceId.isEmpty() || key.accountId.isEmpty() || m_disabledAccounts.contains(key)) {
        return nullptr;
    }

    auto existing = m_sessions.find(key);
    if (existing != m_sessions.end()) {
        if (existing->session != nullptr) {
            return existing->session;
        }
        m_sessions.erase(existing);
    }

    if (m_sourceManager == nullptr || m_accountStore == nullptr) {
        return nullptr;
    }

    const std::optional<StoredSourceAccount> stored =
        m_accountStore->storedAccount(key.sourceId, key.accountId);
    if (!stored.has_value() || !stored->enabled) {
        return nullptr;
    }

    const std::optional<SourceAccount> account =
        m_accountStore->sourceAccount(key.sourceId, key.accountId);
    if (!account.has_value()) {
        return nullptr;
    }

    IMusicSourceSession *session =
        m_sourceManager->createSession(key.sourceId, *account, this);
    if (session == nullptr) {
        return nullptr;
    }

    m_sessions.insert(key, {session, {}});
    connect(session, &IMusicSourceSession::requestSucceeded, this,
            [this, key](const QUuid &requestId, const QString &, const QJsonValue &) {
                forgetRequest(key, requestId);
            });
    connect(session, &IMusicSourceSession::requestFailed, this,
            [this, key](const QUuid &requestId, const SourceError &) {
                forgetRequest(key, requestId);
            });
    connect(session, &QObject::destroyed, this, [this, key] {
        if (m_sessions.remove(key) != 0) {
            emit sessionInvalidated(key.sourceId, key.accountId);
        }
    });
    return session;
}

void SourceSessionRegistry::trackRequest(const MediaId &id, const QUuid &requestId)
{
    if (requestId.isNull()) {
        return;
    }

    const SessionKey key = keyFor(id);
    const auto entry = m_sessions.find(key);
    if (entry != m_sessions.end() && entry->session != nullptr) {
        entry->requests.insert(requestId);
    }
}

void SourceSessionRegistry::completeRequest(const MediaId &id, const QUuid &requestId)
{
    forgetRequest(keyFor(id), requestId);
}

void SourceSessionRegistry::disable(const QString &sourceId, const QString &accountId)
{
    const SessionKey key = keyFor(sourceId, accountId);
    if (key.sourceId.isEmpty() || key.accountId.isEmpty()) {
        return;
    }
    m_disabledAccounts.insert(key);
    release(key);
}

void SourceSessionRegistry::remove(const QString &sourceId, const QString &accountId)
{
    const SessionKey key = keyFor(sourceId, accountId);
    if (key.sourceId.isEmpty() || key.accountId.isEmpty()) {
        return;
    }
    release(key);
}

QList<StoredSourceAccount> SourceSessionRegistry::enabledAccounts() const
{
    if (m_accountStore == nullptr) {
        return {};
    }

    QList<StoredSourceAccount> enabled;
    for (const StoredSourceAccount &account : m_accountStore->accounts()) {
        if (account.enabled && !m_disabledAccounts.contains(keyFor(account.sourceId, account.accountId))) {
            enabled.append(account);
        }
    }
    return enabled;
}

SourceSessionRegistry::SessionKey SourceSessionRegistry::keyFor(const MediaId &id)
{
    return keyFor(id.sourceId, id.accountId);
}

SourceSessionRegistry::SessionKey SourceSessionRegistry::keyFor(const QString &sourceId,
                                                                 const QString &accountId)
{
    return {sourceId, accountId};
}

void SourceSessionRegistry::release(const SessionKey &key)
{
    const auto entry = m_sessions.find(key);
    if (entry == m_sessions.end()) {
        return;
    }

    const QPointer<IMusicSourceSession> session = entry->session;
    const QSet<QUuid> requests = entry->requests;
    m_sessions.erase(entry);
    if (session != nullptr) {
        for (const QUuid &requestId : requests) {
            session->cancel(requestId);
        }
        delete session;
    }
    emit sessionInvalidated(key.sourceId, key.accountId);
}

void SourceSessionRegistry::invalidateMissingSources()
{
    if (m_sourceManager == nullptr) {
        return;
    }

    const QStringList sourceIds = m_sourceManager->sourceIds();
    const auto keys = m_sessions.keys();
    for (const SessionKey &key : keys) {
        if (!sourceIds.contains(key.sourceId)) {
            // SourceManager only emits sourceChanged after a package unload
            // succeeds. Do not dereference a session from that package here:
            // normal callers must have removed every account beforehand.
            if (m_sessions.remove(key) != 0) {
                emit sessionInvalidated(key.sourceId, key.accountId);
            }
        }
    }
}

void SourceSessionRegistry::forgetRequest(const SessionKey &key, const QUuid &requestId)
{
    const auto entry = m_sessions.find(key);
    if (entry != m_sessions.end()) {
        entry->requests.remove(requestId);
    }
}
