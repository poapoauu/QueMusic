// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
#include "LocalMediaFiles.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QDir>
#include <QMimeDatabase>
#include <QImage>

#include <algorithm>

#include <fileref.h>
#include <mpegfile.h>
#include <id3v2tag.h>
#include <synchronizedlyricsframe.h>
#include <unsynchronizedlyricsframe.h>
#include <tpropertymap.h>

namespace {

QString tagString(const TagLib::String &value)
{
    return QString::fromUtf8(value.toCString(true));
}

QVariantMap lyricLine(qint64 time, const QString &text)
{
    QVariantMap line;
    line.insert(QStringLiteral("time"), time);
    line.insert(QStringLiteral("text"), text);
    return line;
}

} // namespace

QVariantList LocalMediaFiles::parseLrc(const QString &contents)
{
    static const QRegularExpression timestamp(
        QStringLiteral(R"(\[(\d{1,3}):(\d{2})(?:\.(\d{1,3}))?\])"));

    QVariantList lyrics;
    const QString normalized = contents.startsWith(QChar(0xFEFF))
        ? contents.mid(1)
        : contents;
    const QStringList lines = normalized.split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                                               Qt::KeepEmptyParts);

    for (const QString &line : lines) {
        QRegularExpressionMatchIterator matches = timestamp.globalMatch(line);
        QString text = line;
        bool hasTimestamp = false;
        QList<qint64> times;
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            hasTimestamp = true;
            const qint64 minutes = match.captured(1).toLongLong();
            const qint64 seconds = match.captured(2).toLongLong();
            QString fraction = match.captured(3);
            while (fraction.size() < 3)
                fraction.append(QLatin1Char('0'));
            const qint64 milliseconds = fraction.isEmpty() ? 0 : fraction.left(3).toLongLong();
            times.append((minutes * 60 + seconds) * 1000 + milliseconds);
        }

        if (!hasTimestamp)
            continue;
        text.remove(timestamp);
        text = text.trimmed();
        if (text.isEmpty())
            continue;
        for (const qint64 time : times)
            lyrics.append(lyricLine(time, text));
    }

    std::stable_sort(lyrics.begin(), lyrics.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("time")).toLongLong()
             < right.toMap().value(QStringLiteral("time")).toLongLong();
    });
    return lyrics;
}

static QVariantList parsePlainLyrics(const QString &text)
{
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty())
        return {};

    // USLT and generic LYRICS tags are unsynchronized. Keep the complete text
    // in one item so the player can display it without pretending timestamps.
    return {lyricLine(0, cleaned)};
}

static QVariantList parseEmbeddedLyrics(const QString &filePath)
{
#ifdef Q_OS_WIN
    const auto encodedPath = filePath.toStdWString();
    TagLib::FileRef ref(encodedPath.c_str(), false);
#else
    const QByteArray encodedPath = QFile::encodeName(filePath);
    if (encodedPath.isEmpty())
        return {};

    TagLib::FileRef ref(encodedPath.constData(), false);
#endif
    if (ref.isNull() || ref.file() == nullptr)
        return {};

    // SYLT is the native ID3v2 synchronized-lyrics frame. Only millisecond
    // timestamps can be mapped to the playback position without extra data.
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(ref.file())) {
        if (auto *id3v2 = mpeg->ID3v2Tag()) {
            const auto synchronized = id3v2->frameList("SYLT");
            for (auto *frame : synchronized) {
                auto *sylt = dynamic_cast<TagLib::ID3v2::SynchronizedLyricsFrame *>(frame);
                if (!sylt || sylt->timestampFormat()
                    != TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds)
                    continue;

                QVariantList lyrics;
                for (const auto &entry : sylt->synchedText()) {
                    const QString text = tagString(entry.text).trimmed();
                    if (!text.isEmpty())
                        lyrics.append(lyricLine(entry.time, text));
                }
                if (!lyrics.isEmpty())
                    return lyrics;
            }

            const auto unsynchronized = id3v2->frameList("USLT");
            for (auto *frame : unsynchronized) {
                auto *uslt = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame);
                if (!uslt)
                    continue;
                const QString text = tagString(uslt->text());
                const QVariantList timed = LocalMediaFiles::parseLrc(text);
                if (!timed.isEmpty())
                    return timed;
                const QVariantList plain = parsePlainLyrics(text);
                if (!plain.isEmpty())
                    return plain;
            }
        }
    }

    // TagLib exposes Vorbis/FLAC, MP4, ASF and other text metadata through the
    // common property map. This also covers ID3v2 LYRICS properties not selected
    // by the frame-specific path above.
    const TagLib::PropertyMap properties = ref.properties();
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        const QString key = tagString(it->first).toUpper();
        if (!key.startsWith(QStringLiteral("LYRICS")))
            continue;
        const QString text = tagString(it->second.toString("")).trimmed();
        if (text.isEmpty())
            continue;
        const QVariantList timed = LocalMediaFiles::parseLrc(text);
        return timed.isEmpty() ? parsePlainLyrics(text) : timed;
    }
    return {};
}

