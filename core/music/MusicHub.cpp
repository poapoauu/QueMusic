#include "MusicHub.h"
#include "MediaAssetRepository.h"
#include "DirectoryLibraryController.h"
#include "PageRepository.h"
#include "extensions/discovery/v1/IDiscoveryProviderV1.h"
#include <QDir>
#include <QSettings>
#include <QTimer>
#include <array>
#include <cmath>

namespace {
bool validPage(int page) { return page >= 0 && page <= int(MusicPageKindV2::Search); }
bool validDiscovery(int kind) { return kind >= 0 && kind <= 1; }
bool validSearchTab(int tab) { return tab >= 0 && tab <= 3; }
PageSectionKindV2 searchSectionForTab(int tab)
{
    switch (tab) {
    case 1: return PageSectionKindV2::Playlists;
    case 2: return PageSectionKindV2::Albums;
    // Providers return tracks for both the song and lyric presentation tabs;
    // lyrics are resolved only after an item is selected.
    case 3: return PageSectionKindV2::Tracks;
    default: return PageSectionKindV2::Tracks;
    }
}
QList<PageSectionKindV2> sectionsForPage(MusicPageKindV2 page)
{
    using K = PageSectionKindV2;
    switch (page) {
    case MusicPageKindV2::Recommendation: return {K::RecentlyPlayed, K::FrequentlyPlayed, K::HighestRated, K::Newest, K::Random};
    case MusicPageKindV2::Category: return {K::Genres, K::Artists, K::Albums, K::Tracks, K::Playlists};
    case MusicPageKindV2::Favorites: return {K::FavoriteTracks, K::FavoriteAlbums, K::FavoriteArtists, K::Playlists};
    case MusicPageKindV2::Search: return {K::SearchResults};
    }
    return {};
}
bool integer(const QVariant &value, int maximum)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Double: break;
    default: return false;
    }
    const double n = value.toDouble();
    return std::isfinite(n) && std::floor(n) == n && n >= 0 && n <= maximum;
}
bool parseRef(const QVariantMap &map, MediaRefV2 *ref)
{
    if (map.size() != 5) return false;
    for (auto key : {"sourcePluginId", "sourceInstanceId", "accountId", "entityId"})
        if (map.value(key).metaType().id() != QMetaType::QString || map.value(key).toString().trimmed().isEmpty()) return false;
    if (!integer(map.value("entityType"), int(MediaEntityTypeV2::Directory))) return false;
    *ref = mediaRefV2FromVariantMap(map);
    return true;
}
SourceErrorV2 hostError(const QString &key, SourceErrorKindV2 kind = SourceErrorKindV2::Unavailable)
{
    return {kind, key, {}, {}, false};
}
QVariantMap errorMap(const SourceErrorV2 &error)
{
    return {{"kind", int(error.kind)}, {"messageKey", error.messageKey},
            {"detail", QString{}}, {"retryable", error.retryable}};
}
QString cachePath(QSettings *settings, const QString &leaf)
{
    // Optional local root. Absent means the repositories' standard cache paths.
    const auto root = settings ? settings->value("MusicHub/cacheDirectory").toString() : QString{};
    return QDir::isAbsolutePath(root) ? QDir(root).filePath(leaf) : QString{};
}
}

struct MusicHub::Impl {
    struct Page {
        std::unique_ptr<MusicPageModel> model;
        quint64 generation = 0;
        quint64 revision = 0;
        bool activated = false;
        int expected = 0;
        QHash<QString, PageQueryV2> origins;
    };
    struct Pending {
        int page;
        quint64 generation;
        PageQueryV2 query;
        QString sectionId; // empty means full-refresh query
        bool append = false;
    };
    struct Context { PageQueryV2 query; QVariantMap item; };
    MusicHub *q;
    QPointer<SourceRegistry> sources;
    QPointer<SourceScopeStore> scope;
    AggregateComposer composer;
    ArtworkCache artwork;
    std::array<Page, 6> pages;
    std::unique_ptr<PageRepository> repository;
    std::unique_ptr<DirectoryLibraryController> directory;
    std::unique_ptr<MediaAssetRepository> assets;
    std::unique_ptr<MediaActionRouter> router;
    QHash<QUuid, Pending> pending;
    QHash<QUuid, MediaRefV2> assetRequests;
    QVariantList options;
    QList<Context> stack;
    QString searchText;
    int searchTab = 0;
    quint64 contextRevision = 0;
    bool synchronizing = false;

