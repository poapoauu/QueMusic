#pragma once
#include "v2/SourceV2Types.h"

namespace KugouMappers {
QString canonicalHash(const QString &hash);
std::optional<MediaItemV2> track(const QJsonObject &object, const SourceIdentityV2 &identity);
std::optional<MediaItemV2> verifiedResourceTrack(const QJsonObject &object,
                                               const MediaRefV2 &media);
std::optional<StreamDescriptorV2> stream(const QJsonObject &object, const MediaRefV2 &media);
}
