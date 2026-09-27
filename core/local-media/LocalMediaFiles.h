#pragma once
#include <QString>
#include <QVariantMap>
#include <optional>

namespace LocalMediaFiles {
// Check canonical path components, including symlink targets.
std::optional<QString> boundedPath(const QString &path, const QString &canonicalRoot,
                                   bool directory = false);
QString lyricsText(const QString &canonicalFile, const QString &canonicalRoot);
QVariantMap artworkPayload(const QString &canonicalFile, const QString &canonicalRoot);
QString sidecarArtworkPath(const QString &canonicalFile, const QString &canonicalRoot);
// Shared parsing preserves the legacy display adapter's list/source API.
QVariantList parseLrc(const QString &contents);
QVariantMap readLyrics(const QString &filePath, const QString &canonicalRoot);
}