static QVariantMap result(const QString &source, const QVariantList &lyrics)
{
    QVariantMap value;
    value.insert(QStringLiteral("found"), !lyrics.isEmpty());
    value.insert(QStringLiteral("source"), source);
    value.insert(QStringLiteral("lyrics"), lyrics);
    return value;
}

std::optional<QString> LocalMediaFiles::boundedPath(const QString &path,
                                                   const QString &canonicalRoot,
                                                   bool directory)
{
    const QFileInfo rootInfo(canonicalRoot);
    const QString root = rootInfo.canonicalFilePath();
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (root.isEmpty() || root != QDir::cleanPath(canonicalRoot) || !rootInfo.isDir() || canonical.isEmpty()
        || (directory ? !info.isDir() : !info.isFile()) || !info.isReadable())
        return std::nullopt;
    const QString relative = QDir(root).relativeFilePath(canonical);
    if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../"))
        return std::nullopt;
    return canonical;
}

static QString sidecarLyrics(const QString &localPath, const QString &root)
{
    const QFileInfo audioInfo(localPath);
    const auto path = LocalMediaFiles::boundedPath(audioInfo.absolutePath() + '/'
        + audioInfo.completeBaseName() + ".lrc", root);
    if (!path) return {};
    QFile sidecar(*path);
    if (!sidecar.open(QIODevice::ReadOnly)) return {};
    const QString text = QString::fromUtf8(sidecar.readAll());
    return LocalMediaFiles::parseLrc(text).isEmpty() ? QString{} : text;
}

QVariantMap LocalMediaFiles::readLyrics(const QString &filePath, const QString &canonicalRoot)
{
    const auto bounded = boundedPath(filePath, canonicalRoot);
    if (!bounded) return result("none", {});
    const QString sidecar = sidecarLyrics(*bounded, canonicalRoot);
    if (!sidecar.isEmpty()) return result("sidecar", parseLrc(sidecar));
    const QVariantList embedded = parseEmbeddedLyrics(*bounded);
    if (!embedded.isEmpty())
        return result(QStringLiteral("embedded"), embedded);
    return result(QStringLiteral("none"), {});
}

QString LocalMediaFiles::lyricsText(const QString &canonicalFile, const QString &canonicalRoot)
{
    const auto path = boundedPath(canonicalFile, canonicalRoot);
    if (!path) return {};
    const QString sidecar = sidecarLyrics(*path, canonicalRoot);
    if (!sidecar.isEmpty()) return sidecar;
    QString text;
    for (const auto &line : parseEmbeddedLyrics(*path)) {
        const auto value = line.toMap();
        const qint64 time = value.value("time").toLongLong();
        const QString timestamp = QString("[%1:%2.%3]")
            .arg(time / 60000, 2, 10, QLatin1Char('0'))
            .arg(time / 1000 % 60, 2, 10, QLatin1Char('0'))
            .arg(time % 1000, 3, 10, QLatin1Char('0'));
        for (const auto &part : value.value("text").toString().split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts))
            text += timestamp + part + '\n';
    }
    return text;
}

static QVariantMap picturePayload(const QByteArray &bytes)
{
    if (bytes.isEmpty() || QImage::fromData(bytes).isNull()) return {};
    const QString mime = QMimeDatabase().mimeTypeForData(bytes).name();
    if (!mime.startsWith("image/")) return {};
    return {{"bytes", bytes}, {"mimeType", mime}};
}

QString LocalMediaFiles::sidecarArtworkPath(const QString &canonicalFile, const QString &canonicalRoot)
{
    const auto bounded = boundedPath(canonicalFile, canonicalRoot);
    if (!bounded) return {};
    const QFileInfo info(*bounded);
    for (const auto &name : {info.completeBaseName(), QString("cover"), QString("folder"), QString("AlbumArt")}) {
        for (const auto &ext : {"jpg", "jpeg", "png", "webp", "bmp", "gif"}) {
            const auto candidate = boundedPath(info.absoluteDir().filePath(name + '.' + ext), canonicalRoot);
            if (!candidate) continue;
            QFile file(*candidate);
            if (file.open(QIODevice::ReadOnly) && !picturePayload(file.readAll()).isEmpty()) return *candidate;
        }
    }
    return {};
}

QVariantMap LocalMediaFiles::artworkPayload(const QString &canonicalFile, const QString &canonicalRoot)
{
    const auto path = boundedPath(canonicalFile, canonicalRoot);
    if (!path) return {};
#ifdef Q_OS_WIN
    const auto encoded = path->toStdWString();
    TagLib::FileRef ref(encoded.c_str(), false);
#else
    const auto encoded = QFile::encodeName(*path);
    TagLib::FileRef ref(encoded.constData(), false);
#endif
    if (!ref.isNull()) {
        for (const auto &picture : ref.complexProperties("PICTURE")) {
            const auto data = picture["data"].toByteVector();
            const auto payload = picturePayload(QByteArray(data.data(), int(data.size())));
            if (!payload.isEmpty()) return payload;
        }
    }
    const QString sidecar = sidecarArtworkPath(*path, canonicalRoot);
    if (sidecar.isEmpty()) return {};
    QFile file(sidecar);
    return file.open(QIODevice::ReadOnly) ? picturePayload(file.readAll()) : QVariantMap{};
}
