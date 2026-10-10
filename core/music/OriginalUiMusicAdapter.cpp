#include "OriginalUiMusicAdapter.h"

#include "MusicHub.h"
#include "DirectoryLibraryController.h"
#include "MusicPageModel.h"
#include "PlaybackCoordinator.h"
#include "OnlineListModel.h"
#include "core/media/TimedLyrics.h"

#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <utility>

namespace {

bool newDownloadDestination(const QUrl &url)
{
    if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.port() != -1 || url.hasQuery() || url.hasFragment()) return false;
    const auto path = url.toLocalFile();
    if (path.isEmpty() || path.contains(QChar::Null)) return false;
    const QFileInfo target(path);
    const QFileInfo parent(target.dir().absolutePath());
    return target.isAbsolute() && !target.fileName().isEmpty() && !target.exists() && !target.isSymLink()
        && parent.exists() && parent.isDir() && parent.isWritable();
}

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

QVariantMap aggregateSectionState(MusicPageModel *model, const QList<PageSectionKindV2> &kinds,
                                  bool ignoreUnsupported = false)
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
        const bool noBlockingErrors = sourceErrors.isEmpty() || (ignoreUnsupported && !failed);
        hasMore |= more && noBlockingErrors;
        loadingMore |= loading;
        if (id.isEmpty()) continue;
        // Keep diagnostic text and source identities out of this aggregate UI state.
        if (failed) {
            errors.insert(id, QVariantMap{{"failed", true}});
            if (!loading) retryIds.append(id);
        } else if (more && !loading && noBlockingErrors) {
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
      m_recommendSongs(new OnlineListModel(this)),
      m_personalRadio(new OnlineListModel(this)), m_personalRadar(new OnlineListModel(this)),
      m_categoryItems(new OnlineListModel(this)),
      m_categorySongs(new OnlineListModel(this)),
      m_categoryArtists(new OnlineListModel(this)), m_categoryPlaylists(new OnlineListModel(this)),
      m_categoryCharts(new OnlineListModel(this)),
      m_favoriteSongs(new OnlineListModel(this)), m_favoriteLists(new OnlineListModel(this)),
      m_favoriteArtists(new OnlineListModel(this)),
      m_searchSongs(new OnlineListModel(this)), m_searchLists(new OnlineListModel(this)),
      m_searchAlbums(new OnlineListModel(this)), m_searchLyrics(new OnlineListModel(this)),
      m_directoryItems(new OnlineListModel(this))
{
    if (!m_hub) return;
    if (m_playback) {
        connect(m_playback, &PlaybackCoordinator::currentChanged, this, [this] { syncCurrentDownload(); });
        connect(m_playback, &QObject::destroyed, this, [this] { m_playback = nullptr; syncCurrentDownload(); });
        connect(m_playback, &PlaybackCoordinator::currentChanged, this, [this] { syncCurrentFavorite(); });
        connect(m_playback, &PlaybackCoordinator::currentChanged, this, [this] { syncCurrentLyrics(); });
        connect(m_playback, &QObject::destroyed, this, [this] { m_playback = nullptr; syncCurrentFavorite(); });
        connect(m_playback, &QObject::destroyed, this, [this] { m_playback = nullptr; clearCurrentLyrics(); });
        connect(m_hub, &MusicHub::sourceOptionsChanged, this, [this] { syncCurrentLyrics(); });
        connect(m_hub, &MusicHub::artworkReady, this, [this](QUuid id, QVariantMap media, QUrl url) {
            if (!currentAssetRequestIsCurrent(id, m_artworkRequest) || media != m_lyricsMedia) return;
            m_artworkRequest = {};
            m_currentCover = url.isLocalFile() && url.host().isEmpty() ? url : QUrl{};
            emit currentCoverChanged();
        });
        connect(m_hub, &MusicHub::lyricsReady, this, [this](QUuid id, QVariantMap media, const QString &text) {
            if (!lyricsRequestIsCurrent(id) || media != m_lyricsMedia) return;
            m_lyricsRequest = {};
            if (text.size() > 65536) {
                m_currentLyricsState = QStringLiteral("failed");
            } else {
                auto lines = TimedLyrics::parseLrc(text);
                if (lines.isEmpty() && !text.trimmed().isEmpty())
                    lines.append(QVariantMap{{"time", 0LL}, {"text", text.trimmed()}});
                if (lines.size() > 4096) m_currentLyricsState = QStringLiteral("failed");
                else {
                    m_currentLyrics = lines;
                    m_currentLyricsState = lines.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready");
                }
            }
            emit currentLyricsChanged();
        });
        connect(m_hub, &MusicHub::assetFailed, this, [this](QUuid id, QVariantMap error) {
            if (currentAssetRequestIsCurrent(id, m_artworkRequest)) {
                m_artworkRequest = {}; m_currentCover = QUrl{};
                emit currentCoverChanged();
                return;
            }
            if (!lyricsRequestIsCurrent(id)) return;
            m_lyricsRequest = {};
            m_currentLyricsState = error.value("kind").toInt() == int(SourceErrorKindV2::Unsupported)
                ? QStringLiteral("empty") : QStringLiteral("failed");
            emit currentLyricsChanged();
        });
        connect(m_hub->actions(), &MediaActionRouter::actionSucceeded, this, [this](QUuid id, QVariantMap result) {
            if (!favoriteRequestIsCurrent(id)) return;
            m_favoriteRequest = {};
            const int action = result.value("action").toInt();
            const auto favorite = result.value("favorite");
            if ((action != int(SourceActionV2::Favorite) && action != int(SourceActionV2::Unfavorite))
                || result.value("subject").toMap() != m_favoriteMedia
                || favorite.metaType().id() != QMetaType::Bool) m_favoriteFailed = true;
            else {
                m_favoriteState = favorite.toBool() ? QStringLiteral("favorite") : QStringLiteral("notFavorite");
                m_favoriteFailed = false;
            }
            emit currentFavoriteChanged();
        });
        connect(m_hub->actions(), &MediaActionRouter::actionFailed, this, [this](QUuid id, const QVariantMap &) {
            if (!favoriteRequestIsCurrent(id)) return;
            m_favoriteRequest = {}; m_favoriteFailed = true;
            emit currentFavoriteChanged();
        });
        connect(m_hub->actions(), &MediaActionRouter::actionSucceeded, this, [this](QUuid id, QVariantMap result) {
            finishDownload(id, result, true);
        });
        connect(m_hub->actions(), &MediaActionRouter::actionFailed, this, [this](QUuid id, const QVariantMap &error) {
            finishDownload(id, {}, false, error.value("messageKey").toString() == QStringLiteral("music.actionCancelled"));
        });
    }
    connect(m_hub->actions(), &QObject::destroyed, this, &OriginalUiMusicAdapter::failPendingDownloadTasks);
    const auto observe = [this](MusicPageModel *model) {
        connect(model, &QAbstractItemModel::modelReset, this, &OriginalUiMusicAdapter::rebuild);
        connect(model, &QAbstractItemModel::rowsInserted, this, [this] { rebuild(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { rebuild(); });
        connect(model, &QAbstractItemModel::dataChanged, this, [this] { rebuild(); });
    };
    observe(m_hub->recommendation());
    for (int kind = 0; kind < 2; ++kind) {
        auto *model = m_hub->discovery(kind);
        observe(model);
        connect(model, &MusicPageModel::stateChanged, this, &OriginalUiMusicAdapter::discoveryStatusChanged);
        connect(model, &MusicPageModel::errorChanged, this, &OriginalUiMusicAdapter::discoveryStatusChanged);
    }
    observe(m_hub->category());
    connect(m_hub->category(), &MusicPageModel::stateChanged,
            this, &OriginalUiMusicAdapter::categoryStatusChanged);
    connect(m_hub->category(), &MusicPageModel::errorChanged,
            this, &OriginalUiMusicAdapter::categoryStatusChanged);
    connect(m_hub, &MusicHub::categoryContextChanged,
            this, &OriginalUiMusicAdapter::categoryNavigationChanged);
    observe(m_hub->favorites());
    connect(m_hub->favorites(), &MusicPageModel::stateChanged,
            this, &OriginalUiMusicAdapter::favoriteStatusChanged);
    connect(m_hub->favorites(), &MusicPageModel::errorChanged,
            this, &OriginalUiMusicAdapter::favoriteStatusChanged);
    observe(m_hub->searchResults());
    connect(m_hub->searchResults(), &MusicPageModel::stateChanged,
            this, &OriginalUiMusicAdapter::searchStatusChanged);
    connect(m_hub->directoryLibrary(), &DirectoryLibraryController::changed,
            this, [this] { rebuildDirectories(); emit directoryChanged(); });
    connect(m_hub, &MusicHub::sourceOptionsChanged, this, &OriginalUiMusicAdapter::sourceOptionsChanged);
    connect(m_hub, &MusicHub::selectedSourceInstanceIdChanged,
            this, &OriginalUiMusicAdapter::selectedSourceInstanceIdChanged);
    connect(m_hub, &QObject::destroyed, this, [this] {
        m_hub = nullptr;
        syncCurrentFavorite();
        syncCurrentDownload();
    });
    connect(m_hub, &QObject::destroyed, this, [this] {
        m_hub = nullptr;
        clearCurrentLyrics();
        clearPresentationState();
        emit discoveryStatusChanged();
        emit categoryStatusChanged();
        emit searchStatusChanged();
        emit categoryNavigationChanged();
        emit directoryChanged();
        emit sourceOptionsChanged();
        emit selectedSourceInstanceIdChanged();
    });
    rebuild();
    syncCurrentFavorite();
    syncCurrentDownload();
    syncCurrentLyrics();
}

QVariantMap OriginalUiMusicAdapter::currentFavorite() const
{
    const auto item = m_hub && m_playback ? m_playback->currentActionItem() : QVariantMap{};
    const bool same = !item.isEmpty() && m_playback->currentGeneration() == m_favoriteGeneration
        && item.value("ref").toMap() == m_favoriteMedia;
    return {{"canFavorite", !item.isEmpty() && permits(item, QStringLiteral("canFavorite"))},
            {"canUnfavorite", !item.isEmpty() && permits(item, QStringLiteral("canUnfavorite"))},
            {"state", same ? m_favoriteState : QStringLiteral("unknown")},
            {"pending", same && !m_favoriteRequest.isNull()}, {"failed", same && m_favoriteFailed},
            {"token", !item.isEmpty() ? m_playback->currentGeneration().toString(QUuid::WithoutBraces) : QString{}}};
}
void OriginalUiMusicAdapter::syncCurrentFavorite(bool notify)
{
    const auto item = m_hub && m_playback ? m_playback->currentActionItem() : QVariantMap{};
    const auto generation = !item.isEmpty() ? m_playback->currentGeneration() : QUuid{};
    const auto media = item.value("ref").toMap();
    if (generation != m_favoriteGeneration || media != m_favoriteMedia) {
        m_favoriteGeneration = generation; m_favoriteMedia = media; m_favoriteRequest = {};
        m_favoriteState = QStringLiteral("unknown"); m_favoriteFailed = false;
    }
    if (notify) emit currentFavoriteChanged();
}
bool OriginalUiMusicAdapter::favoriteRequestIsCurrent(const QUuid &id) const
{
    return !id.isNull() && id == m_favoriteRequest && m_hub && m_playback
        && m_playback->currentGeneration() == m_favoriteGeneration
        && m_playback->currentActionItem().value("ref").toMap() == m_favoriteMedia;
}
QUuid OriginalUiMusicAdapter::setCurrentFavorite(bool favorite, const QString &expectedToken)
{
    if (!expectedToken.isEmpty() && (!m_playback
        || expectedToken != m_playback->currentGeneration().toString(QUuid::WithoutBraces))) return {};
    const QPointer<OriginalUiMusicAdapter> guard(this);
    syncCurrentFavorite(false);
    if (!guard || !m_hub || !m_playback || !m_favoriteRequest.isNull()) return {};
    const auto item = m_playback->currentActionItem();
    if (item.isEmpty() || !permits(item, favorite ? QStringLiteral("canFavorite") : QStringLiteral("canUnfavorite"))) return {};
    // Reserve before notifying observers; a reentrant click must not dispatch twice.
    const auto reservation = QUuid::createUuid();
    m_favoriteRequest = reservation; m_favoriteFailed = false;
    emit currentFavoriteChanged();
    if (!guard || !favoriteRequestIsCurrent(reservation)) return {};
    const auto request = m_hub->actions()->setFavorite(item, favorite);
    if (!guard) return request;
    if (favoriteRequestIsCurrent(reservation)) m_favoriteRequest = request;
    return request;
}

QVariantMap OriginalUiMusicAdapter::currentDownload() const
{
    const auto item = m_hub && m_playback ? m_playback->currentActionItem() : QVariantMap{};
    const bool same = !item.isEmpty() && m_playback->currentGeneration() == m_downloadGeneration
        && item.value("ref").toMap() == m_downloadMedia;
    return {{"canDownload", !item.isEmpty() && actionCapability(item, SourceActionV2::Download).value("enabled").toBool()},
            {"pending", same && !m_downloadRequest.isNull()}, {"failed", same && m_downloadFailed},
            {"completed", same && m_downloadCompleted},
            {"token", !item.isEmpty() ? m_playback->currentGeneration().toString(QUuid::WithoutBraces) : QString{}}};
}
void OriginalUiMusicAdapter::syncCurrentDownload(bool notify)
{
    const auto item = m_hub && m_playback ? m_playback->currentActionItem() : QVariantMap{};
    const auto generation = !item.isEmpty() ? m_playback->currentGeneration() : QUuid{};
    const auto media = item.value("ref").toMap();
    if (generation != m_downloadGeneration || media != m_downloadMedia) {
        m_downloadGeneration = generation; m_downloadMedia = media; m_downloadRequest = {};
        m_downloadDestination = QUrl{}; m_downloadFailed = false; m_downloadCompleted = false;
    }
    if (notify) emit currentDownloadChanged();
}
bool OriginalUiMusicAdapter::downloadRequestIsCurrent(const QUuid &id) const
{
    return !id.isNull() && id == m_downloadRequest && m_hub && m_playback
        && m_playback->currentGeneration() == m_downloadGeneration
        && m_playback->currentActionItem().value("ref").toMap() == m_downloadMedia;
}
QVariantList OriginalUiMusicAdapter::downloadTasks() const
{
    QVariantList result;
    for (const auto &task : m_downloadTasks) result.append(task.presentation);
    return result;
}
bool OriginalUiMusicAdapter::dismissDownloadTask(const QString &taskId)
{
    for (qsizetype i = 0; i < m_downloadTasks.size(); ++i) {
        const auto &task = m_downloadTasks.at(i);
        if (task.presentation.value("taskId").toString() != taskId) continue;
        if (task.presentation.value("state").toString() == QStringLiteral("pending")) return false;
        m_downloadTasks.removeAt(i);
        emit downloadTasksChanged();
        return true;
    }
    return false;
}
bool OriginalUiMusicAdapter::cancelDownloadTask(const QString &taskId)
{
    if (!m_hub) return false;
    for (const auto &task : m_downloadTasks) {
        if (task.presentation.value("taskId").toString() != taskId
            || task.presentation.value("state").toString() != "pending" || task.request.isNull()) continue;
        return m_hub->actions()->cancelDownload(task.request);
    }
    return false;
}
void OriginalUiMusicAdapter::finishDownload(const QUuid &id, const QVariantMap &result, bool succeeded, bool cancelled)
{
    bool changed = false;
    for (auto &task : m_downloadTasks) {
        if (id.isNull() || task.request != id || task.presentation.value("state").toString() != "pending") continue;
        const auto destination = result.value("destination");
        succeeded = succeeded && result.value("action").toInt() == int(SourceActionV2::Download)
            && result.value("subject").toMap() == task.media
            && destination.metaType().id() == QMetaType::QUrl && destination.toUrl() == task.destination;
        task.presentation["state"] = succeeded ? QStringLiteral("completed")
            : cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
        task.request = {}; task.media.clear(); task.destination = QUrl{};
        changed = true;
        break;
    }
    const bool current = changed && downloadRequestIsCurrent(id);
    if (current) {
        m_downloadRequest = {}; m_downloadDestination = QUrl{};
        m_downloadFailed = !succeeded; m_downloadCompleted = succeeded;
    }
    const QPointer<OriginalUiMusicAdapter> guard(this);
    if (current) emit currentDownloadChanged();
    if (guard && changed) emit downloadTasksChanged();
}
void OriginalUiMusicAdapter::failPendingDownloadTasks()
{
    bool changed = false;
    for (auto &task : m_downloadTasks) {
        if (task.presentation.value("state").toString() != "pending") continue;
        task.presentation["state"] = QStringLiteral("failed");
        task.request = {}; task.media.clear(); task.destination = QUrl{};
        changed = true;
    }
    if (changed) emit downloadTasksChanged();
}
QUuid OriginalUiMusicAdapter::downloadCurrent(const QUrl &destination, const QString &expectedToken)
{
    if (expectedToken.isEmpty() || !m_playback
        || expectedToken != m_playback->currentGeneration().toString(QUuid::WithoutBraces)) return {};
    const QPointer<OriginalUiMusicAdapter> guard(this);
    syncCurrentDownload(false);
    if (!guard || !m_hub || !m_playback || !m_downloadRequest.isNull()) return {};
    const auto item = m_playback->currentActionItem();
    if (item.isEmpty() || !actionCapability(item, SourceActionV2::Download).value("enabled").toBool()) return {};
    m_downloadCompleted = false;
    const auto targetReserved = [this, &destination] {
        for (const auto &task : m_downloadTasks)
            if (task.presentation.value("state").toString() == "pending"
                && task.destination == destination) return true;
        return false;
    };
    if (!newDownloadDestination(destination) || targetReserved()) {
        m_downloadFailed = true; emit currentDownloadChanged(); return {};
    }
    const auto reservation = QUuid::createUuid();
    m_downloadRequest = reservation; m_downloadDestination = destination; m_downloadFailed = false;
    emit currentDownloadChanged();
    if (!guard || !downloadRequestIsCurrent(reservation)) return {};
    // Observers may create the target while reacting to pending; recheck before dispatch.
    if (!newDownloadDestination(destination) || targetReserved()) {
        m_downloadRequest = {}; m_downloadDestination = QUrl{}; m_downloadFailed = true;
        emit currentDownloadChanged(); return {};
    }
    // Session-local presentation is independent of the current playback generation.
    // Keep the request/ref/destination private; v2 provides terminal state, not byte progress.
    const QVariantMap presentation{{"taskId", reservation.toString(QUuid::WithoutBraces)},
        {"title", item.value("title").toString()}, {"artist", artistName(item)},
        {"sourceLabel", item.value("sourceLabel").toString()},
        {"fileName", QFileInfo(destination.toLocalFile()).fileName()}, {"state", QStringLiteral("pending")}};
    m_downloadTasks.append({presentation, {}, item.value("ref").toMap(), destination});
    const auto request = m_hub->actions()->download(item, destination);
    if (!guard) return request;
    for (auto &task : m_downloadTasks)
        if (task.presentation.value("taskId") == presentation.value("taskId")
            && task.presentation.value("state").toString() == "pending") task.request = request;
    if (downloadRequestIsCurrent(reservation)) m_downloadRequest = request;
    emit downloadTasksChanged();
    return request;
}

OriginalUiMusicAdapter::~OriginalUiMusicAdapter() { cancelCurrentLyrics(); cancelCurrentCover(); }
QVariantList OriginalUiMusicAdapter::currentLyrics() const { return m_currentLyrics; }
QString OriginalUiMusicAdapter::currentLyricsState() const { return m_currentLyricsState; }
QUrl OriginalUiMusicAdapter::currentCover() const { return m_currentCover; }
void OriginalUiMusicAdapter::cancelCurrentCover()
{
    const auto id = std::exchange(m_artworkRequest, QUuid{});
    if (m_hub && !id.isNull()) m_hub->cancelAsset(id);
}
void OriginalUiMusicAdapter::cancelCurrentLyrics()
{
    const auto id = std::exchange(m_lyricsRequest, QUuid{});
    if (m_hub && !id.isNull()) m_hub->cancelAsset(id);
}
void OriginalUiMusicAdapter::clearCurrentLyrics()
{
    cancelCurrentLyrics();
    cancelCurrentCover();
    m_lyricsGeneration = {}; m_lyricsMedia.clear(); m_currentLyrics.clear();
    m_currentCover = QUrl{}; m_currentLyricsState = QStringLiteral("idle");
    emit currentCoverChanged(); emit currentLyricsChanged();
}
bool OriginalUiMusicAdapter::lyricsRequestIsCurrent(const QUuid &id) const
{
    return currentAssetRequestIsCurrent(id, m_lyricsRequest);
}
bool OriginalUiMusicAdapter::currentAssetRequestIsCurrent(const QUuid &id, const QUuid &request) const
{
    return !id.isNull() && id == request && m_playback && m_hub
        && m_lyricsGeneration == m_playback->currentGeneration()
        && m_lyricsMedia == m_playback->currentItem().value("ref").toMap()
        && !m_playback->currentItem().value("unavailable").toBool();
}
void OriginalUiMusicAdapter::syncCurrentLyrics(bool force)
{
    const auto item = m_playback ? m_playback->currentItem() : QVariantMap{};
    const auto generation = m_playback ? m_playback->currentGeneration() : QUuid{};
    const auto media = item.value("ref").toMap();
    if (!m_hub || generation.isNull() || media.isEmpty() || item.value("unavailable").toBool()) {
        clearCurrentLyrics(); return;
    }
    const bool changed = generation != m_lyricsGeneration || media != m_lyricsMedia;
    if (!force && !changed) return;
    cancelCurrentLyrics();
    if (changed) { cancelCurrentCover(); m_currentCover = QUrl{}; }
    m_lyricsGeneration = generation; m_lyricsMedia = media; m_currentLyrics.clear();
    m_currentLyricsState = QStringLiteral("loading");
    if (changed) emit currentCoverChanged();
    // Cover observers may synchronously switch playback before lyric observers run.
    if (!m_playback || generation != m_playback->currentGeneration()
        || generation != m_lyricsGeneration || media != m_lyricsMedia) return;
    emit currentLyricsChanged();
    // A presentation callback can synchronously switch/stop the current playback.
    if (m_hub && m_playback && generation == m_playback->currentGeneration()
        && generation == m_lyricsGeneration && media == m_lyricsMedia) {
        m_lyricsRequest = m_hub->loadLyrics(media);
        if (changed) m_artworkRequest = m_hub->loadArtwork(media);
    }
}
void OriginalUiMusicAdapter::retryCurrentLyrics()
{
    if (m_currentLyricsState == QStringLiteral("failed")) syncCurrentLyrics(true);
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
bool OriginalUiMusicAdapter::searchLoading() const
{
    return m_hub && m_hub->searchResults()->state() == PageLoadStateV2::Loading;
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
OnlineListModel *OriginalUiMusicAdapter::favoriteArtists() const { return m_favoriteArtists; }
QString OriginalUiMusicAdapter::favoriteArtistsState() const
{
    if (!m_hub) return QStringLiteral("unavailable");
    if (m_hub->favorites()->state() == PageLoadStateV2::Loading) return QStringLiteral("loading");
    if (m_favoriteArtists->rowCount() > 0) return QStringLiteral("ready");
    return m_favoriteArtists->error().isEmpty() ? QStringLiteral("empty") : QStringLiteral("failed");
}
OnlineListModel *OriginalUiMusicAdapter::searchSongs() const { return m_searchSongs; }
OnlineListModel *OriginalUiMusicAdapter::searchLists() const { return m_searchLists; }
OnlineListModel *OriginalUiMusicAdapter::searchAlbums() const { return m_searchAlbums; }
OnlineListModel *OriginalUiMusicAdapter::searchLyrics() const { return m_searchLyrics; }
OnlineListModel *OriginalUiMusicAdapter::directoryItems() const { return m_directoryItems; }
QString OriginalUiMusicAdapter::directoryState() const
{
    if (!m_hub || !m_hub->directoryLibrary()) return QStringLiteral("unavailable");
    auto *model = m_hub->directoryLibrary()->model();
    switch (model->state()) {
    case PageLoadStateV2::Loading: return QStringLiteral("loading");
    case PageLoadStateV2::Ready: return QStringLiteral("ready");
    case PageLoadStateV2::Empty: return QStringLiteral("empty");
    case PageLoadStateV2::Failed: {
        const auto error = model->errorMap();
        const bool failed = (!error.isEmpty() && error.value("kind").toInt() != int(SourceErrorKindV2::Unsupported))
            || !aggregateSectionState(model, {PageSectionKindV2::Tracks}).value("error").toMap().isEmpty();
        return failed ? QStringLiteral("failed") : QStringLiteral("empty");
    }
    case PageLoadStateV2::Idle: return QStringLiteral("empty");
    }
    return QStringLiteral("unavailable");
}
bool OriginalUiMusicAdapter::directoryCanNavigateBack() const
{
    return m_hub && m_hub->directoryLibrary() && m_hub->directoryLibrary()->canNavigateBack();
}
QString OriginalUiMusicAdapter::directoryContextToken() const
{
    return m_hub && m_hub->directoryLibrary() ? m_hub->directoryLibrary()->contextToken() : QString{};
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
void OriginalUiMusicAdapter::retryDirectorySection(const QString &sectionId)
{
    if (m_hub && m_hub->directoryLibrary()) m_hub->directoryLibrary()->retry(sectionId);
}
bool OriginalUiMusicAdapter::pluginAvailable(const QString &packageId) const
{
    return m_hub && m_hub->sourcePluginLoaded(packageId);
}

QVariantMap OriginalUiMusicAdapter::capabilitiesForItem(const QVariantMap &item) const
{
    const auto browse = browseCapability(item);
    const auto effective = [this, &item](SourceActionV2 action) -> QVariantMap {
        if (!m_hub) return unavailable();
        const auto capability = m_hub->presentationAction(item, action);
        return capability.state == AvailabilityV2::Available
            ? QVariantMap{{QStringLiteral("enabled"), true}, {QStringLiteral("reasonKey"), QString{}}}
            : unavailable(safeReasonKey(capability.reasonKey));
    };
    const auto play = effective(SourceActionV2::Play);
    const auto favorite = effective(SourceActionV2::Favorite);
    const auto unfavorite = effective(SourceActionV2::Unfavorite);
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
    m_personalRadio->setItems({});
    m_personalRadar->setItems({});
    m_categoryItems->setItems({});
    m_categorySongs->setItems({});
    m_categoryArtists->setItems({});
    m_categoryPlaylists->setItems({});
    m_categoryCharts->setItems({});
    m_favoriteSongs->setItems({});
    m_favoriteLists->setItems({});
    m_favoriteArtists->setItems({});
    m_searchSongs->setItems({});
    m_searchLists->setItems({});
    m_searchAlbums->setItems({});
    m_searchLyrics->setItems({});
    m_directoryItems->setItems({});
    m_directoryKeys.clear();
    for (OnlineListModel *model : {m_recommendSongs, m_personalRadio, m_personalRadar, m_categoryItems, m_categorySongs, m_favoriteSongs,
                                   m_categoryArtists, m_categoryPlaylists, m_categoryCharts,
                                   m_favoriteLists, m_favoriteArtists, m_searchSongs, m_searchLists,
                                   m_searchAlbums, m_searchLyrics, m_directoryItems})
        model->setPresentationState({});
    emit favoriteStatusChanged();
}

void OriginalUiMusicAdapter::rebuild()
{
    QVariantList recommendations;
    QVariantList radio, radar;
    QVariantList category;
    QVariantList favoriteSongs;
    QVariantList favoriteLists;
    QVariantList favoriteArtists;
    QVariantList searchSongs;
    QVariantList searchLists;
    QVariantList searchAlbums;
    QVariantList searchLyrics;
    m_fullItems.clear();
    m_directoryKeys.clear();

    const auto append = [this](MusicPageModel *model, QVariantList *target,
                               QVariantList *tracks = nullptr, QVariantList *playlists = nullptr,
                               bool privateFeed = false, QVariantList *artists = nullptr) {
        for (int section = 0; section < model->rowCount(); ++section) {
            const auto items = model->data(model->index(section), MusicPageModel::ItemsRole).toList();
            const auto sectionIndex = model->index(section);
            const QVariantMap sectionState{
                {QStringLiteral("sectionId"), model->data(sectionIndex, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(sectionIndex, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(sectionIndex, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), privateFeed
                    ? aggregateSectionState(model, {PageSectionKindV2::Tracks}, true).value("error")
                    : model->data(sectionIndex, MusicPageModel::ErrorRole)}};
            for (int index = 0; index < items.size(); ++index) {
                const QVariantMap full = model->itemAt(section, index);
                const int entityType = full.value(QStringLiteral("ref")).toMap()
                    .value(QStringLiteral("entityType")).toInt();
                if (tracks || playlists || artists) {
                    if (tracks && entityType == int(MediaEntityTypeV2::Track)) tracks->append(presentationItem(full, sectionState));
                    else if (playlists && entityType == int(MediaEntityTypeV2::Playlist)) playlists->append(presentationItem(full, sectionState));
                    else if (artists && entityType == int(MediaEntityTypeV2::Artist)) {
                        auto safeState = sectionState;
                        safeState["error"] = aggregateSectionState(model, {PageSectionKindV2::FavoriteArtists}, true).value("error");
                        artists->append(presentationItem(full, safeState));
                    }
                } else target->append(presentationItem(full, sectionState));
            }
        }
    };

    if (m_hub) {
        append(m_hub->recommendation(), &recommendations);
        append(m_hub->discovery(0), &radio, nullptr, nullptr, true);
        append(m_hub->discovery(1), &radar, nullptr, nullptr, true);
        append(m_hub->category(), &category);
        append(m_hub->favorites(), nullptr, &favoriteSongs, &favoriteLists, false, &favoriteArtists);
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
    m_personalRadio->setItems(radio);
    m_personalRadar->setItems(radar);
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
    m_favoriteArtists->setItems(favoriteArtists);
    m_searchSongs->setItems(searchSongs);
    m_searchLists->setItems(searchLists);
    m_searchAlbums->setItems(searchAlbums);
    m_searchLyrics->setItems(searchLyrics);
    if (m_hub) {
        m_personalRadio->setPresentationState(aggregateSectionState(m_hub->discovery(0), {PageSectionKindV2::Tracks}, true));
        m_personalRadar->setPresentationState(aggregateSectionState(m_hub->discovery(1), {PageSectionKindV2::Tracks}, true));
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
        m_favoriteArtists->setPresentationState(aggregateSectionState(m_hub->favorites(), {PageSectionKindV2::FavoriteArtists}, true));
        m_searchSongs->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Tracks}));
        m_searchLists->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Playlists}));
        m_searchAlbums->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Albums}));
        m_searchLyrics->setPresentationState(aggregateSectionState(m_hub->searchResults(), {PageSectionKindV2::Tracks}));
    }
    rebuildDirectories();
    emit categoryStatusChanged();
    emit discoveryStatusChanged();
    emit favoriteStatusChanged();
}

