#include "OriginalUiMusicAdapter.h"

#include "MusicHub.h"
#include "DirectoryLibraryController.h"
#include "MusicPageModel.h"
#include "PlaybackCoordinator.h"
#include "OnlineListModel.h"

#include <QRegularExpression>
#include <utility>

namespace {

QString artistName(const QVariantMap &item)
{
    const QStringList artists = item.value(QStringLiteral("artists")).toStringList();
    return artists.join(QStringLiteral(", "));
}

QString safeReasonKey(const QVariant &value)
{
    const QString key = value.toString();
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,127}$"));
    return pattern.match(key).hasMatch() ? key : QStringLiteral("music.actionUnavailable");
}

QVariantMap unavailable(const QString &reason = QStringLiteral("music.actionUnavailable"))
{
    return {{QStringLiteral("enabled"), false}, {QStringLiteral("reasonKey"), reason}};
}

QVariantMap actionCapability(const QVariantMap &item, SourceActionV2 action)
{
    const QVariantMap actions = item.value(QStringLiteral("availableActions")).toMap();
    const QVariantMap value = actions.value(QString::number(int(action))).toMap();
    if (value.value(QStringLiteral("state")).toInt() != int(AvailabilityV2::Available))
        return unavailable(safeReasonKey(value.value(QStringLiteral("reasonKey"))));
    return {{QStringLiteral("enabled"), true}, {QStringLiteral("reasonKey"), QString{}}};
}

QVariantMap browseCapability(const QVariantMap &item)
{
    const QVariant entityValue = item.value(QStringLiteral("ref")).toMap()
        .value(QStringLiteral("entityType"));
    const int entityType = entityValue.isValid() ? entityValue.toInt() : -1;
    const bool supported = entityType == int(MediaEntityTypeV2::Album)
        || entityType == int(MediaEntityTypeV2::Artist)
        || entityType == int(MediaEntityTypeV2::Playlist)
        || entityType == int(MediaEntityTypeV2::Genre)
        || entityType == int(MediaEntityTypeV2::Directory);
    return supported ? QVariantMap{{QStringLiteral("enabled"), true},
                                   {QStringLiteral("reasonKey"), QString{}}}
                     : unavailable(QStringLiteral("music.browseUnsupported"));
}

