#include "NavidromeMappers.h"

#include <QJsonArray>

namespace {
ActionAvailabilityV2 available()
{
    return {AvailabilityV2::Available,QStringLiteral("source.capability.available"),{}};
}
MediaRefV2 ref(const QJsonObject &value,const SourceIdentityV2 &source,
               MediaEntityTypeV2 type)
{
    return {source.sourcePluginId,source.sourceInstanceId,source.accountId,type,
            value.value(QStringLiteral("id")).toString()};
}
void sourceBadge(MediaItemV2 &item,const SourceIdentityV2 &source)
{
    item.metadata.insert(QStringLiteral("sourceBadge"),source.displayName);
}
PageSectionV2 section(PageSectionKindV2 kind,QString id,QString title)
{
    PageSectionV2 result; result.kind=kind; result.sectionId=std::move(id);
    result.titleKey=std::move(title); return result;
}
template<typename Mapper>
PageSectionV2 mapped(PageSectionKindV2 kind,const QString &id,const QString &title,
                     const QJsonArray &values,const SourceIdentityV2 &source,Mapper mapper)
{
    auto result=section(kind,id,title);
    for (const auto &value:values) if (value.isObject()) result.items.append(mapper(value.toObject(),source));
    return result;
}
}

namespace NavidromeMappers {
MediaItemV2 song(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Track);
    item.title=value.value(QStringLiteral("title")).toString();
    item.subtitle=value.value(QStringLiteral("artist")).toString();
    if (!item.subtitle.isEmpty()) item.artists={item.subtitle};
    item.album=value.value(QStringLiteral("album")).toString();
    item.durationMs=value.value(QStringLiteral("duration")).toInteger()*1000;
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
    const QString isrc=value.value(QStringLiteral("isrc")).toString();
    if (!isrc.isEmpty()) item.externalIds.insert(QStringLiteral("isrc"),isrc);
    const QString mbid=value.value(QStringLiteral("musicBrainzId")).toString();
    if (!mbid.isEmpty()) item.externalIds.insert(QStringLiteral("musicBrainzRecordingId"),mbid);
    for (SourceActionV2 action:{SourceActionV2::Play,SourceActionV2::Artwork,
                                SourceActionV2::Lyrics,SourceActionV2::Download})
        item.availableActions.insert(action,available());
    sourceBadge(item,source); return item;
}
MediaItemV2 album(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Album);
    item.title=value.value(QStringLiteral("name")).toString();
    item.subtitle=value.value(QStringLiteral("artist")).toString();
    if (!item.subtitle.isEmpty()) item.artists={item.subtitle};
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
    const QString mbid=value.value(QStringLiteral("musicBrainzId")).toString();
    if (!mbid.isEmpty()) item.externalIds.insert(QStringLiteral("musicBrainzReleaseId"),mbid);
    if (!item.artworkId.isEmpty()) item.availableActions.insert(SourceActionV2::Artwork,available());
    sourceBadge(item,source); return item;
}
MediaItemV2 artist(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Artist);
    item.title=value.value(QStringLiteral("name")).toString();
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
    const QString mbid=value.value(QStringLiteral("musicBrainzId")).toString();
    if (!mbid.isEmpty()) item.externalIds.insert(QStringLiteral("musicBrainzArtistId"),mbid);
    if (!item.artworkId.isEmpty()) item.availableActions.insert(SourceActionV2::Artwork,available());
    sourceBadge(item,source); return item;
}
MediaItemV2 playlist(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Playlist);
    item.title=value.value(QStringLiteral("name")).toString();
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
    if (!item.artworkId.isEmpty()) item.availableActions.insert(SourceActionV2::Artwork,available());
    sourceBadge(item,source); return item;
}
PageSectionV2 albums(PageSectionKindV2 kind,const QJsonObject &response,
                     const SourceIdentityV2 &source)
{
    QJsonArray values=response.value(QStringLiteral("albumList2")).toObject()
                          .value(QStringLiteral("album")).toArray();
    if (values.isEmpty())
        values=response.value(QStringLiteral("artist")).toObject()
                   .value(QStringLiteral("album")).toArray();
    return mapped(kind,QString::number(int(kind)),QStringLiteral("music.section.albums"),
                  values,source,album);
}
QList<PageSectionV2> starred(const QJsonObject &response,const SourceIdentityV2 &source)
{
    const auto root=response.value(QStringLiteral("starred2")).toObject();
    return {mapped(PageSectionKindV2::FavoriteTracks,QStringLiteral("favorite-tracks"),
                   QStringLiteral("music.section.favoriteTracks"),root.value("song").toArray(),source,song),
            mapped(PageSectionKindV2::FavoriteAlbums,QStringLiteral("favorite-albums"),
                   QStringLiteral("music.section.favoriteAlbums"),root.value("album").toArray(),source,album),
            mapped(PageSectionKindV2::FavoriteArtists,QStringLiteral("favorite-artists"),
                   QStringLiteral("music.section.favoriteArtists"),root.value("artist").toArray(),source,artist)};
}
QList<PageSectionV2> search(const QJsonObject &response,const SourceIdentityV2 &source)
{
    const auto root=response.value(QStringLiteral("searchResult3")).toObject();
    auto result=section(PageSectionKindV2::SearchResults,QStringLiteral("search-results"),
                        QStringLiteral("music.section.searchResults"));
    for (const auto &value:root.value(QStringLiteral("song")).toArray()) if (value.isObject()) result.items.append(song(value.toObject(),source));
    for (const auto &value:root.value(QStringLiteral("album")).toArray()) if (value.isObject()) result.items.append(album(value.toObject(),source));
    for (const auto &value:root.value(QStringLiteral("artist")).toArray()) if (value.isObject()) result.items.append(artist(value.toObject(),source));
    return {result};
}
}
