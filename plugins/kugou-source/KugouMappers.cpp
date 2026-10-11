#include "KugouMappers.h"
#include <QRegularExpression>
#include <cmath>

namespace KugouMappers {
QString canonicalHash(const QString &hash)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-fA-F]{32}\\z"));
    return pattern.match(hash).hasMatch() ? hash.toUpper() : QString();
}
std::optional<MediaItemV2> track(const QJsonObject &object, const SourceIdentityV2 &identity)
{
    const auto hash = canonicalHash(object.value("hash").toString());
    const auto title = object.value("songname").toString();
    if (hash.isEmpty() || title.trimmed().isEmpty()) return {};
    MediaItemV2 item;
    item.ref = {identity.sourcePluginId, identity.sourceInstanceId, identity.accountId,
                MediaEntityTypeV2::Track, hash};
    item.title = title;
    item.subtitle = object.value("singername").toString();
    if (!item.subtitle.isEmpty()) item.artists.append(item.subtitle);
    item.album = object.value("album_name").toString();
    const auto seconds = object.value("duration");
    const auto duration = seconds.toDouble(-1);
    if (seconds.isDouble() && std::isfinite(duration) && duration >= 0 && duration <= 7 * 86400)
        item.durationMs = qint64(duration * 1000);
    // Only explicit free tracks are candidates in the anonymous prototype.
    // Missing/unknown/string rights are never inferred to be free.
    const auto pay = object.value("pay_type");
    item.availableActions.insert(SourceActionV2::Play,
        pay.isDouble() && pay.toDouble() == 0
            ? ActionAvailabilityV2{AvailabilityV2::Available, {}, {}}
            : ActionAvailabilityV2{AvailabilityV2::Unavailable, "kugou.track.restricted", {}});
    return item;
}
std::optional<StreamDescriptorV2> stream(const QJsonObject &object, const MediaRefV2 &media)
{
    if (media.entityType != MediaEntityTypeV2::Track || media.entityId.isEmpty()
        || canonicalHash(media.entityId) != media.entityId) return {};
    if (object.contains("status") && object.value("status").toDouble(-1) != 1) return {};
    const auto data = object.value("data").isObject() ? object.value("data").toObject() : object;
    if (canonicalHash(data.value("hash").toString()) != media.entityId) return {};
    const auto pay = data.value("pay_type");
    if (!pay.isUndefined() && (!pay.isDouble() || pay.toDouble(-1) != 0)) return {};
    const QString primary = data.value("url").toString();
    const QString resource = primary.isEmpty() ? data.value("backup_url").toString() : primary;
    if (resource.isEmpty() || resource.size() > 8192) return {};
    const QUrl url(resource, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.host().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http")
        || !url.userInfo().isEmpty() || url.hasFragment()) return {};
    StreamDescriptorV2 result;
    result.media = media; result.url = url;
    // Neither platform response dictionaries nor transport credentials escape.
    return result;
}
}