OnlineListModel *OriginalUiMusicAdapter::personalRadio() const { return m_personalRadio; }
OnlineListModel *OriginalUiMusicAdapter::personalRadar() const { return m_personalRadar; }
QString OriginalUiMusicAdapter::personalRadioState() const { return discoveryState(0); }
QString OriginalUiMusicAdapter::personalRadarState() const { return discoveryState(1); }
QString OriginalUiMusicAdapter::discoveryState(int kind) const
{
    auto *model = m_hub ? m_hub->discovery(kind) : nullptr;
    if (!model) return QStringLiteral("unsupported");
    switch (model->state()) {
    case PageLoadStateV2::Idle: return QStringLiteral("idle");
    case PageLoadStateV2::Loading: return QStringLiteral("loading");
    case PageLoadStateV2::Ready: return QStringLiteral("ready");
    case PageLoadStateV2::Empty: return QStringLiteral("empty");
    case PageLoadStateV2::Failed: {
        bool supportedEmpty = false, blockingError = false;
        auto failure = model->error().kind;
        for (const auto &value : model->sourceStates()) {
            const auto source = value.toMap();
            supportedEmpty |= source.value("state").toInt() == int(SourcePageLoadStateV2::Empty)
                && source.value("error").toMap().isEmpty();
        }
        for (int i = 0; i < model->rowCount(); ++i)
            for (const auto &value : model->data(model->index(i), MusicPageModel::ErrorRole).toMap()) {
                const auto kind = SourceErrorKindV2(value.toMap().value("kind").toInt());
                if (kind != SourceErrorKindV2::Unsupported) {
                    if (!blockingError) failure = kind;
                    blockingError = true;
                }
            }
        // The generic model reports a partially unsupported empty aggregate as
        // Failed. For an optional feed, successful-empty is not unsupported.
        if (supportedEmpty && !blockingError) return QStringLiteral("empty");
        if (failure == SourceErrorKindV2::Unsupported) return QStringLiteral("unsupported");
        if (failure == SourceErrorKindV2::Authorization && !supportedEmpty) return QStringLiteral("forbidden");
        return QStringLiteral("failed");
    }
    }
    return QStringLiteral("failed");
}
void OriginalUiMusicAdapter::refreshDiscovery(int kind) { if (m_hub) m_hub->refreshDiscovery(kind); }
void OriginalUiMusicAdapter::loadMoreDiscovery(int kind, const QString &id) { if (m_hub) m_hub->loadMoreDiscovery(kind, id); }
void OriginalUiMusicAdapter::retryDiscovery(int kind, const QString &id) { if (m_hub) m_hub->retryDiscovery(kind, id); }
void OriginalUiMusicAdapter::closeDiscovery(int kind) { if (m_hub) m_hub->closeDiscovery(kind); }

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
            QVariantMap state{
                {QStringLiteral("sectionId"), model->data(index, MusicPageModel::SectionIdRole)},
                {QStringLiteral("hasMore"), model->data(index, MusicPageModel::HasMoreRole)},
                {QStringLiteral("loadingMore"), model->data(index, MusicPageModel::LoadingMoreRole)},
                {QStringLiteral("error"), model->data(index, MusicPageModel::ErrorRole)}};
            bool failed = false;
            for (const auto &error : state.value(QStringLiteral("error")).toMap())
                failed |= error.toMap().value("kind").toInt() != int(SourceErrorKindV2::Unsupported);
            const QVariantMap error = failed ? QVariantMap{{"failed", true}} : QVariantMap{};
            state.insert(QStringLiteral("error"), error);
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
        m_directoryItems->setPresentationState(aggregateSectionState(model, {PageSectionKindV2::Tracks}));
    } else m_directoryItems->setPresentationState({});
    m_directoryItems->setItems(rows);
}
