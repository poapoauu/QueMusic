#pragma once

#include <QFlags>

enum class PlaybackCapability : quint64 {
    None = 0,
    Audio = 1ull << 0,
    Video = 1ull << 1,
    Seek = 1ull << 2,
    Volume = 1ull << 3,
};
Q_DECLARE_FLAGS(PlaybackCapabilities, PlaybackCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(PlaybackCapabilities)
