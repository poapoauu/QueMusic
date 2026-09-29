#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

struct LocalIdentitySnapshot {
    QHash<QString, QString> trackIdsByPath;
    QHash<QString, QString> directoryIdsByPath;
};

// Private to the Local plugin. Call reconcile only after a complete successful scan.
class LocalIdentityStore final
{
public:
    LocalIdentityStore(QString filePath, QString sourceInstanceId);
    bool reconcile(const QStringList &trackPaths, const QStringList &directoryPaths,
                   LocalIdentitySnapshot *out, QString *errorKey);

private:
    QString m_filePath;
    QString m_sourceInstanceId;
};
