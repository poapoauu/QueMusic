// SPDX-License-Identifier: Apache-2.0
#include "LocalLyricsReader.h"
#include "core/local-media/LocalMediaFiles.h"
#include <QFileInfo>
#include <QUrl>

QVariantList LocalLyricsReader::parseLrc(const QString &contents)
{
    return LocalMediaFiles::parseLrc(contents);
}

QVariantMap LocalLyricsReader::read(const QString &filePath)
{
    const QUrl url(filePath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : filePath;
    return LocalMediaFiles::readLyrics(path, QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath());
}
