#pragma once

#include "QueueHistoryTypes.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>

class PlaybackCoordinator;

// Host-owned disk lifecycle. File paths and backup hooks are injected by tests.
class QueueHistoryStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString warningKey READ warningKey NOTIFY warningChanged)
    Q_PROPERTY(QString backupPath READ backupPath NOTIFY warningChanged)
    Q_PROPERTY(QVariantMap latest READ latest NOTIFY historyChanged)
public:
    struct Hooks {
        std::function<bool(const QString &, const QByteArray &)> write;
        std::function<bool(const QString &, const QString &)> backup;
    };

    explicit QueueHistoryStore(PlaybackCoordinator *coordinator, QString filePath,
                               QObject *parent = nullptr, Hooks hooks = {});
    bool loadAndAttach();
    QList<RecentPlay> history() const;
    QVariantMap latest() const;
    Q_INVOKABLE bool playLatest();
    LegacyImportResult importLegacyFile(const QString &path);
    Q_INVOKABLE bool retrySave();
    QString warningKey() const;
    QString backupPath() const;
signals:
    void warningChanged();
    void historyChanged();
private:
    int latestQueueIndex() const;
    void persist();
    void recordStart(const QUuid &generation);
    void warn(const QString &key);
    QString newBackupPath() const;

    QPointer<PlaybackCoordinator> m_coordinator;
    QString m_filePath;
    Hooks m_hooks;
    QList<RecentPlay> m_history;
    QString m_warningKey;
    QString m_backupPath;
    QUuid m_lastRecordedGeneration;
    int m_legacyImportVersion = 0;
    bool m_attached = false;
    bool m_readOnly = false;
};