    Impl(MusicHub *owner, SourceRegistry *registry, SourceScopeStore *store, QSettings *settings)
        : q(owner), sources(registry), scope(store), artwork(cachePath(settings, "artwork-v2")),
          repository(std::make_unique<PageRepository>(registry, &composer, owner,
                     std::make_shared<PageCache>(cachePath(settings, "pages-v2")))),
          directory(std::make_unique<DirectoryLibraryController>(registry, repository.get(), owner)),
          assets(std::make_unique<MediaAssetRepository>(registry, &artwork, owner)),
          router(std::make_unique<MediaActionRouter>(registry, owner))
    {
        for (int i = 0; i < int(pages.size()); ++i)
            pages[i].model = std::make_unique<MusicPageModel>(i < 4 ? MusicPageKindV2(i) : MusicPageKindV2::Recommendation, owner);
        QObject::connect(repository.get(), &PageRepository::pageReady, q,
            [this](QUuid id, quint64 gen, PageResultV2 result) { receive(id, gen, result); });
        QObject::connect(repository.get(), &PageRepository::pageFailed, q,
            [this](QUuid id, quint64 gen, SourceErrorV2 error) { receive(id, gen, {}, error); });
        QObject::connect(assets.get(), &MediaAssetRepository::artworkReady, q,
            [this](QUuid id, MediaRefV2 media, QUrl url) {
                QTimer::singleShot(0, q, [this, id, media, url] {
                    if (!assetRequests.remove(id)) return;
                    if (!owns(media) || !url.isLocalFile() || !url.host().isEmpty())
                        emit q->assetFailed(id, errorMap(hostError("music.assetUnavailable")));
                    else emit q->artworkReady(id, mediaRefV2ToVariantMap(media), url);
                });
            });
        QObject::connect(assets.get(), &MediaAssetRepository::lyricsReady, q,
            [this](QUuid id, MediaRefV2 media, QString lyrics) {
                QTimer::singleShot(0, q, [this, id, media, lyrics] {
                    if (!assetRequests.remove(id)) return;
                    if (!owns(media)) emit q->assetFailed(id, errorMap(hostError("music.assetUnavailable")));
                    else emit q->lyricsReady(id, mediaRefV2ToVariantMap(media), lyrics);
                });
            });
        QObject::connect(assets.get(), &MediaAssetRepository::failed, q,
            [this](QUuid id, SourceErrorV2 error) { assetFailure(id, error); });
        if (scope) QObject::connect(scope, &SourceScopeStore::selectedSourceInstanceIdChanged, q, [this] {
            stack.clear();
            emit q->categoryContextChanged();
            options = makeOptions();
            emit q->sourceOptionsChanged();
            resetPages();
            emit q->selectedSourceInstanceIdChanged();
        });
        if (sources) {
            QObject::connect(sources, &SourceRegistry::instanceContentChanged, q,
                [this](const QString &source, quint64) {
                    const QPointer<MusicHub> guard(q);
                    for (int i = 0; i < int(pages.size()); ++i) {
                        const auto query = baseQuery(i);
                        if (pages[i].activated && (query.scope.isAggregate()
                            || query.scope.sourceInstanceId == source)) refresh(i);
                        if (!guard) return;
                    }
                });
            QObject::connect(sources, &SourceRegistry::instanceCapabilitiesChanged, q,
                [this](const QString &source) {
                    const QPointer<MusicHub> guard(q);
                    const auto initialScope = selected();
                    for (int i = 4; i < int(pages.size()); ++i) {
                        if (!selected().isEmpty() && selected() != source) continue;
                        const bool active = pages[i].activated;
                        const auto revision = invalidate(i, true); // discard old private rows immediately
                        if (!guard || selected() != initialScope) return;
                        if (active) QTimer::singleShot(0, q, [this, i, revision] {
                            if (pages[i].activated && pages[i].revision == revision) refresh(i);
                        });
                    }
                });
            QObject::connect(sources, &SourceRegistry::instanceRefreshFailed, q,
                [this](const QString &source, const SourceErrorV2 &) {
                    if (selected().isEmpty() || selected() == source)
                        emit q->sourceRefreshFailed(source, QStringLiteral("source.instance.refreshFailed"));
                });
            QObject::connect(sources, &SourceRegistry::instanceChanged, q, [this] { synchronizeSources(); });
            QObject::connect(sources, &QObject::destroyed, q, [this] { synchronizeSources(); });
            if (sources->pluginManager()) QObject::connect(sources->pluginManager(), &PluginManager::pluginChanged,
                q, [this] { synchronizeSources(); });
        }
        options = makeOptions();
    }
    QString selected() const { return scope ? scope->selectedSourceInstanceId() : QString{}; }
    QString unavailable(const QString &id) const
    {
        if (id.isEmpty()) return {};
        if (sources) for (const auto &instance : sources->enabledInstances()) {
            if (instance.sourceInstanceId != id) continue;
            if (!instance.enabled) return "source.instance.disabled";
            if (!sources->pluginManager() || sources->pluginManager()->plugin(instance.pluginPackageId).state != PluginState::Loaded)
                return "source.instance.unavailable";
            return {};
        }
        return "source.instance.removed";
    }
    bool owns(const MediaRefV2 &ref) const
    {
        if (!sources || !unavailable(ref.sourceInstanceId).isEmpty()) return false;
        for (const auto &instance : sources->enabledInstances())
            if (instance.sourceInstanceId == ref.sourceInstanceId)
                return instance.enabled && instance.sourceId == ref.sourcePluginId && instance.accountId == ref.accountId;
        return false;
    }
    QVariantList makeOptions() const
    {
        QVariantList result{QVariantMap{{"sourceInstanceId", ""}, {"displayName", QStringLiteral("全部音源")}, {"available", true}}};
        bool included = selected().isEmpty();
        if (sources) for (const auto &instance : sources->enabledInstances()) {
            if (!instance.enabled && instance.sourceInstanceId != selected()) continue;
            const auto reason = unavailable(instance.sourceInstanceId);
            result.append(QVariantMap{{"sourceInstanceId", instance.sourceInstanceId}, {"displayName", instance.displayName},
                                      {"available", reason.isEmpty()}, {"reasonKey", reason}});
            included |= instance.sourceInstanceId == selected();
        }
        if (!included) result.append(QVariantMap{{"sourceInstanceId", selected()}, {"displayName", selected()},
            {"available", false}, {"reasonKey", "source.instance.removed"}});
        return result;
    }
    void synchronizeSources()
    {
        // Creation/open notifications are not query changes. Compare membership,
        // enablement and plugin availability, excluding transient session state.
        if (synchronizing) {
            QTimer::singleShot(0, q, [this] { synchronizeSources(); });
            return;
        }
        const auto next = makeOptions();
        if (next == options) return;
        synchronizing = true;
        options = next; // publish before callback-capable cancellation
        resetPages();
        synchronizing = false;
        emit q->sourceOptionsChanged();
    }
    quint64 invalidate(int index, bool reset)
    {
        auto &page = pages[index];
        const auto revision = ++page.revision;
        const auto old = page.generation;
        ++page.generation;
        if (reset) page.origins.clear();
        QList<QUuid> ids;
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->page == index) { ids.append(it.key()); it = pending.erase(it); }
            else ++it;
        }
        if (reset) page.model->resetGeneration(old);
        else page.model->cancelGeneration(old);
        // No pending-map iterator survives provider cancel()/registry callbacks.
        for (const auto &id : ids) repository->cancel(id);
        return revision;
    }
    void resetPages()
    {
        const auto revision = ++contextRevision;
        for (int i = 0; i < int(pages.size()); ++i) {
            invalidate(i, true);
            if (revision != contextRevision) return;
        }
        for (int i = 0; i < int(pages.size()); ++i) {
            if (pages[i].activated) refresh(i);
            if (revision != contextRevision) return;
        }
    }
    PageQueryV2 baseQuery(int index) const
    {
        if (index >= 4) return discoveryQueryV1(DiscoveryKindV1(index-4), {selected()});
        if (index == 1 && !stack.isEmpty()) return stack.last().query;
        PageQueryV2 query;
        query.page = MusicPageKindV2(index); query.scope.sourceInstanceId = selected();
        if (index == 3) {
            query.searchText = searchText;
            query.section = searchSectionForTab(searchTab);
        }
        return query;
    }
    void refresh(int index)
    {
        auto &page = pages[index];
        const auto revision = invalidate(index, index >= 4); // personalized refresh never keeps stale cached rows
        if (page.revision != revision) return;
        const auto query = baseQuery(index);
        const auto sections = index >= 4 || (index == 1 && !stack.isEmpty()) ? QList<PageSectionKindV2>{query.section}
            : index == int(MusicPageKindV2::Search) ? QList<PageSectionKindV2>{query.section}
                                                     : sectionsForPage(query.page);
        page.expected = sections.size();
        // Publish the expected generation before beginRequest emits Loading:
        // QML can synchronously cancel, refresh or change the shared scope.
        const auto generation = ++page.generation;
        page.model->beginRequest();
        if (page.revision != revision || page.generation != generation) return;
        const auto reason = unavailable(query.scope.sourceInstanceId);
        if (!reason.isEmpty()) {
            page.expected = 1;
            page.model->applyFailure(generation, hostError(reason));
            page.model->finishGeneration(generation, 1);
            return;
        }
        for (auto section : sections) {
            if (page.generation != generation) return;
            auto request = query; request.section = section;
            const auto id = repository->requestPage(request, generation);
            pending.insert(id, {index, generation, request, {}, false});
        }
    }
    void receive(const QUuid &id, quint64 generation, const PageResultV2 &result,
                 std::optional<SourceErrorV2> failure = {})
    {
        const auto it = pending.constFind(id);
        if (it == pending.cend() || it->generation != generation) return;
        const auto request = it.value();
        auto &page = pages[request.page];
        if (page.generation != generation) return;
        const bool terminal = failure.has_value() || (!result.cached && result.complete);
        if (terminal) pending.remove(id); // one terminal per host ID, before emitting model signals
        if (!request.sectionId.isEmpty()) {
            if (!terminal) return;
            if (failure) page.model->applySectionFailure(generation, request.sectionId, *failure);
            else {
                // A failed initial query has a host ID before the provider has
                // supplied a row ID. Keep that rendered identity on retry and
                // continuation; repository validates unique typed section kinds.
                auto scoped = result;
                scoped.sections.clear();
                for (auto section : result.sections) {
                    if (section.kind != request.query.section) continue;
                    section.sectionId = request.sectionId;
                    scoped.sections.append(section);
                }
                page.model->applySectionResult(generation, request.sectionId, scoped, request.append);
            }
            return;
        }
        if (failure) {
            PageSectionV2 section;
            section.kind = request.query.section;
            section.sectionId = "music.queryFailure." + QString::number(int(section.kind));
            section.titleKey = "music.section.failed";
            QSet<QString> existingIds;
            bool existingKind = false;
            for (int i = 0; i < page.model->rowCount(); ++i) {
                const auto existing = page.model->section(i);
                existingIds.insert(existing.sectionId);
                if (existing.kind == section.kind) { section = existing; existingKind = true; break; }
            }
            if (!existingKind) while (existingIds.contains(section.sectionId)) section.sectionId += '#';
            page.origins.insert(section.sectionId, request.query);
            page.model->applyQueryFailure(generation, section, *failure);
        }
        else {
            for (const auto &section : result.sections) {
                auto origin = request.query; origin.section = section.kind; origin.cursor.clear();
                page.origins.insert(section.sectionId, origin);
            }
            page.model->applyResult(generation, result);
        }
        if (page.generation == generation && terminal) page.model->finishGeneration(generation, page.expected);
    }
    void sectionRequest(int index, const QString &id, bool append)
    {
        auto &page = pages[index];
        QString sectionId = id;
        // Original list controls did not carry a section identity. Prefer their
        // row-provided IDs, but keep the one-section fallback working for an
        // empty/error model where no presentation row exists yet.
        if (sectionId.isEmpty()) {
            for (int i = 0; i < page.model->rowCount(); ++i) {
                const auto section = page.model->section(i);
                const bool retryable = !page.model->data(page.model->index(i), MusicPageModel::ErrorRole)
                                            .toMap().isEmpty();
                if ((!append && retryable) || (append && section.hasMore
                                                && !section.nextCursor.isEmpty())) {
                    sectionId = section.sectionId;
                    break;
                }
            }
        }
        if (!page.origins.contains(sectionId)) return;
        auto query = page.origins.value(sectionId);
        for (int i = 0; i < page.model->rowCount(); ++i) {
            const auto section = page.model->section(i);
            if (section.sectionId != sectionId) continue;
            if (append && (!section.hasMore || section.nextCursor.isEmpty())) return;
            query.section = section.kind;
            query.cursor = append ? section.nextCursor : QString{};
            const auto generation = page.generation;
            if (!page.model->beginSectionRequest(generation, sectionId) || page.generation != generation) return;
            const auto requestId = repository->requestPage(query, generation);
            pending.insert(requestId, {index, generation, query, sectionId, append});
            return;
        }
    }
    void assetFailure(QUuid id, SourceErrorV2 error)
    {
        QTimer::singleShot(0, q, [this, id, error] {
            if (assetRequests.remove(id)) emit q->assetFailed(id, errorMap(error));
        });
    }
    QUuid asset(const QVariantMap &map, bool lyrics)
    {
        MediaRefV2 ref;
        if (!parseRef(map, &ref) || !owns(ref)) {
            const auto id = QUuid::createUuid(); assetRequests.insert(id, ref);
            assetFailure(id, hostError("music.assetInvalidRequest", SourceErrorKindV2::InvalidRequest));
            return id;
        }
        const auto id = lyrics ? assets->requestLyrics(ref) : assets->requestArtwork(ref);
        assetRequests.insert(id, ref);
        return id;
    }
};