QVariantMap sectionState(MusicPageModel *model, const QList<PageSectionKindV2> &kinds)
{
    if (!model) return {};
    for (int i = 0; i < model->rowCount(); ++i) {
        const auto index = model->index(i);
        if (!kinds.contains(model->section(i).kind)) continue;
        return {{QStringLiteral("sectionId"), model->data(index, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(index, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(index, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), model->data(index, MusicPageModel::ErrorRole)}};
    }
    return {};
}

QVariantMap aggregateSectionState(MusicPageModel *model, const QList<PageSectionKindV2> &kinds)
{
    if (!model) return {};
    QString firstId;
    QStringList paginationIds, retryIds;
    QVariantMap errors;
    bool hasMore = false, loadingMore = false;
    for (int i = 0; i < model->rowCount(); ++i) {
        if (!kinds.contains(model->section(i).kind)) continue;
        const auto index = model->index(i);
        const QString id = model->data(index, MusicPageModel::SectionIdRole).toString();
        if (firstId.isEmpty()) firstId = id;
        const bool more = model->data(index, MusicPageModel::HasMoreRole).toBool()
            && !model->section(i).nextCursor.isEmpty();
        const bool loading = model->data(index, MusicPageModel::LoadingMoreRole).toBool();
        bool failed = false;
        const auto sourceErrors = model->data(index, MusicPageModel::ErrorRole).toMap();
        for (const auto &value : sourceErrors)
            failed |= value.toMap().value("kind").toInt() != int(SourceErrorKindV2::Unsupported);
        hasMore |= more && sourceErrors.isEmpty();
        loadingMore |= loading;
        if (id.isEmpty()) continue;
        // Keep diagnostic text and source identities out of this aggregate UI state.
        if (failed) {
            errors.insert(id, QVariantMap{{"failed", true}});
            if (!loading) retryIds.append(id);
        } else if (more && !loading && sourceErrors.isEmpty()) {
            paginationIds.append(id);
        }
    }
    return {{"sectionId", firstId}, {"hasMore", hasMore}, {"loadingMore", loadingMore},
            {"paginationSectionIds", paginationIds}, {"retrySectionIds", retryIds}, {"error", errors}};
}

} // namespace

OriginalUiMusicAdapter::OriginalUiMusicAdapter(MusicHub *hub, PlaybackCoordinator *playback,
                                               QObject *parent)
    : QObject(parent), m_hub(hub), m_playback(playback),
      m_recommendSongs(new OnlineListModel(this)), m_categoryItems(new OnlineListModel(this)),
      m_categorySongs(new OnlineListModel(this)),
      m_categoryArtists(new OnlineListModel(this)), m_categoryPlaylists(new OnlineListModel(this)),
      m_categoryCharts(new OnlineListModel(this)),
      m_favoriteSongs(new OnlineListModel(this)), m_favoriteLists(new OnlineListModel(this)),
      m_searchSongs(new OnlineListModel(this)), m_searchLists(new OnlineListModel(this)),
      m_searchAlbums(new OnlineListModel(this)), m_searchLyrics(new OnlineListModel(this)),
      m_directoryItems(new OnlineListModel(this))
{
    if (!m_hub) return;
    const auto observe = [this](MusicPageModel *model) {
        connect(model, &QAbstractItemModel::modelReset, this, &OriginalUiMusicAdapter::rebuild);
        connect(model, &QAbstractItemModel::rowsInserted, this, [this] { rebuild(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { rebuild(); });
        connect(model, &QAbstractItemModel::dataChanged, this, [this] { rebuild(); });
    };
    observe(m_hub->recommendation());
    observe(m_hub->category());
    connect(m_hub->category(), &MusicPageModel::stateChanged,
            this, &OriginalUiMusicAdapter::categoryStatusChanged);
    connect(m_hub->category(), &MusicPageModel::errorChanged,
            this, &OriginalUiMusicAdapter::categoryStatusChanged);
    connect(m_hub, &MusicHub::categoryContextChanged,
            this, &OriginalUiMusicAdapter::categoryNavigationChanged);
    observe(m_hub->favorites());
    observe(m_hub->searchResults());
    connect(m_hub->directoryLibrary(), &DirectoryLibraryController::changed,
            this, [this] { rebuildDirectories(); emit directoryChanged(); });
    connect(m_hub, &MusicHub::sourceOptionsChanged, this, &OriginalUiMusicAdapter::sourceOptionsChanged);
    connect(m_hub, &MusicHub::selectedSourceInstanceIdChanged,
            this, &OriginalUiMusicAdapter::selectedSourceInstanceIdChanged);
    connect(m_hub, &QObject::destroyed, this, [this] {
        m_hub = nullptr;
        clearPresentationState();
        emit categoryStatusChanged();
        emit categoryNavigationChanged();
        emit directoryChanged();
        emit sourceOptionsChanged();
        emit selectedSourceInstanceIdChanged();
    });
    rebuild();
}

OnlineListModel *OriginalUiMusicAdapter::recommendSongs() const { return m_recommendSongs; }
OnlineListModel *OriginalUiMusicAdapter::categoryItems() const { return m_categoryItems; }
OnlineListModel *OriginalUiMusicAdapter::categorySongs() const { return m_categorySongs; }
OnlineListModel *OriginalUiMusicAdapter::categoryArtists() const { return m_categoryArtists; }
OnlineListModel *OriginalUiMusicAdapter::categoryPlaylists() const { return m_categoryPlaylists; }
OnlineListModel *OriginalUiMusicAdapter::categoryCharts() const { return m_categoryCharts; }
bool OriginalUiMusicAdapter::categoryHasError() const
{
    if (!m_hub) return false;
    auto *model = m_hub->category();
    const auto failed = [](const QVariantMap &error) {
        return !error.isEmpty() && error.value("kind").toInt() != int(SourceErrorKindV2::Unsupported);
    };
    if (failed(model->errorMap())) return true;
    for (int i = 0; i < model->rowCount(); ++i) {
        const auto errors = model->data(model->index(i), MusicPageModel::ErrorRole).toMap();
        for (const auto &value : errors)
            if (failed(value.toMap())) return true;
    }
    return false;
}
QString OriginalUiMusicAdapter::categoryState() const
{
    if (!m_hub) return QStringLiteral("unavailable");
    switch (m_hub->category()->state()) {
    case PageLoadStateV2::Loading: return QStringLiteral("loading");
    case PageLoadStateV2::Ready: return QStringLiteral("ready");
    case PageLoadStateV2::Failed: return categoryHasError() ? QStringLiteral("failed") : QStringLiteral("empty");
    case PageLoadStateV2::Idle: case PageLoadStateV2::Empty: return QStringLiteral("empty");
    }
    return QStringLiteral("empty");
}
bool OriginalUiMusicAdapter::categoryCanNavigateBack() const
{
    return m_hub && m_hub->canNavigateBack();
}
QString OriginalUiMusicAdapter::categoryTitle() const
{
    return m_hub ? m_hub->categoryContext().value("item").toMap().value("title").toString() : QString{};
}
QString OriginalUiMusicAdapter::categoryCover() const
{
    return m_hub ? m_hub->categoryContext().value("item").toMap().value("artworkId").toString() : QString{};
}
OnlineListModel *OriginalUiMusicAdapter::favoriteSongs() const { return m_favoriteSongs; }
OnlineListModel *OriginalUiMusicAdapter::favoriteLists() const { return m_favoriteLists; }
OnlineListModel *OriginalUiMusicAdapter::searchSongs() const { return m_searchSongs; }
OnlineListModel *OriginalUiMusicAdapter::searchLists() const { return m_searchLists; }
OnlineListModel *OriginalUiMusicAdapter::searchAlbums() const { return m_searchAlbums; }
OnlineListModel *OriginalUiMusicAdapter::searchLyrics() const { return m_searchLyrics; }
OnlineListModel *OriginalUiMusicAdapter::directoryItems() const { return m_directoryItems; }
QString OriginalUiMusicAdapter::directoryState() const
{
    if (!m_hub || !m_hub->directoryLibrary()) return QStringLiteral("unavailable");
    switch (m_hub->directoryLibrary()->model()->state()) {
    case PageLoadStateV2::Loading: return QStringLiteral("loading");
    case PageLoadStateV2::Ready: return QStringLiteral("ready");
    case PageLoadStateV2::Empty: return QStringLiteral("empty");
    case PageLoadStateV2::Failed: return QStringLiteral("failed");
    case PageLoadStateV2::Idle: return QStringLiteral("empty");
    }
    return QStringLiteral("unavailable");
}
bool OriginalUiMusicAdapter::directoryCanNavigateBack() const
{
    return m_hub && m_hub->directoryLibrary() && m_hub->directoryLibrary()->canNavigateBack();
}
QVariantList OriginalUiMusicAdapter::sourceOptions() const { return m_hub ? m_hub->sourceOptions() : QVariantList{}; }
QString OriginalUiMusicAdapter::selectedSourceInstanceId() const
{
    return m_hub ? m_hub->selectedSourceInstanceId() : QString{};
}
void OriginalUiMusicAdapter::setSelectedSourceInstanceId(const QString &id)
{
    if (m_hub) m_hub->setSelectedSourceInstanceId(id);
}

void OriginalUiMusicAdapter::activatePage(int pageKind)
{
    if (m_hub) m_hub->activatePage(pageKind);
}

void OriginalUiMusicAdapter::refreshPage(int pageKind)
{
    if (m_hub) m_hub->refresh(pageKind);
}

void OriginalUiMusicAdapter::search(const QString &text, int searchTab)
{
    if (m_hub) m_hub->search(text, searchTab);
}

QVariantMap OriginalUiMusicAdapter::capabilities(const QVariant &rows) const
{
    QVariantList values;
    if (rows.metaType().id() == QMetaType::QVariantMap)
        values.append(rows);
    else if (rows.metaType().id() == QMetaType::QVariantList)
        values = rows.toList();
    return capabilitiesFor(values);
}

void OriginalUiMusicAdapter::loadMore(int pageKind, const QString &sectionId)
{
    if (m_hub) m_hub->loadMore(pageKind, sectionId);
}

void OriginalUiMusicAdapter::retry(int pageKind, const QString &sectionId)
{
    if (m_hub) m_hub->retrySection(pageKind, sectionId);
}

QVariantMap OriginalUiMusicAdapter::resolvePresentationItem(const QVariantMap &row) const
{
    bool ok = false;
    const quint64 key = row.value(QStringLiteral("_adapterKey")).toULongLong(&ok);
    return ok ? m_fullItems.value(key) : QVariantMap{};
}

bool OriginalUiMusicAdapter::browse(const QVariantMap &row)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_hub && !item.isEmpty() && permits(item, QStringLiteral("canBrowse"))
        && m_hub->browse(item);
}
void OriginalUiMusicAdapter::closeCategoryBrowse()
{
    if (m_hub) m_hub->resetCategoryNavigation();
}
bool OriginalUiMusicAdapter::categoryBack()
{
    return m_hub && m_hub->navigateBack();
}

QUuid OriginalUiMusicAdapter::play(const QVariantMap &row)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_playback && !item.isEmpty() && permits(item, QStringLiteral("canPlay"))
        ? m_playback->play(item) : QUuid{};
}

QUuid OriginalUiMusicAdapter::enqueue(const QVariantMap &row)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_playback && !item.isEmpty() && permits(item, QStringLiteral("canEnqueue"))
        ? m_playback->enqueue(item) : QUuid{};
}

