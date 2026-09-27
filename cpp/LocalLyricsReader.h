// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
#ifndef LOCALLYRICSREADER_H
#define LOCALLYRICSREADER_H

#include <QVariantList>
#include <QVariantMap>

class LocalLyricsReader
{
public:
    // Parse standard LRC timestamps into the lyric model used by PlayerMaxCenter.
    static QVariantList parseLrc(const QString &contents);

    // Read an audio file's sidecar LRC first, then embedded metadata lyrics.
    // The result contains: found (bool), source (sidecar/embedded), lyrics (list).
    static QVariantMap read(const QString &filePath);

};

#endif // LOCALLYRICSREADER_H
