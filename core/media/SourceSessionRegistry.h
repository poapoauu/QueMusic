#pragma once

#include "MediaTypes.h"
#include "SourceAccountStore.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

class IMusicSourceSession;
class SourceManager;

class SourceSessionRegistry : public QObject {
    Q_OBJECT

public:
    explicit SourceSessionRegistry(SourceManager *sourceManager, SourceAccountStore *accountStore,
                                   QObject *parent = nullptr);
    ~SourceSessionRegistry() override;

    IMusicSourceSession *sessionFor(const MediaId &id);
    QUuid requestArtwork(const MediaId &id, const TrackRef &track);
    void trackRequest(const MediaId &id, const QUuid &requestId);
    void completeRequest(const MediaId &id, const QUuid &requestId);
    void disable(const QString &sourceId, const QString &accountId);
    void remove(const QString &sourceId, const QString &accountId);
    QList<StoredSourceAccount> enabledAccounts() const;

signals:
    void sessionInvalidated(QString sourceId, QString accountId);

private:
    struct SessionKey {
        QString sourceId;
        QString accountId;

        friend bool operator==(const SessionKey &left, const SessionKey &right)
        {
            return left.sourceId == right.sourceId && left.accountId == right.accountId;
        }

        friend size_t qHash(const SessionKey &key, size_t seed = 0)
        {
            return qHash(key.sourceId, seed) ^ (qHash(key.accountId, seed) << 1);
        }
    };

    struct SessionEntry {
        QPointer<IMusicSourceSession> session;
        QSet<QUuid> requests;
    };

    static SessionKey keyFor(const MediaId &id);
    static SessionKey keyFor(const QString &sourceId, const QString &accountId);
    void release(const SessionKey &key);
    void invalidateMissingSources();
    void forgetRequest(const SessionKey &key, const QUuid &requestId);

    QPointer<SourceManager> m_sourceManager;
    SourceAccountStore *m_accountStore = nullptr;
    QHash<SessionKey, SessionEntry> m_sessions;
    QSet<SessionKey> m_disabledAccounts;
};
