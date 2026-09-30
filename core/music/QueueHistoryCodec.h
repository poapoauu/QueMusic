#pragma once

#include "QueueHistoryTypes.h"

#include <QByteArray>
#include <optional>

class QueueHistoryCodec
{
public:
    static std::optional<QByteArray> encode(const QueueHistorySnapshot &snapshot);
    static DecodeResult decode(const QByteArray &bytes);
    static LegacyImportResult importLegacy(const QByteArray &bytes);
};
