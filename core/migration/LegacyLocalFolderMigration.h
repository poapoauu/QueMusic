#pragma once

#include "v2/SourceV2Types.h"
#include <QStringList>
#include <functional>

class QSettings;
class SourceAccountStore;
struct SourceAccountSaveV2;

struct LegacyLocalMigrationResult {
    int imported = 0;
    int skipped = 0;
    bool complete = false;
    QStringList errorKeys;
};

class LegacyLocalFolderMigration final {
public:
    // Hooks are for deterministic fault injection at durable-write boundaries.
    // Production uses the real account store and QSettings::sync.
    struct Hooks {
        std::function<bool(SourceAccountStore *, const SourceAccountSaveV2 &)> saveAccount;
        std::function<bool(QSettings *)> syncJournal;
        std::function<bool()> afterAccountSaved;
    };
    LegacyLocalFolderMigration(SourceAccountStore *accounts, QSettings *journal,
                               Hooks hooks = {});
    LegacyLocalMigrationResult run(const QString &databasePath,
                                   const SettingsSchemaV2 &schema);
private:
    SourceAccountStore *m_accounts;
    QSettings *m_journal;
    Hooks m_hooks;
};
