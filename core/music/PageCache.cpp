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
#include <QUrl>
#include <algorithm>
#include <climits>
#include <cmath>
#include <list>

namespace {
QMutex diskMutex;
QMutex memoryRegistryMutex;
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
QJsonObject allowedMetadata(const QVariantMap &map)
{
    QJsonObject out=allowedMap(map,{"genre"},{"year","trackNumber","discNumber","rating","bitRate","sampleRate"});
    if (map.value("collectionKind").metaType().id() == QMetaType::QString
        && map.value("collectionKind").toString() == QStringLiteral("chart"))
        out.insert(QStringLiteral("collectionKind"), QStringLiteral("chart"));
    const QVariant playlistId=map.value(QStringLiteral("playlistId"));
    const QVariant playlistIndex=map.value(QStringLiteral("playlistIndex"));
    if (playlistId.metaType().id()==QMetaType::QString && !playlistId.toString().isEmpty()
        && playlistIndex.metaType().id()==QMetaType::Int && playlistIndex.toInt()>=0) {
        out.insert(QStringLiteral("playlistId"),playlistId.toString());
        out.insert(QStringLiteral("playlistIndex"),playlistIndex.toInt());
    }
    return out;
}
QVariantMap readMetadata(const QJsonObject &object)
{
    QVariantMap out=allowedMap(object.toVariantMap(),{"genre"},{"year","trackNumber","discNumber","rating","bitRate","sampleRate"}).toVariantMap();
    if (object.value("collectionKind").isString()
        && object.value("collectionKind").toString() == QStringLiteral("chart"))
        out.insert(QStringLiteral("collectionKind"), QStringLiteral("chart"));
    const QJsonValue playlistId=object.value(QStringLiteral("playlistId"));
    const QJsonValue playlistIndex=object.value(QStringLiteral("playlistIndex"));
    const double index=playlistIndex.toDouble(-1);
    if (playlistId.isString() && !playlistId.toString().isEmpty() && playlistIndex.isDouble()
        && std::isfinite(index) && std::floor(index)==index && index>=0 && index<=INT_MAX) {
        out.insert(QStringLiteral("playlistId"),playlistId.toString());
        out.insert(QStringLiteral("playlistIndex"),int(index));
    }
    return out;
}
ActionAvailabilityV2 safeAction(const ActionAvailabilityV2 &input)
{
    ActionAvailabilityV2 result{input.state,input.reasonKey,{}};
    bool safe=true;
    for (auto it=input.constraints.cbegin();it!=input.constraints.cend();++it) {
        if (it.key()==QStringLiteral("sameSourceOnly") && it->metaType().id()==QMetaType::Bool) {
            result.constraints.insert(it.key(),it->toBool());
        } else if (it.key()==QStringLiteral("maxBitrate")) {
            bool numeric=false;
            switch (it->metaType().id()) {
            case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
            case QMetaType::ULongLong: case QMetaType::Float: case QMetaType::Double:
                numeric=true; break;
            default: break;
            }
            const double value=numeric?it->toDouble():-1;
            // Preserve only finite nonnegative numbers in the exact JSON-safe
            // range. Strings, booleans, objects and nested maps are not numbers.
            if (numeric && std::isfinite(value) && value>=0 && value<=9007199254740991.0)
                result.constraints.insert(it.key(),value);
            else safe=false;
        } else safe=false;
    }
    if (!safe) {
        result.state=AvailabilityV2::Unavailable;
        result.reasonKey=QStringLiteral("music.actionConstraintsUnsupported");
    }
    return result;
}
ActionAvailabilityV2 readAction(const QJsonObject &object)
{
    ActionAvailabilityV2 action{AvailabilityV2(object.value("state").toInt()),object.value("reasonKey").toString(),{}};
    const auto value=object.value("constraints");
    const auto constraints=value.toObject();
    bool safe=value.isObject();
    // Never hydrate arbitrary maps from disk: convert only the two scalar
    // restriction types, and fail closed if anything else was present.
    for (auto it=constraints.begin();it!=constraints.end();++it) {
        if (it.key()==QStringLiteral("sameSourceOnly") && it->isBool())
            action.constraints.insert(it.key(),it->toBool());
        else if (it.key()==QStringLiteral("maxBitrate") && it->isDouble())
            action.constraints.insert(it.key(),it->toDouble());
        else safe=false;
    }
    action=safeAction(action);
    if (!safe) {
        action.state=AvailabilityV2::Unavailable;
        action.reasonKey=QStringLiteral("music.actionConstraintsUnsupported");
    }
    return action;
}
QJsonObject itemJson(const MediaItemV2 &i)
{
    QJsonObject actions;
    for (auto it=i.availableActions.begin(); it!=i.availableActions.end(); ++it) {
        const auto action=safeAction(*it);
        actions.insert(QString::number(int(it.key())),QJsonObject{{"state",int(action.state)},
            {"reasonKey",action.reasonKey},{"constraints",QJsonObject::fromVariantMap(action.constraints)}});
    }
    return {{"ref",QJsonObject::fromVariantMap(mediaRefV2ToVariantMap(i.ref))},
            {"title",i.title},{"subtitle",i.subtitle},{"artists",QJsonArray::fromStringList(i.artists)},
            {"album",i.album},{"durationMs",i.durationMs},
            {"artworkId",i.artworkId.contains(":") || i.artworkId.contains('?') ? QString{} : i.artworkId},
            {"externalIds",allowedMap(i.externalIds,{"isrc","musicBrainzRecordingId"})},
            {"metadata",allowedMetadata(i.metadata)},
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
bool isDirectLocator(const QString &id)
{
    if (QDir::isAbsolutePath(id) || id.startsWith(QStringLiteral("\\\\"))) return true;
    if (id.size()>=3 && id[0].isLetter() && id[1]==QLatin1Char(':')
        && (id[2]==QLatin1Char('/') || id[2]==QLatin1Char('\\'))) return true;
    const QUrl url(id);
    return url.isValid() && (url.isLocalFile()
        || url.scheme().compare(QStringLiteral("file"),Qt::CaseInsensitive)==0
        || (!url.scheme().isEmpty()
            && id.startsWith(url.scheme()+QStringLiteral("://"),Qt::CaseInsensitive)));
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
            if (isDirectLocator(item.ref.entityId)) return {};
            item.title=i.value("title").toString(); item.subtitle=i.value("subtitle").toString();
            for (auto artist : i.value("artists").toArray()) if (artist.isString()) item.artists.append(artist.toString());
            item.album=i.value("album").toString(); item.durationMs=i.value("durationMs").toInteger();
            item.artworkId=i.value("artworkId").toString();
            item.externalIds=allowedMap(i.value("externalIds").toObject().toVariantMap(),{"isrc","musicBrainzRecordingId"}).toVariantMap();
            item.metadata=readMetadata(i.value("metadata").toObject());
            const auto actions=i.value("actions").toObject();
            for (auto it=actions.begin();it!=actions.end();++it) {
                bool ok=false; int action=it.key().toInt(&ok);
                int state=it.value().toObject().value("state").toInt(-1);
                if (ok && action>=0 && action<=int(SourceActionV2::DeleteBookmark)
                    && state>=0 && state<=int(AvailabilityV2::Forbidden))
                    item.availableActions.insert(SourceActionV2(action),readAction(it.value().toObject()));
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

struct PageCache::Memory {
    struct Entry {
        QString path;
        CachedPageV2 page;
        QStringList sources;
    };
    QString directory;
    std::list<Entry> entries; // most recently used first
    QHash<QString,std::list<Entry>::iterator> index;
};
QList<std::weak_ptr<PageCache::Memory>> &PageCache::activeMemories()
{
    static QList<std::weak_ptr<Memory>> memories;
    return memories;
}
void PageCache::remember(const QString &path,CachedPageV2 page,const QStringList &sources) const
{
    // Caller holds diskMutex, which also serializes memory access/invalidation.
    auto old=m_memory->index.find(path);
    if (old!=m_memory->index.end()) {
        m_memory->entries.erase(old.value());
        m_memory->index.erase(old);
    }
    m_memory->entries.push_front({path,std::move(page),sources});
    m_memory->index.insert(path,m_memory->entries.begin());
    while (m_memory->entries.size()>64) {
        m_memory->index.remove(m_memory->entries.back().path);
        m_memory->entries.pop_back();
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
    : m_directory(QDir::cleanPath(QDir(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/music-pages-v2":std::move(directory)).absolutePath()))
    , m_memory(std::make_shared<Memory>())
{
    m_memory->directory=m_directory;
    // Construction may run on UI. This lock never encloses file IO; it must
    // not wait on a worker holding diskMutex during a blocking filesystem call.
    QMutexLocker lock(&memoryRegistryMutex);
    auto &memories=activeMemories();
    memories.erase(std::remove_if(memories.begin(),memories.end(),[](const auto &memory) { return memory.expired(); }),memories.end());
    memories.append(m_memory);
}
QString PageCache::filePath(const PageCacheKeyV2 &key) const { return QDir(m_directory).filePath(digest(keyBytes(key,true))+".json"); }
QString PageCache::queryScope(const PageCacheKeyV2 &key) { return digest(keyBytes(key,false)); }
PageResultV2 PageCache::sanitized(const PageResultV2 &page) { return readPage(pageJson(page)).value_or(PageResultV2{}); }

std::optional<CachedPageV2> PageCache::lookup(const PageCacheKeyV2 &key,QDateTime now,
                                            std::chrono::seconds maxAge) const
{
    QMutexLocker lock(&diskMutex);
    const auto path=filePath(key);
    auto hit=m_memory->index.find(path);
    if (hit!=m_memory->index.end()) {
        m_memory->entries.splice(m_memory->entries.begin(),m_memory->entries,hit.value());
        auto value=hit.value()->page;
        value.cached=true; value.page.cached=true; value.page.complete=false;
        value.stale=value.storedAt.secsTo(now)>maxAge.count() || value.storedAt>now;
        return value;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size()>16*1024*1024) return {};
    auto doc=QJsonDocument::fromJson(file.readAll());
    auto obj=doc.object();
    // Version 1 erased action restrictions; its Available states cannot safely
    // be upgraded. Treat old files as misses and let the repository refresh.
    if (!doc.isObject() || obj.value("version")!=QJsonValue(2)) return {};
    const auto time=QDateTime::fromString(obj.value("storedAt").toString(),Qt::ISODateWithMs);
    auto page=readPage(obj.value("page").toObject());
    if (!time.isValid() || !page) return {};
    page->cached=true; page->complete=false;
    CachedPageV2 result{*page,true,time.secsTo(now)>maxAge.count() || time>now,time};
    remember(path,result,key.sourceInstanceIds);
    return result;
}
bool PageCache::store(const PageCacheKeyV2 &key,const PageResultV2 &page,QDateTime storedAt)
{
    QMutexLocker lock(&diskMutex);
    if (!storedAt.isValid() || !QDir().mkpath(m_directory)) return false;
    QJsonArray sources;
    for (const auto &source:key.sourceInstanceIds) sources.append(digest(source.toUtf8()));
    const auto clean=pageJson(page);
    const auto bytes=QJsonDocument(QJsonObject{{"version",2},{"storedAt",storedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"sources",sources},{"page",clean}}).toJson(QJsonDocument::Compact);
    if (bytes.size()>16*1024*1024) return false;
    auto safePage=readPage(clean);
    if (!safePage) return false;
    const auto path=filePath(key);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) return false;
    remember(path,{*safePage,true,false,storedAt},key.sourceInstanceIds);
    return true;
}
void PageCache::invalidateSource(const QString &source)
{
    QMutexLocker lock(&diskMutex);
    const auto memories=[] {
        QMutexLocker registryLock(&memoryRegistryMutex);
        return activeMemories();
    }();
    for (const auto &weak:memories) {
        auto memory=weak.lock();
        if (!memory || memory->directory!=m_directory) continue;
        for (auto it=memory->entries.begin();it!=memory->entries.end();) {
            if (it->sources.contains(source)) {
                memory->index.remove(it->path);
                it=memory->entries.erase(it);
            } else ++it;
        }
    }
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
