#include "LocalSourceScanner.h"
#include "core/local-media/LocalMediaFiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

namespace {
SourceErrorV2 failure(SourceErrorKindV2 kind, const QString &key)
{
    SourceErrorV2 error;
    error.kind = kind;
    error.messageKey = key;
    return error;
}
bool ignored(const LocalScanConfig &config, const QString &path)
{
    const QString relative = QDir(config.canonicalRoot).relativeFilePath(path);
    for (const auto &ignore : config.ignoreDirectories)
        if (relative == ignore || relative.startsWith(ignore + '/')) return true;
    return false;
}
QString text(const TagLib::String &s)
{
    return QString::fromUtf8(s.toCString(true));
}
}

LocalSourceScanner::LocalSourceScanner(AccessPolicy access) : m_access(std::move(access)) {}

std::optional<LocalScanConfig> LocalSourceScanner::parseConfig(const QVariantMap &parameters,
                                                              SourceErrorV2 *error)
{
    auto reject = [&](SourceErrorKindV2 kind, const QString &key) -> std::optional<LocalScanConfig> {
        if (error) *error = failure(kind, key);
        return std::nullopt;
    };
    const auto rootValue = parameters.value("rootDirectory");
    if (rootValue.metaType().id() != QMetaType::QString)
        return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidRoot");
    QString root = rootValue.toString();
    // Paths are literal, never user-input expansion. A local file URL is allowed
    // only without authority/userinfo, query or fragment; network shares are not.
    const QUrl url(root, QUrl::StrictMode);
    if (root.startsWith("file:", Qt::CaseInsensitive)) {
        if (!url.isValid() || !url.isLocalFile() || !url.authority().isEmpty()
            || url.hasQuery() || url.hasFragment())
            return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidRoot");
        root = url.toLocalFile();
    }
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || root.startsWith("//")
        || root.startsWith("\\\\"))
        return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidRoot");
    LocalScanConfig config;
    const QFileInfo rootInfo(root);
    config.canonicalRoot = rootInfo.canonicalFilePath();
    if (config.canonicalRoot.isEmpty() || !rootInfo.isDir())
        return reject(SourceErrorKindV2::NotFound, "local.root.notFound");
    if (parameters.contains("recursive")) {
        if (parameters.value("recursive").metaType().id() != QMetaType::Bool)
            return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidRecursive");
        config.recursive = parameters.value("recursive").toBool();
    }
    if (parameters.contains("ignoreDirectories")
        && parameters.value("ignoreDirectories").metaType().id() != QMetaType::QString)
        return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidIgnoreDirectories");
    for (QString line : parameters.value("ignoreDirectories").toString().split('\n')) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        line.replace('\\', '/');
        const auto components = line.split('/');
        if (QDir::isAbsolutePath(line) || line.contains(':') || components.contains("..")
            || components.contains("") || components.contains("."))
            return reject(SourceErrorKindV2::InvalidRequest, "local.config.invalidIgnoreDirectories");
        config.ignoreDirectories.append(line);
    }
    config.ignoreDirectories.removeDuplicates();
    return config;
}

std::optional<QString> LocalSourceScanner::validatedPath(const LocalScanConfig &config,
                                                       const QString &entityId, bool directory)
{
    const QUrl url(entityId, QUrl::StrictMode);
    if (!url.isValid() || !url.isLocalFile() || !url.authority().isEmpty()
        || url.hasQuery() || url.hasFragment()) return std::nullopt;
    const auto bounded = LocalMediaFiles::boundedPath(url.toLocalFile(), config.canonicalRoot, directory);
    if (!bounded || ignored(config, *bounded)
        || QUrl::fromLocalFile(*bounded).toString(QUrl::FullyEncoded) != entityId)
        return std::nullopt;
    if (!directory) {
        QFile file(*bounded);
        if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    }
    return bounded;
}