QUuid OriginalUiMusicAdapter::setFavorite(const QVariantMap &row, bool favorite)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_hub && m_hub->actions() && !item.isEmpty()
        && permits(item, favorite ? QStringLiteral("canFavorite") : QStringLiteral("canUnfavorite"))
        ? m_hub->actions()->setFavorite(item, favorite) : QUuid{};
}

void OriginalUiMusicAdapter::activateDirectories()
{
    if (m_hub && m_hub->directoryLibrary()) m_hub->directoryLibrary()->activate();
}
bool OriginalUiMusicAdapter::browseDirectory(const QVariantMap &row)
{
    bool ok = false;
    const quint64 key = row.value(QStringLiteral("_adapterKey")).toULongLong(&ok);
    if (!ok || !m_directoryKeys.contains(key) || !m_hub || !m_hub->directoryLibrary()) return false;
    const auto item = m_fullItems.value(key);
    return !item.isEmpty() && m_hub->directoryLibrary()->browse(item);
}
bool OriginalUiMusicAdapter::directoryBack()
{
    return m_hub && m_hub->directoryLibrary() && m_hub->directoryLibrary()->navigateBack();
}
void OriginalUiMusicAdapter::refreshDirectories()
{
    if (m_hub && m_hub->directoryLibrary()) m_hub->directoryLibrary()->refresh();
}
void OriginalUiMusicAdapter::loadMoreDirectories(const QString &sectionId)
{
    if (m_hub && m_hub->directoryLibrary()) m_hub->directoryLibrary()->loadMore(sectionId);
}
bool OriginalUiMusicAdapter::pluginAvailable(const QString &packageId) const
{
    return m_hub && m_hub->sourcePluginLoaded(packageId);
}

