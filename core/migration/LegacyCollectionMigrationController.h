#pragma once

#include "LegacyCollectionMigration.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class SourceRegistry;

// UI-facing, opt-in bridge. It never runs a migration during construction.
class LegacyCollectionMigrationController final : public QObject {
    Q_OBJECT
public:
    LegacyCollectionMigrationController(SourceRegistry *registry, QString sourceDatabase,
                                        QString destinationDatabase, QString backupDatabase,
                                        QObject *parent = nullptr);

    Q_INVOKABLE QVariantList candidates() const;
    Q_INVOKABLE QVariantMap preview() const;
    Q_INVOKABLE QVariantMap run(const QString &candidateInstanceId);

private:
    QPointer<SourceRegistry> m_registry;
    QString m_sourceDatabase;
    QString m_destinationDatabase;
    QString m_backupDatabase;
    LegacyMediaIdentityResolver m_resolver;
    LegacyCollectionMigration m_migration;
};
