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
bool playQueue(const QJsonObject &response, const SourceIdentityV2 &source, QVariantMap *payload)
{
    const auto value=response.value("playQueue");
    if (!value.isUndefined() && !value.isObject()) return false;
    const auto queue=value.toObject();
    const auto entries=queue.value("entry");
    if (!entries.isUndefined() && !entries.isArray()) return false;
    QVariantList items; QStringList ids;
    for (const auto &entry:entries.toArray()) {
        if (!entry.isObject()) return false;
        const auto media=ref(entry.toObject(),source,MediaEntityTypeV2::Track);
        if (media.entityId.trimmed().isEmpty()) return false;
        ids.append(media.entityId); items.append(mediaRefV2ToVariantMap(media));
    }
    const auto position=queue.value("position");
    const qint64 ms=position.isUndefined()?0:position.toInteger(-1);
    if (ms<0) return false;
    const auto currentValue=queue.value("current");
    if (!currentValue.isUndefined() && !currentValue.isString()) return false;
    const QString current=currentValue.toString();
    if ((!current.isEmpty() && !ids.contains(current)) || (items.isEmpty() && ms!=0)) return false;
    QVariantMap currentRef;
    if (!current.isEmpty()) currentRef=mediaRefV2ToVariantMap(
        {source.sourcePluginId,source.sourceInstanceId,source.accountId,MediaEntityTypeV2::Track,current});
    *payload={{"items",items},{"current",currentRef},{"positionMs",ms}};
    return true;
}
bool bookmarks(const QJsonObject &response, const SourceIdentityV2 &source, QVariantMap *payload)
{
    const auto root=response.value("bookmarks");
    if (!root.isObject()) return false;
    const auto entries=root.toObject().value("bookmark");
    if (!entries.isUndefined() && !entries.isArray()) return false;
    QVariantList bookmarks;
    for (const auto &entry:entries.toArray()) {
        if (!entry.isObject()) return false;
        const auto bookmark=entry.toObject();
        if (!bookmark.value("entry").isObject()) return false;
        const auto media=ref(bookmark.value("entry").toObject(),source,MediaEntityTypeV2::Track);
        const qint64 position=bookmark.value("position").toInteger(-1);
        const auto comment=bookmark.value("comment");
        if (media.entityId.trimmed().isEmpty() || position<0
            || (!comment.isUndefined() && !comment.isString())) return false;
        bookmarks.append(QVariantMap{{"media",mediaRefV2ToVariantMap(media)},
            {"positionMs",position},{"comment",comment.toString()}});
    }
    *payload={{"bookmarks",bookmarks}};
    return true;
}

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
    for (SourceActionV2 action:{SourceActionV2::Play,SourceActionV2::Lyrics,
                                SourceActionV2::Download,SourceActionV2::Favorite,
                                SourceActionV2::Unfavorite,SourceActionV2::Rating,
                                SourceActionV2::CreateBookmark,SourceActionV2::DeleteBookmark})
        item.availableActions.insert(action,available());
    item.availableActions.insert(SourceActionV2::AddPlaylistTracks,
        {AvailabilityV2::Available,{},{{"sameSourceOnly",true}}});
    if (!item.artworkId.isEmpty()) item.availableActions.insert(SourceActionV2::Artwork,available());
    sourceBadge(item,source); return item;
}
MediaItemV2 album(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Album);
    item.availableActions.insert(SourceActionV2::Favorite,available());
    item.availableActions.insert(SourceActionV2::Unfavorite,available());
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
    item.availableActions.insert(SourceActionV2::Favorite,available());
    item.availableActions.insert(SourceActionV2::Unfavorite,available());
    item.title=value.value(QStringLiteral("name")).toString();
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
    const QString mbid=value.value(QStringLiteral("musicBrainzId")).toString();
    if (!mbid.isEmpty()) item.externalIds.insert(QStringLiteral("musicBrainzArtistId"),mbid);
    sourceBadge(item,source); return item;
}
MediaItemV2 playlist(const QJsonObject &value,const SourceIdentityV2 &source)
{
    MediaItemV2 item; item.ref=ref(value,source,MediaEntityTypeV2::Playlist);
    for (const auto action:{SourceActionV2::UpdatePlaylist,SourceActionV2::DeletePlaylist,
                           SourceActionV2::RemovePlaylistTracks}) item.availableActions.insert(action,available());
    item.availableActions.insert(SourceActionV2::AddPlaylistTracks,
        {AvailabilityV2::Available,{},{{"sameSourceOnly",true}}});
    item.title=value.value(QStringLiteral("name")).toString();
    item.artworkId=value.value(QStringLiteral("coverArt")).toString();
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
    return {mapped(PageSectionKindV2::Tracks,QStringLiteral("search-tracks"),
                   QStringLiteral("music.section.tracks"),root.value("song").toArray(),source,song),
            mapped(PageSectionKindV2::Albums,QStringLiteral("search-albums"),
                   QStringLiteral("music.section.albums"),root.value("album").toArray(),source,album),
            mapped(PageSectionKindV2::Artists,QStringLiteral("search-artists"),
                   QStringLiteral("music.section.artists"),root.value("artist").toArray(),source,artist)};
}
}
