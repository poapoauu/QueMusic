#include "PageCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <algorithm>

namespace {
QMutex diskMutex;
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
QJsonObject allowedMap(const QVariantMap &map, const QStringList &strings,
                       const QStringList &numbers = {})
{
    QJsonObject out;
    for (const auto &key : strings) {
        const auto value = map.value(key);
        if (value.metaType().id() == QMetaType::QString) out.insert(key,value.toString());
    }
    for (const auto &key : numbers) {
        const auto value = map.value(key);
        switch (value.metaType().id()) {
        case QMetaType::Int: case QMetaType::LongLong: case QMetaType::Double:
        case QMetaType::UInt: case QMetaType::ULongLong:
            out.insert(key,QJsonValue::fromVariant(value)); break;
        default: break;
        }
    }
    return out;
}
QJsonObject itemJson(const MediaItemV2 &i)
{
    QJsonObject actions;
    for (auto it=i.availableActions.begin(); it!=i.availableActions.end(); ++it)
        actions.insert(QString::number(int(it.key())),QJsonObject{{"state",int(it->state)}, {"reasonKey",it->reasonKey}});
    return {{"ref",QJsonObject::fromVariantMap(mediaRefV2ToVariantMap(i.ref))},
            {"title",i.title},{"subtitle",i.subtitle},{"artists",QJsonArray::fromStringList(i.artists)},
            {"album",i.album},{"durationMs",i.durationMs},
            {"artworkId",i.artworkId.contains(":") || i.artworkId.contains('?') ? QString{} : i.artworkId},
            {"externalIds",allowedMap(i.externalIds,{"isrc","musicBrainzRecordingId"})},
            {"metadata",allowedMap(i.metadata,{"genre"},{"year","trackNumber","discNumber","rating","bitRate","sampleRate"})},
            {"actions",actions}};
}
QJsonObject pageJson(const PageResultV2 &page)
{
    QJsonArray sections;
    for (const auto &s : page.sections) {
        QJsonArray items;
        for (const auto &i : s.items) items.append(itemJson(i));
        sections.append(QJsonObject{{"id",s.sectionId},{"title",s.titleKey},{"kind",int(s.kind)},
                                   {"layout",s.layoutHint},{"items",items}});
    }
    QJsonObject states;
    for (auto it=page.sourceStates.begin(); it!=page.sourceStates.end(); ++it) {
        QJsonObject state{{"state",int(it->state)}};
        // Error detail and arbitrary plugin strings are deliberately not persisted.
        if (it->error) state.insert("errorKind",int(it->error->kind));
        states.insert(it.key(),state);
    }
    return {{"sections",sections},{"sourceStates",states}};
}
std::optional<PageResultV2> readPage(const QJsonObject &object)
{
    if (!object.value("sections").isArray() || !object.value("sourceStates").isObject()) return {};
    PageResultV2 page;
    for (const auto &value : object.value("sections").toArray()) {
        if (!value.isObject()) return {};
        const auto s=value.toObject();
        if (!s.value("items").isArray() || !s.value("kind").isDouble()
            || s.value("kind").toInt(-1)<0 || s.value("kind").toInt()>int(PageSectionKindV2::SearchResults)) return {};
        PageSectionV2 section;
        section.sectionId=s.value("id").toString(); section.titleKey=s.value("title").toString();
        section.kind=PageSectionKindV2(s.value("kind").toInt()); section.layoutHint=s.value("layout").toString();
        for (const auto &iv : s.value("items").toArray()) {
            if (!iv.isObject() || !iv.toObject().value("ref").isObject()) return {};
            const auto i=iv.toObject();
            const auto ref=i.value("ref").toObject();
            if (ref.value("entityType").toInt(-1)<0 || ref.value("entityType").toInt()>int(MediaEntityTypeV2::Directory)) return {};
            MediaItemV2 item;
            item.ref=mediaRefV2FromVariantMap(ref.toVariantMap());
            item.title=i.value("title").toString(); item.subtitle=i.value("subtitle").toString();
            for (auto artist : i.value("artists").toArray()) if (artist.isString()) item.artists.append(artist.toString());
            item.album=i.value("album").toString(); item.durationMs=i.value("durationMs").toInteger();
            item.artworkId=i.value("artworkId").toString();
            item.externalIds=allowedMap(i.value("externalIds").toObject().toVariantMap(),{"isrc","musicBrainzRecordingId"}).toVariantMap();
            item.metadata=allowedMap(i.value("metadata").toObject().toVariantMap(),{"genre"},{"year","trackNumber","discNumber","rating","bitRate","sampleRate"}).toVariantMap();
            const auto actions=i.value("actions").toObject();
            for (auto it=actions.begin();it!=actions.end();++it) {
                bool ok=false; int action=it.key().toInt(&ok);
                int state=it.value().toObject().value("state").toInt(-1);
                if (ok && action>=0 && action<=int(SourceActionV2::DeleteBookmark)
                    && state>=0 && state<=int(AvailabilityV2::Forbidden))
                    item.availableActions.insert(SourceActionV2(action),{AvailabilityV2(state),it.value().toObject().value("reasonKey").toString(),{}});
            }
            section.items.append(item);
        }
        page.sections.append(section);
    }
    const auto states=object.value("sourceStates").toObject();
    for (auto it=states.begin();it!=states.end();++it) {
        auto s=it.value().toObject(); int state=s.value("state").toInt(-1);
        if (state<0 || state>int(SourcePageLoadStateV2::Failed)) return {};
        SourcePageStateV2 source{SourcePageLoadStateV2(state),{}};
        if (s.contains("errorKind")) {
            int kind=s.value("errorKind").toInt(-1);
            if (kind<0 || kind>int(SourceErrorKindV2::Unsupported)) return {};
            source.error=SourceErrorV2{SourceErrorKindV2(kind)};
        }
        page.sourceStates.insert(it.key(),source);
    }
    return page;
}
QByteArray keyBytes(const PageCacheKeyV2 &key, bool includeCursor)
{
    auto sources=key.sourceInstanceIds; sources.sort(); sources.removeDuplicates();
    const auto &q=key.query;
    // Keys are hashed only. Unknown/non-primitive filter values disable disk
    // caching in the repository; they are not converted via QObject serializers.
    QJsonObject filters;
    for (auto it=q.filters.begin();it!=q.filters.end();++it) {
        switch (it->metaType().id()) {
        case QMetaType::QString: case QMetaType::Bool: case QMetaType::Int:
        case QMetaType::LongLong: case QMetaType::Double:
            filters.insert(it.key(),QJsonValue::fromVariant(*it)); break;
        default: filters.insert(it.key(),QStringLiteral("unsupported-filter-type")); break;
        }
    }
    return QJsonDocument(QJsonObject{{"page",int(q.page)},{"section",int(q.section)},
        {"scope",q.scope.sourceInstanceId},{"sources",QJsonArray::fromStringList(sources)},
        {"text",q.searchText},{"filters",filters},{"cursor",includeCursor?q.cursor:QString{}},
        {"limit",q.limit}}).toJson(QJsonDocument::Compact);
}
}

