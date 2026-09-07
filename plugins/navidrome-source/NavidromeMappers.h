#pragma once

#include "v2/SourceV2Types.h"

#include <QJsonObject>

namespace NavidromeMappers {
MediaItemV2 song(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 album(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 artist(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 playlist(const QJsonObject &value, const SourceIdentityV2 &source);
PageSectionV2 albums(PageSectionKindV2 kind, const QJsonObject &response,
                     const SourceIdentityV2 &source);
QList<PageSectionV2> starred(const QJsonObject &response,
                             const SourceIdentityV2 &source);
QList<PageSectionV2> search(const QJsonObject &response,
                            const SourceIdentityV2 &source);
}