LocalScanResult LocalSourceScanner::scan(const LocalScanConfig &config,
                                       const std::atomic_bool &cancel) const
{
    LocalScanResult result;
    auto accessible = [&](const QString &path, bool directory) {
        if (m_access && !m_access(path, directory)) return false;
        const QFileInfo info(path);
        if (!info.isReadable()) return false;
        if (directory) return info.isDir();
        QFile file(path);
        return info.isFile() && file.open(QIODevice::ReadOnly);
    };
    auto cancelled = [&] {
        if (!cancel.load(std::memory_order_relaxed)) return false;
        result.cancelled = true;
        result.entries.clear();
        result.watchedDirectories.clear();
        return true;
    };
    if (cancelled()) return result;
    const auto root = LocalMediaFiles::boundedPath(config.canonicalRoot, config.canonicalRoot, true);
    if (!root || *root != config.canonicalRoot || !accessible(*root, true)) {
        result.error = failure(SourceErrorKindV2::Unavailable, "local.root.unavailable");
        return result;
    }
    // Compatibility candidates from FilePage; not a declaration of decoder support.
    static const QSet<QString> extensions = {"mp3", "wav", "aac", "flac", "ogg", "eac3", "wma",
                                            "ac3", "alac", "mkv", "wmv", "avi", "mpeg4"};
    QStringList pending{*root};
    QSet<QString> seen;
    while (!pending.isEmpty()) {
        if (cancelled()) return result;
        const QString directory = pending.takeLast();
        if (!accessible(directory, true)) {
            if (directory == config.canonicalRoot) {
                result.error = failure(SourceErrorKindV2::Unavailable, "local.root.unavailable");
                result.entries.clear();
                result.watchedDirectories.clear();
                return result;
            }
            result.warningKeys.append("local.scan.directoryUnavailable");
            continue;
        }
        result.watchedDirectories.append(directory);
        const auto files = QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                         | QDir::Hidden | QDir::System, QDir::Name);
        for (const auto &info : files) {
            if (cancelled()) return result;
            if (info.isDir()) {
                if (!config.recursive || info.isSymLink() || info.isJunction()) continue;
                const auto path = LocalMediaFiles::boundedPath(info.filePath(), config.canonicalRoot, true);
                if (path && !ignored(config, *path)) pending.append(*path);
                else if (!ignored(config, info.filePath())) result.warningKeys.append("local.scan.directoryUnavailable");
                continue;
            }
            if (!extensions.contains(info.suffix().toLower())) continue;
            const auto path = LocalMediaFiles::boundedPath(info.filePath(), config.canonicalRoot);
            if (!path || !accessible(*path, false)) {
                result.warningKeys.append("local.scan.fileUnavailable");
                continue;
            }
            if (ignored(config, *path) || seen.contains(*path)) continue;
            seen.insert(*path);
            LocalScanEntry entry;
            entry.canonicalPath = *path;
            entry.entityId = QUrl::fromLocalFile(*path).toString(QUrl::FullyEncoded);
            entry.item.ref.entityId = entry.entityId;
            entry.item.ref.entityType = MediaEntityTypeV2::Track;
            entry.item.title = QFileInfo(*path).completeBaseName();
#ifdef Q_OS_WIN
            const auto encoded = path->toStdWString();
            TagLib::FileRef ref(encoded.c_str());
#else
            const auto encoded = QFile::encodeName(*path);
            TagLib::FileRef ref(encoded.constData());
#endif
            if (!ref.isNull() && ref.tag()) {
                if (!ref.tag()->title().isEmpty()) entry.item.title = text(ref.tag()->title());
                entry.item.album = text(ref.tag()->album());
                const auto properties = ref.properties();
                const auto artists = properties.find("ARTIST");
                if (artists != properties.end())
                    for (const auto &artist : artists->second) entry.item.artists.append(text(artist));
                if (entry.item.artists.isEmpty() && !ref.tag()->artist().isEmpty())
                    entry.item.artists.append(text(ref.tag()->artist()));
                if (ref.audioProperties()) entry.item.durationMs = ref.audioProperties()->lengthInMilliseconds();
            } else result.warningKeys.append("local.scan.metadataUnavailable");
            if (cancelled()) return result;
            result.entries.append(entry);
        }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b) { return a.entityId < b.entityId; });
    result.watchedDirectories.sort();
    result.warningKeys.removeDuplicates();
    if (cancelled()) return result;
    return result;
}
