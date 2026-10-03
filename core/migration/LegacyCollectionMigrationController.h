#pragma once

#include "LegacyCollectionMigration.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class SourceRegistry;
class PlaybackCoordinator;

// UI-facing, opt-in bridge. It never runs a migration during construction.
class LegacyCollectionMigrationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY revisionChanged)
public:
    LegacyCollectionMigrationController(SourceRegistry *registry, PlaybackCoordinator *playback,
                                        QString sourceDatabase,
                                        QString destinationDatabase, QString backupDatabase,
                                        QObject *parent = nullptr);

    Q_INVOKABLE QVariantList candidates() const;
    Q_INVOKABLE QVariantMap preview() const;
    Q_INVOKABLE QVariantMap run(const QString &candidateInstanceId);
    Q_INVOKABLE QString songStatus(int folderId, int songId) const;
    Q_INVOKABLE bool playSong(int folderId, int songId);
    Q_INVOKABLE bool enqueueSong(int folderId, int songId);
    quint64 revision() const { return m_revision; }

signals:
    void revisionChanged();

private:
    QPointer<SourceRegistry> m_registry;
    QPointer<PlaybackCoordinator> m_playback;
    QString m_sourceDatabase;
    QString m_destinationDatabase;
    QString m_backupDatabase;
    LegacyMediaIdentityResolver m_resolver;
    LegacyCollectionMigration m_migration;
    quint64 m_revision = 0;
    bool submitSong(int folderId, int songId, bool play);
};