QVariantMap OriginalUiMusicAdapter::capabilitiesForItem(const QVariantMap &item) const
{
    const auto browse = browseCapability(item);
    const auto play = actionCapability(item, SourceActionV2::Play);
    const auto favorite = actionCapability(item, SourceActionV2::Favorite);
    const auto unfavorite = actionCapability(item, SourceActionV2::Unfavorite);
    return {{QStringLiteral("canBrowse"), browse.value(QStringLiteral("enabled"))},
            {QStringLiteral("browseReasonKey"), browse.value(QStringLiteral("reasonKey"))},
            {QStringLiteral("canPlay"), play.value(QStringLiteral("enabled"))},
            {QStringLiteral("playReasonKey"), play.value(QStringLiteral("reasonKey"))},
            {QStringLiteral("canEnqueue"), play.value(QStringLiteral("enabled"))},
            {QStringLiteral("enqueueReasonKey"), play.value(QStringLiteral("reasonKey"))},
            {QStringLiteral("canFavorite"), favorite.value(QStringLiteral("enabled"))},
            {QStringLiteral("favoriteReasonKey"), favorite.value(QStringLiteral("reasonKey"))},
            {QStringLiteral("canUnfavorite"), unfavorite.value(QStringLiteral("enabled"))},
            {QStringLiteral("unfavoriteReasonKey"), unfavorite.value(QStringLiteral("reasonKey"))}};
}

