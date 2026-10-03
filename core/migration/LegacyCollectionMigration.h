#pragma once

#include "LegacyMediaIdentityResolver.h"
#include <QString>
#include <QStringList>

// Explicit, retryable import of legacy `my` folders. The source database is
// opened read-only. Output contains refs and display snapshots, never paths.
class LegacyCollectionMigration final {
public:
    struct Result {
        int matched = 0;
        int noMatch = 0;
        int ambiguous = 0;
        int unavailable = 0;
        int invalidPath = 0;
        bool committed = false;
        QString errorKey;
    };

    explicit LegacyCollectionMigration(const LegacyMediaIdentityResolver *resolver);
    Result run(const QString &sourceDatabase, const QString &destinationDatabase,
               const QString &backupDatabase, const QStringList &candidateInstanceIds) const;

private:
    const LegacyMediaIdentityResolver *m_resolver = nullptr;
};
