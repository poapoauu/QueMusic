#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>
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
    Q_PROPERTY(OnlineListModel *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *searchLists READ searchLists CONSTANT)
    Q_PROPERTY(OnlineListModel *searchAlbums READ searchAlbums CONSTANT)
    Q_PROPERTY(OnlineListModel *searchLyrics READ searchLyrics CONSTANT)
    Q_PROPERTY(OnlineListModel *directoryItems READ directoryItems CONSTANT)
    Q_PROPERTY(QString directoryState READ directoryState NOTIFY directoryChanged)
    Q_PROPERTY(bool directoryCanNavigateBack READ directoryCanNavigateBack NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    explicit OriginalUiMusicAdapter(MusicHub *hub, PlaybackCoordinator *playback,
                                   QObject *parent = nullptr);

    OnlineListModel *recommendSongs() const;
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
    OnlineListModel *searchSongs() const;
    OnlineListModel *searchLists() const;
    OnlineListModel *searchAlbums() const;
    OnlineListModel *searchLyrics() const;
    OnlineListModel *directoryItems() const;
    QString directoryState() const;
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
    Q_INVOKABLE bool pluginAvailable(const QString &packageId) const;

signals:
    void categoryStatusChanged();
    void categoryNavigationChanged();
    void sourceOptionsChanged();
    void selectedSourceInstanceIdChanged();
    void directoryChanged();

private:
    QVariantMap resolvePresentationItem(const QVariantMap &presentationItem) const;
    QVariantMap capabilitiesFor(const QVariantList &rows) const;
    QVariantMap capabilitiesForItem(const QVariantMap &fullItem) const;
    bool permits(const QVariantMap &fullItem, const QString &capability) const;
    QVariantMap presentationItem(const QVariantMap &fullItem, const QVariantMap &sectionState);
    void clearPresentationState();
    void rebuild();
    void rebuildDirectories();

    QPointer<MusicHub> m_hub;
    QPointer<PlaybackCoordinator> m_playback;
    OnlineListModel *m_recommendSongs;
    OnlineListModel *m_categoryItems;
    OnlineListModel *m_categorySongs;
    OnlineListModel *m_categoryArtists;
    OnlineListModel *m_categoryPlaylists;
    OnlineListModel *m_categoryCharts;
    OnlineListModel *m_favoriteSongs;
    OnlineListModel *m_favoriteLists;
    OnlineListModel *m_searchSongs;
    OnlineListModel *m_searchLists;
    OnlineListModel *m_searchAlbums;
    OnlineListModel *m_searchLyrics;
    OnlineListModel *m_directoryItems;
    QHash<quint64, QVariantMap> m_fullItems;
    QSet<quint64> m_directoryKeys;
    quint64 m_nextAdapterKey = 1;
};