QVariantMap OriginalUiMusicAdapter::capabilitiesFor(const QVariantList &rows) const
{
    const QStringList names{QStringLiteral("Browse"), QStringLiteral("Play"),
                            QStringLiteral("Enqueue"), QStringLiteral("Favorite"),
                            QStringLiteral("Unfavorite")};
    QVariantMap result;
    for (const QString &name : names) {
        result.insert(QStringLiteral("can") + name, false);
        result.insert(name.left(1).toLower() + name.mid(1) + QStringLiteral("ReasonKey"),
                      QStringLiteral("music.actionInvalidItem"));
    }
    if (rows.isEmpty()) return result;

    bool first = true;
    for (const QVariant &value : rows) {
        if (value.metaType().id() != QMetaType::QVariantMap) return result;
        const QVariantMap item = resolvePresentationItem(value.toMap());
        if (item.isEmpty()) return result;
        const QVariantMap itemCapabilities = capabilitiesForItem(item);
        for (const QString &name : names) {
            const QString enabled = QStringLiteral("can") + name;
            const QString reason = name.left(1).toLower() + name.mid(1) + QStringLiteral("ReasonKey");
            if (first) {
                result.insert(enabled, itemCapabilities.value(enabled));
                result.insert(reason, itemCapabilities.value(reason));
            } else if (!itemCapabilities.value(enabled).toBool()) {
                result.insert(enabled, false);
                result.insert(reason, itemCapabilities.value(reason));
            }
        }
        first = false;
    }
    return result;
}

bool OriginalUiMusicAdapter::permits(const QVariantMap &item, const QString &capability) const
{
    return capabilitiesForItem(item).value(capability).toBool();
}

QVariantMap OriginalUiMusicAdapter::presentationItem(const QVariantMap &full,
                                                      const QVariantMap &sectionState)
{
    const QVariantMap ref = full.value(QStringLiteral("ref")).toMap();
    const quint64 key = m_nextAdapterKey++;
    m_fullItems.insert(key, full);
    return {{QStringLiteral("title"), full.value(QStringLiteral("title")).toString()},
            {QStringLiteral("artist"), artistName(full)},
            {QStringLiteral("album"), full.value(QStringLiteral("album")).toString()},
            {QStringLiteral("cover"), full.value(QStringLiteral("artworkId")).toString()},
            // Original QListView rows use seconds; source DTOs retain milliseconds.
            {QStringLiteral("duration"), qMax<qint64>(0, full.value(QStringLiteral("durationMs")).toLongLong()) / 1000},
            {QStringLiteral("source"), ref.value(QStringLiteral("sourcePluginId")).toString()},
            {QStringLiteral("entityType"), ref.value(QStringLiteral("entityType"))},
            {QStringLiteral("collectionKind"),
             ref.value(QStringLiteral("entityType")).toInt() == int(MediaEntityTypeV2::Playlist)
                 && full.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("collectionKind"))
                        == QStringLiteral("chart") ? QStringLiteral("chart") : QString{}},
            {QStringLiteral("subtitle"), full.value(QStringLiteral("subtitle")).toString()},
            {QStringLiteral("sectionId"), sectionState.value(QStringLiteral("sectionId"))},
            {QStringLiteral("hasMore"), sectionState.value(QStringLiteral("hasMore"))},
            {QStringLiteral("loadingMore"), sectionState.value(QStringLiteral("loadingMore"))},
            {QStringLiteral("error"), sectionState.value(QStringLiteral("error"))},
            {QStringLiteral("_adapterKey"), QVariant::fromValue<qulonglong>(key)}};
}

