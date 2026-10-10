#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class MusicHub;
class OnlineListModel;
class PlaybackCoordinator;

// Presents v2 page results through the original UI's small, stable role set.
// Full source-owned item maps remain private and are recovered only by the
// opaque presentation key when routing an action.
class OriginalUiMusicAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(OnlineListModel *recommendSongs READ recommendSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *personalRadio READ personalRadio CONSTANT)
    Q_PROPERTY(OnlineListModel *personalRadar READ personalRadar CONSTANT)
    Q_PROPERTY(QString personalRadioState READ personalRadioState NOTIFY discoveryStatusChanged)
    Q_PROPERTY(QString personalRadarState READ personalRadarState NOTIFY discoveryStatusChanged)
    Q_PROPERTY(OnlineListModel *categoryItems READ categoryItems CONSTANT)
    Q_PROPERTY(OnlineListModel *categorySongs READ categorySongs CONSTANT)
    Q_PROPERTY(OnlineListModel *categoryArtists READ categoryArtists CONSTANT)
    Q_PROPERTY(OnlineListModel *categoryPlaylists READ categoryPlaylists CONSTANT)
    Q_PROPERTY(OnlineListModel *categoryCharts READ categoryCharts CONSTANT)
    Q_PROPERTY(QString categoryState READ categoryState NOTIFY categoryStatusChanged)
    Q_PROPERTY(bool categoryHasError READ categoryHasError NOTIFY categoryStatusChanged)
    Q_PROPERTY(bool categoryCanNavigateBack READ categoryCanNavigateBack NOTIFY categoryNavigationChanged)
    Q_PROPERTY(QString categoryTitle READ categoryTitle NOTIFY categoryNavigationChanged)
    Q_PROPERTY(QString categoryCover READ categoryCover NOTIFY categoryNavigationChanged)
    Q_PROPERTY(OnlineListModel *favoriteSongs READ favoriteSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *favoriteLists READ favoriteLists CONSTANT)
    Q_PROPERTY(OnlineListModel *favoriteArtists READ favoriteArtists CONSTANT)
    Q_PROPERTY(QString favoriteArtistsState READ favoriteArtistsState NOTIFY favoriteStatusChanged)
    Q_PROPERTY(OnlineListModel *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *searchLists READ searchLists CONSTANT)
    Q_PROPERTY(OnlineListModel *searchAlbums READ searchAlbums CONSTANT)
    Q_PROPERTY(OnlineListModel *searchLyrics READ searchLyrics CONSTANT)
    Q_PROPERTY(bool searchLoading READ searchLoading NOTIFY searchStatusChanged)
    Q_PROPERTY(OnlineListModel *directoryItems READ directoryItems CONSTANT)
    Q_PROPERTY(QString directoryState READ directoryState NOTIFY directoryChanged)
    Q_PROPERTY(QString directoryContextToken READ directoryContextToken NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList currentLyrics READ currentLyrics NOTIFY currentLyricsChanged)
    Q_PROPERTY(QString currentLyricsState READ currentLyricsState NOTIFY currentLyricsChanged)
    Q_PROPERTY(QUrl currentCover READ currentCover NOTIFY currentCoverChanged)
    Q_PROPERTY(QVariantMap currentFavorite READ currentFavorite NOTIFY currentFavoriteChanged)
    Q_PROPERTY(QVariantMap currentDownload READ currentDownload NOTIFY currentDownloadChanged)
    Q_PROPERTY(QVariantList downloadTasks READ downloadTasks NOTIFY downloadTasksChanged)
    Q_PROPERTY(bool directoryCanNavigateBack READ directoryCanNavigateBack NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    explicit OriginalUiMusicAdapter(MusicHub *hub, PlaybackCoordinator *playback,
                                   QObject *parent = nullptr);
    ~OriginalUiMusicAdapter() override;
    QVariantList currentLyrics() const;
    QString currentLyricsState() const;
    QUrl currentCover() const;
    QVariantMap currentFavorite() const;
    Q_INVOKABLE QUuid setCurrentFavorite(bool favorite, const QString &expectedToken = {});
    QVariantMap currentDownload() const;
    Q_INVOKABLE QUuid downloadCurrent(const QUrl &destination, const QString &expectedToken);
    QVariantList downloadTasks() const;
    // Removes only a terminal presentation record, never a request or a file.
    Q_INVOKABLE bool dismissDownloadTask(const QString &taskId);
    Q_INVOKABLE bool cancelDownloadTask(const QString &taskId);
    Q_INVOKABLE void retryCurrentLyrics();

    OnlineListModel *recommendSongs() const;
    OnlineListModel *personalRadio() const;
    OnlineListModel *personalRadar() const;
    QString personalRadioState() const;
    QString personalRadarState() const;
    Q_INVOKABLE void refreshDiscovery(int kind);
    Q_INVOKABLE void loadMoreDiscovery(int kind, const QString &sectionId);
    Q_INVOKABLE void retryDiscovery(int kind, const QString &sectionId);
    Q_INVOKABLE void closeDiscovery(int kind);
    OnlineListModel *categoryItems() const;
    OnlineListModel *categorySongs() const;
    OnlineListModel *categoryArtists() const;
    OnlineListModel *categoryPlaylists() const;
    OnlineListModel *categoryCharts() const;
    QString categoryState() const;
    bool categoryHasError() const;
    bool categoryCanNavigateBack() const;
    QString categoryTitle() const;
    QString categoryCover() const;
    OnlineListModel *favoriteSongs() const;
    OnlineListModel *favoriteLists() const;
    OnlineListModel *favoriteArtists() const;
    QString favoriteArtistsState() const;
    OnlineListModel *searchSongs() const;
    OnlineListModel *searchLists() const;
    OnlineListModel *searchAlbums() const;
    OnlineListModel *searchLyrics() const;
    bool searchLoading() const;
    OnlineListModel *directoryItems() const;
    QString directoryState() const;
    QString directoryContextToken() const;
    bool directoryCanNavigateBack() const;
    QVariantList sourceOptions() const;
    QString selectedSourceInstanceId() const;
    void setSelectedSourceInstanceId(const QString &id);

    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void refreshPage(int pageKind);
    // searchTab follows the original UI order: songs, playlists, albums, lyrics.
    Q_INVOKABLE void search(const QString &text, int searchTab = 0);
    // The argument is either one opaque presentation row or a list of rows.
    // Returned values are limited to booleans and vetted reason keys.
    Q_INVOKABLE QVariantMap capabilities(const QVariant &rows) const;
    Q_INVOKABLE void loadMore(int pageKind, const QString &sectionId);
    Q_INVOKABLE void retry(int pageKind, const QString &sectionId);
    Q_INVOKABLE bool browse(const QVariantMap &presentationItem);
    Q_INVOKABLE void closeCategoryBrowse();
    Q_INVOKABLE bool categoryBack();
    Q_INVOKABLE QUuid play(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid enqueue(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &presentationItem, bool favorite);
    Q_INVOKABLE void activateDirectories();
    Q_INVOKABLE bool browseDirectory(const QVariantMap &presentationItem);
    Q_INVOKABLE bool directoryBack();
    Q_INVOKABLE void refreshDirectories();
    Q_INVOKABLE void loadMoreDirectories(const QString &sectionId);
    Q_INVOKABLE void retryDirectorySection(const QString &sectionId);
    Q_INVOKABLE bool pluginAvailable(const QString &packageId) const;

signals:
    void searchStatusChanged();
    void favoriteStatusChanged();
    void discoveryStatusChanged();
    void categoryStatusChanged();
    void categoryNavigationChanged();
    void sourceOptionsChanged();
    void selectedSourceInstanceIdChanged();
    void directoryChanged();
    void currentLyricsChanged();
    void currentCoverChanged();
    void currentFavoriteChanged();
    void currentDownloadChanged();
    void downloadTasksChanged();

private:
    QVariantMap resolvePresentationItem(const QVariantMap &presentationItem) const;
    QVariantMap capabilitiesFor(const QVariantList &rows) const;
    QVariantMap capabilitiesForItem(const QVariantMap &fullItem) const;
    bool permits(const QVariantMap &fullItem, const QString &capability) const;
    QVariantMap presentationItem(const QVariantMap &fullItem, const QVariantMap &sectionState);
    void clearPresentationState();
    void rebuild();
    QString discoveryState(int kind) const;
    void rebuildDirectories();
    void syncCurrentLyrics(bool force = false);
    void cancelCurrentLyrics();
    void cancelCurrentCover();
    bool currentAssetRequestIsCurrent(const QUuid &id, const QUuid &request) const;
    bool lyricsRequestIsCurrent(const QUuid &id) const;
    void clearCurrentLyrics();
    void syncCurrentFavorite(bool notify = true);
    bool favoriteRequestIsCurrent(const QUuid &id) const;
    void syncCurrentDownload(bool notify = true);
    bool downloadRequestIsCurrent(const QUuid &id) const;
    void finishDownload(const QUuid &id, const QVariantMap &result, bool succeeded, bool cancelled = false);
    void failPendingDownloadTasks();
    struct DownloadTask {
        QVariantMap presentation;
        QUuid request;
        QVariantMap media;
        QUrl destination;
    };
    QList<DownloadTask> m_downloadTasks;

    QPointer<MusicHub> m_hub;
    QPointer<PlaybackCoordinator> m_playback;
    OnlineListModel *m_recommendSongs;
    OnlineListModel *m_personalRadio;
    OnlineListModel *m_personalRadar;
    OnlineListModel *m_categoryItems;
    OnlineListModel *m_categorySongs;
    OnlineListModel *m_categoryArtists;
    OnlineListModel *m_categoryPlaylists;
    OnlineListModel *m_categoryCharts;
    OnlineListModel *m_favoriteSongs;
    OnlineListModel *m_favoriteLists;
    OnlineListModel *m_favoriteArtists;
    OnlineListModel *m_searchSongs;
    OnlineListModel *m_searchLists;
    OnlineListModel *m_searchAlbums;
    OnlineListModel *m_searchLyrics;
    OnlineListModel *m_directoryItems;
    QHash<quint64, QVariantMap> m_fullItems;
    QSet<quint64> m_directoryKeys;
    quint64 m_nextAdapterKey = 1;
    QUuid m_lyricsGeneration;
    QUuid m_lyricsRequest;
    QUuid m_artworkRequest;
    QUrl m_currentCover;
    QVariantMap m_lyricsMedia;
    QVariantList m_currentLyrics;
    QString m_currentLyricsState = QStringLiteral("idle");
    QUuid m_favoriteGeneration;
    QVariantMap m_favoriteMedia;
    QUuid m_favoriteRequest;
    QString m_favoriteState = QStringLiteral("unknown");
    bool m_favoriteFailed = false;
    QUuid m_downloadGeneration;
    QVariantMap m_downloadMedia;
    QUuid m_downloadRequest;
    QUrl m_downloadDestination;
    bool m_downloadFailed = false;
    bool m_downloadCompleted = false;
};