QThreadPool *musicCacheIoPool()
{
    static QThreadPool pool;
    static const bool configured=[] { pool.setMaxThreadCount(1); return true; }();
    Q_UNUSED(configured)
    return &pool;
}
PageCache::PageCache(QString directory)
    : m_directory(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/music-pages-v2":std::move(directory)) {}
QString PageCache::filePath(const PageCacheKeyV2 &key) const { return QDir(m_directory).filePath(digest(keyBytes(key,true))+".json"); }
QString PageCache::queryScope(const PageCacheKeyV2 &key) { return digest(keyBytes(key,false)); }
PageResultV2 PageCache::sanitized(const PageResultV2 &page) { return readPage(pageJson(page)).value_or(PageResultV2{}); }

std::optional<CachedPageV2> PageCache::lookup(const PageCacheKeyV2 &key,QDateTime now,
                                            std::chrono::seconds maxAge) const
{
    QMutexLocker lock(&diskMutex);
    QFile file(filePath(key));
    if (!file.open(QIODevice::ReadOnly) || file.size()>16*1024*1024) return {};
    auto doc=QJsonDocument::fromJson(file.readAll());
    auto obj=doc.object();
    if (!doc.isObject() || obj.value("version")!=QJsonValue(1)) return {};
    const auto time=QDateTime::fromString(obj.value("storedAt").toString(),Qt::ISODateWithMs);
    auto page=readPage(obj.value("page").toObject());
    if (!time.isValid() || !page) return {};
    page->cached=true; page->complete=false;
    return CachedPageV2{*page,true,time.secsTo(now)>maxAge.count() || time>now,time};
}
bool PageCache::store(const PageCacheKeyV2 &key,const PageResultV2 &page,QDateTime storedAt)
{
    QMutexLocker lock(&diskMutex);
    if (!storedAt.isValid() || !QDir().mkpath(m_directory)) return false;
    QJsonArray sources;
    for (const auto &source:key.sourceInstanceIds) sources.append(digest(source.toUtf8()));
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"storedAt",storedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"sources",sources},{"page",pageJson(page)}}).toJson(QJsonDocument::Compact);
    if (bytes.size()>16*1024*1024) return false;
    QSaveFile file(filePath(key));
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
void PageCache::invalidateSource(const QString &source)
{
    QMutexLocker lock(&diskMutex);
    const auto key=digest(source.toUtf8());
    QDir dir(m_directory);
    for (const auto &name:dir.entryList({"*.json"},QDir::Files)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto obj=QJsonDocument::fromJson(file.read(16*1024*1024)).object();
        file.close();
        if (obj.value("sources").toArray().contains(key)) file.remove();
    }
}