MusicHub::MusicHub(SourceRegistry *sources, SourceScopeStore *scope, QSettings *settings, QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>(this, sources, scope, settings)) {}
MusicHub::~MusicHub()
{
    // Detach inbound callbacks before destructors cancel borrowed providers.
    if (d->sources) disconnect(d->sources, nullptr, this, nullptr);
    if (d->scope) disconnect(d->scope, nullptr, this, nullptr);
    if (d->sources && d->sources->pluginManager()) disconnect(d->sources->pluginManager(), nullptr, this, nullptr);
    disconnect(d->repository.get(), nullptr, this, nullptr);
    disconnect(d->assets.get(), nullptr, this, nullptr);
    d->pending.clear(); d->assetRequests.clear();
    d->router.reset(); d->assets.reset(); d->directory.reset(); d->repository.reset();
}
MusicPageModel *MusicHub::recommendation() const { return d->pages[0].model.get(); }
MusicPageModel *MusicHub::category() const { return d->pages[1].model.get(); }
MusicPageModel *MusicHub::favorites() const { return d->pages[2].model.get(); }
MusicPageModel *MusicHub::searchResults() const { return d->pages[3].model.get(); }
MusicPageModel *MusicHub::discovery(int kind) const { return validDiscovery(kind) ? d->pages[4+kind].model.get() : nullptr; }
void MusicHub::refreshDiscovery(int kind)
{
    if (!validDiscovery(kind)) return;
    d->pages[4+kind].activated = true; d->refresh(4+kind);
}
void MusicHub::loadMoreDiscovery(int kind, const QString &id)
{ if (validDiscovery(kind)) d->sectionRequest(4+kind, id, true); }
void MusicHub::retryDiscovery(int kind, const QString &id)
{ if (validDiscovery(kind)) d->sectionRequest(4+kind, id, false); }
void MusicHub::closeDiscovery(int kind)
{
    if (!validDiscovery(kind)) return;
    d->pages[4+kind].activated = false; d->invalidate(4+kind, true);
}
MediaActionRouter *MusicHub::actions() const { return d->router.get(); }
DirectoryLibraryController *MusicHub::directoryLibrary() const { return d->directory.get(); }
bool MusicHub::sourcePluginLoaded(const QString &packageId) const
{
    return d->sources && d->sources->pluginManager()
        && d->sources->pluginManager()->plugin(packageId).state == PluginState::Loaded;
}
QVariantList MusicHub::sourceOptions() const { return d->options; }
QString MusicHub::selectedSourceInstanceId() const { return d->selected(); }
void MusicHub::setSelectedSourceInstanceId(const QString &id) { if (d->scope) d->scope->setSelectedSourceInstanceId(id); }
QVariantMap MusicHub::categoryContext() const
{
    return d->stack.isEmpty() ? QVariantMap{} : QVariantMap{{"item", d->stack.last().item},
        {"sourceInstanceId", d->stack.last().query.scope.sourceInstanceId}};
}
bool MusicHub::canNavigateBack() const { return !d->stack.isEmpty(); }
void MusicHub::activatePage(int page)
{
    if (!validPage(page) || d->pages[page].activated) return;
    d->pages[page].activated = true; d->refresh(page);
}
void MusicHub::refresh(int page) { if (validPage(page)) d->refresh(page); }
void MusicHub::loadMore(int page, const QString &id) { if (validPage(page)) d->sectionRequest(page, id, true); }
void MusicHub::retrySection(int page, const QString &id) { if (validPage(page)) d->sectionRequest(page, id, false); }
void MusicHub::search(const QString &text, int searchTab)
{
    if (!validSearchTab(searchTab)) return;
    if (d->searchText != text || d->searchTab != searchTab) {
        d->searchText = text;
        d->searchTab = searchTab;
        d->invalidate(3, true);
    }
    d->pages[3].activated = true; d->refresh(3);
}
void MusicHub::cancel(int page) { if (validPage(page)) d->invalidate(page, false); }
bool MusicHub::browse(const QVariantMap &item)
{
    MediaRefV2 ref;
    if (item.value("ref").metaType().id() != QMetaType::QVariantMap
        || item.value("availableActions").metaType().id() != QMetaType::QVariantMap
        || !parseRef(item.value("ref").toMap(), &ref) || !d->owns(ref)
        || (!d->selected().isEmpty() && d->selected() != ref.sourceInstanceId)) return false;
    PageQueryV2 query;
    query.page = MusicPageKindV2::Category; query.scope.sourceInstanceId = ref.sourceInstanceId;
    query.section = PageSectionKindV2::Tracks;
    switch (ref.entityType) {
    case MediaEntityTypeV2::Album: query.filters = {{"albumId", ref.entityId}}; break;
    case MediaEntityTypeV2::Artist: query.filters = {{"artistId", ref.entityId}}; query.section = PageSectionKindV2::Albums; break;
    case MediaEntityTypeV2::Playlist: query.filters = {{"playlistId", ref.entityId}}; break;
    case MediaEntityTypeV2::Genre: query.filters = {{"genre", ref.entityId}}; break;
    case MediaEntityTypeV2::Directory: query.filters = {{"directoryId", ref.entityId}}; break;
    default: return false;
    }
    // Preserve the full model item shape through the existing sanitizer, rather
    // than retaining arbitrary QML metadata/objects in the observable context.
    MediaItemV2 typed;
    typed.ref = ref; typed.title = item.value("title").toString(); typed.subtitle = item.value("subtitle").toString();
    typed.artists = item.value("artists").toStringList(); typed.album = item.value("album").toString();
    typed.durationMs = item.value("durationMs").toLongLong(); typed.artworkId = item.value("artworkId").toString();
    typed.externalIds = item.value("externalIds").toMap(); typed.metadata = item.value("metadata").toMap();
    const auto actions = item.value("availableActions").toMap();
    for (auto it = actions.cbegin(); it != actions.cend(); ++it) {
        bool ok = false; const int action = it.key().toInt(&ok);
        if (!ok || action < 0 || action > int(SourceActionV2::DeleteBookmark) || QString::number(action) != it.key()
            || it->metaType().id() != QMetaType::QVariantMap) return false;
        const auto value = it->toMap();
        if (!integer(value.value("state"), int(AvailabilityV2::Forbidden))
            || value.value("constraints").metaType().id() != QMetaType::QVariantMap
            || (value.contains("reasonKey") && value.value("reasonKey").metaType().id() != QMetaType::QString)) return false;
        typed.availableActions.insert(SourceActionV2(action), {AvailabilityV2(value.value("state").toInt()),
            value.value("reasonKey").toString(), value.value("constraints").toMap()});
    }
    PageSectionV2 section; section.sectionId = "context"; section.items = {typed};
    MusicPageModel serializer(MusicPageKindV2::Category);
    const auto generation = serializer.beginRequest();
    serializer.applyResult(generation, PageCache::sanitized({{section}, {}, false, true}));
    d->stack.append({query, serializer.itemAt(0, 0)});
    if (d->stack.size() > 32) d->stack.removeFirst();
    d->invalidate(1, true);
    emit categoryContextChanged();
    d->pages[1].activated = true; d->refresh(1);
    return true;
}
bool MusicHub::navigateBack()
{
    if (d->stack.isEmpty()) return false;
    d->stack.removeLast(); d->invalidate(1, true);
    emit categoryContextChanged(); d->refresh(1); return true;
}
void MusicHub::resetCategoryNavigation()
{
    if (d->stack.isEmpty()) return;
    d->stack.clear();
    d->invalidate(1, true);
    emit categoryContextChanged();
    d->refresh(1);
}
QUuid MusicHub::loadArtwork(const QVariantMap &media) { return d->asset(media, false); }
QUuid MusicHub::loadLyrics(const QVariantMap &media) { return d->asset(media, true); }
void MusicHub::cancelAsset(const QUuid &id) { d->assetRequests.remove(id); d->assets->cancel(id); }