void OriginalUiMusicAdapter::clearPresentationState()
{
    m_fullItems.clear();
    m_recommendSongs->setItems({});
    m_categoryItems->setItems({});
    m_categorySongs->setItems({});
    m_categoryArtists->setItems({});
    m_categoryPlaylists->setItems({});
    m_categoryCharts->setItems({});
    m_favoriteSongs->setItems({});
    m_favoriteLists->setItems({});
    m_searchSongs->setItems({});
    m_searchLists->setItems({});
    m_searchAlbums->setItems({});
    m_searchLyrics->setItems({});
    m_directoryItems->setItems({});
    m_directoryKeys.clear();
    for (OnlineListModel *model : {m_recommendSongs, m_categoryItems, m_categorySongs, m_favoriteSongs,
                                   m_categoryArtists, m_categoryPlaylists, m_categoryCharts,
                                   m_favoriteLists, m_searchSongs, m_searchLists,
                                   m_searchAlbums, m_searchLyrics, m_directoryItems})
        model->setPresentationState({});
}

void OriginalUiMusicAdapter::rebuild()
{
    QVariantList recommendations;
    QVariantList category;
    QVariantList favoriteSongs;
    QVariantList favoriteLists;
    QVariantList searchSongs;
    QVariantList searchLists;
    QVariantList searchAlbums;
    QVariantList searchLyrics;
    m_fullItems.clear();
    m_directoryKeys.clear();

    const auto append = [this](MusicPageModel *model, QVariantList *target,
                               QVariantList *tracks = nullptr, QVariantList *playlists = nullptr) {
        for (int section = 0; section < model->rowCount(); ++section) {
            const auto items = model->data(model->index(section), MusicPageModel::ItemsRole).toList();
            const auto sectionIndex = model->index(section);
            const QVariantMap sectionState{
                {QStringLiteral("sectionId"), model->data(sectionIndex, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(sectionIndex, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(sectionIndex, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), model->data(sectionIndex, MusicPageModel::ErrorRole)}};
            for (int index = 0; index < items.size(); ++index) {
                const QVariantMap full = model->itemAt(section, index);
                const int entityType = full.value(QStringLiteral("ref")).toMap()
                    .value(QStringLiteral("entityType")).toInt();
                if (tracks || playlists) {
                    if (entityType == int(MediaEntityTypeV2::Track)) tracks->append(presentationItem(full, sectionState));
                    else if (entityType == int(MediaEntityTypeV2::Playlist)) playlists->append(presentationItem(full, sectionState));
                } else target->append(presentationItem(full, sectionState));
            }
        }
    };

    if (m_hub) {
        append(m_hub->recommendation(), &recommendations);
        append(m_hub->category(), &category);
        append(m_hub->favorites(), nullptr, &favoriteSongs, &favoriteLists);
        for (int section = 0; section < m_hub->searchResults()->rowCount(); ++section) {
            auto *model = m_hub->searchResults();
            const auto sectionIndex = model->index(section);
            const QVariantMap sectionState{
                {QStringLiteral("sectionId"), model->data(sectionIndex, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(sectionIndex, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(sectionIndex, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), model->data(sectionIndex, MusicPageModel::ErrorRole)}};
            const auto items = model->data(sectionIndex, MusicPageModel::ItemsRole).toList();
            for (int index = 0; index < items.size(); ++index) {
                const QVariantMap full = model->itemAt(section, index);
                switch (full.value(QStringLiteral("ref")).toMap()
                            .value(QStringLiteral("entityType")).toInt()) {
                case int(MediaEntityTypeV2::Track):
                    searchSongs.append(presentationItem(full, sectionState));
                    searchLyrics.append(presentationItem(full, sectionState));
                    break;
                case int(MediaEntityTypeV2::Playlist):
                    searchLists.append(presentationItem(full, sectionState));
                    break;
                case int(MediaEntityTypeV2::Album):
                    searchAlbums.append(presentationItem(full, sectionState));
                    break;
                default:
                    break;
                }
            }
        }
    }
    m_recommendSongs->setItems(recommendations);
    m_categoryItems->setItems(category);
    QVariantList categorySongs, categoryArtists, categoryPlaylists, categoryCharts;
    for (const QVariant &row : category) {
        const auto item = row.toMap();
        if (item.value("entityType").toInt() == int(MediaEntityTypeV2::Track))
            categorySongs.append(row);
        else if (item.value("entityType").toInt() == int(MediaEntityTypeV2::Artist))
            categoryArtists.append(row);
        else if (item.value("entityType").toInt() == int(MediaEntityTypeV2::Playlist)) {
            if (item.value("collectionKind").toString() == QStringLiteral("chart"))
                categoryCharts.append(row);
            else categoryPlaylists.append(row);
        }
    }
    m_categorySongs->setItems(categorySongs);
    m_categoryArtists->setItems(categoryArtists);
    m_categoryPlaylists->setItems(categoryPlaylists);
    m_categoryCharts->setItems(categoryCharts);
    m_favoriteSongs->setItems(favoriteSongs);
    m_favoriteLists->setItems(favoriteLists);
    m_searchSongs->setItems(searchSongs);
    m_searchLists->setItems(searchLists);
    m_searchAlbums->setItems(searchAlbums);
    m_searchLyrics->setItems(searchLyrics);
    if (m_hub) {
        m_recommendSongs->setPresentationState(aggregateSectionState(m_hub->recommendation(), {
            PageSectionKindV2::RecentlyPlayed, PageSectionKindV2::FrequentlyPlayed,
            PageSectionKindV2::HighestRated, PageSectionKindV2::Newest, PageSectionKindV2::Random}));
        m_categoryItems->setPresentationState(aggregateSectionState(m_hub->category(), {
            PageSectionKindV2::Genres, PageSectionKindV2::Artists,
            PageSectionKindV2::Albums, PageSectionKindV2::Tracks, PageSectionKindV2::Playlists}));
        m_categorySongs->setPresentationState(aggregateSectionState(m_hub->category(), {PageSectionKindV2::Tracks}));
        m_categoryArtists->setPresentationState(aggregateSectionState(m_hub->category(), {PageSectionKindV2::Artists}));
        // Charts are a presentation classification, not a distinct v2 query/cursor.
        const auto playlistsState = aggregateSectionState(m_hub->category(), {PageSectionKindV2::Playlists});
        m_categoryPlaylists->setPresentationState(playlistsState);
        m_categoryCharts->setPresentationState(playlistsState);
        m_favoriteSongs->setPresentationState(aggregateSectionState(m_hub->favorites(), {PageSectionKindV2::FavoriteTracks}));
        m_favoriteLists->setPresentationState(aggregateSectionState(m_hub->favorites(), {PageSectionKindV2::Playlists}));
        m_searchSongs->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Tracks}));
        m_searchLists->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Playlists}));
        m_searchAlbums->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Albums}));
        m_searchLyrics->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Tracks}));
    }
    rebuildDirectories();
    emit categoryStatusChanged();
}

