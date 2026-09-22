#pragma once

#include "v2/SourceV2Types.h"

#include <QJsonObject>

namespace NavidromeMappers {
bool playQueue(const QJsonObject &response, const SourceIdentityV2 &source, QVariantMap *payload);
bool bookmarks(const QJsonObject &response, const SourceIdentityV2 &source, QVariantMap *payload);
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
