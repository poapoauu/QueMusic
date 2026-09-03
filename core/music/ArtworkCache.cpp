#include "ArtworkCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QMutex artworkMutex;
QString hash(const QString &id) { return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex()); }
const QMap<QString,QString> extensions{{"image/jpeg","jpg"},{"image/png","png"},{"image/webp","webp"},{"image/gif","gif"}};
}
ArtworkCache::ArtworkCache(QString directory)
    :m_directory(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/artwork-v2":std::move(directory)) {}
QString ArtworkCache::sourceDirectory(const QString &source) const { return QDir(m_directory).filePath(hash(source)); }
std::optional<QUrl> ArtworkCache::lookup(const QString &source,const QString &id) const
{
    QMutexLocker lock(&artworkMutex);
    if (source.isEmpty() || id.isEmpty()) return {};
    for (const auto &extension:extensions) {
        QFileInfo f(QDir(sourceDirectory(source)).filePath(hash(id)+'.'+extension));
        if (f.isFile() && f.size()>0 && !f.isSymLink()) return QUrl::fromLocalFile(f.absoluteFilePath());
    }
    return {};
}
QUrl ArtworkCache::store(const QString &source,const QString &id,const QByteArray &bytes,const QString &mime)
{
    QMutexLocker lock(&artworkMutex);
    if (source.isEmpty() || id.isEmpty() || bytes.isEmpty() || bytes.size()>32*1024*1024 || !extensions.contains(mime)) return {};
    const auto dir=sourceDirectory(source);
    if (!QDir().mkpath(dir)) return {};
    const auto path=QDir(dir).absoluteFilePath(hash(id)+'.'+extensions.value(mime));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) return {};
    return QUrl::fromLocalFile(path);
}
void ArtworkCache::invalidateSource(const QString &source)
{
    QMutexLocker lock(&artworkMutex);
    QDir dir(sourceDirectory(source));
    for (const auto &name:dir.entryList(QDir::Files)) dir.remove(name);
    QDir(m_directory).rmdir(hash(source));
}