void OriginalUiMusicAdapter::rebuildDirectories()
{
    for (const quint64 key : std::as_const(m_directoryKeys)) m_fullItems.remove(key);
    m_directoryKeys.clear();
    QVariantList rows;
    if (m_hub && m_hub->directoryLibrary()) {
        auto *controller = m_hub->directoryLibrary();
        auto *model = controller->model();
        for (int section = 0; section < model->rowCount(); ++section) {
            const auto index = model->index(section);
            const QVariantMap state{
                {QStringLiteral("sectionId"), model->data(index, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(index, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(index, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), model->data(index, MusicPageModel::ErrorRole)}};
            const QVariantMap error = state.value(QStringLiteral("error")).toMap();
            if (!error.isEmpty() && model->section(section).items.isEmpty()) {
                rows.append(QVariantMap{{QStringLiteral("title"), tr("目录加载失败")},
                                        {QStringLiteral("isError"), true},
                                        {QStringLiteral("sectionId"), state.value(QStringLiteral("sectionId"))},
                                        {QStringLiteral("error"), error}});
            }
            for (int item = 0; item < model->section(section).items.size(); ++item) {
                const auto full = model->itemAt(section, item);
                auto row = presentationItem(full, state);
                const auto settings = controller->settingsTarget(full);
                if (!settings.isEmpty()) {
                    row.insert(QStringLiteral("settingsPackageId"), settings.value("packageId"));
                    row.insert(QStringLiteral("settingsInstanceId"), settings.value("instanceId"));
                }
                m_directoryKeys.insert(row.value(QStringLiteral("_adapterKey")).toULongLong());
                rows.append(row);
            }
        }
        m_directoryItems->setPresentationState(sectionState(model, {PageSectionKindV2::Tracks}));
    } else m_directoryItems->setPresentationState({});
    m_directoryItems->setItems(rows);
}
