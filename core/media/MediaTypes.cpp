#include "MediaTypes.h"

QVariantMap mediaIdToVariantMap(const MediaId &id)
{
    return {
        {QStringLiteral("sourceId"), id.sourceId},
        {QStringLiteral("accountId"), id.accountId},
        {QStringLiteral("nativeId"), id.nativeId},
        {QStringLiteral("kind"), static_cast<int>(id.kind)},
    };
}

MediaId mediaIdFromVariantMap(const QVariantMap &map)
{
    return {
        map.value(QStringLiteral("sourceId")).toString(),
        map.value(QStringLiteral("accountId")).toString(),
        map.value(QStringLiteral("nativeId")).toString(),
        static_cast<MediaKind>(map.value(QStringLiteral("kind")).toInt()),
    };
}

QVariantMap mediaItemToVariantMap(const MediaItem &item)
{
    return {
        {QStringLiteral("sourceId"), item.id.sourceId},
        {QStringLiteral("accountId"), item.id.accountId},
        {QStringLiteral("nativeId"), item.id.nativeId},
        {QStringLiteral("kind"), static_cast<int>(item.id.kind)},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("subtitle"), item.subtitle},
        {QStringLiteral("artists"), item.artists},
        {QStringLiteral("albumTitle"), item.albumTitle},
        {QStringLiteral("durationMs"), item.durationMs},
        {QStringLiteral("artworkUrl"), item.artworkUrl},
        {QStringLiteral("playable"), item.playable},
        {QStringLiteral("container"), item.container},
        {QStringLiteral("extra"), item.extra},
    };
}
