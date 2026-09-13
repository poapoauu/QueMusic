#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
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
    Q_PROPERTY(OnlineListModel *favoriteSongs READ favoriteSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *favoriteLists READ favoriteLists CONSTANT)
    Q_PROPERTY(OnlineListModel *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    explicit OriginalUiMusicAdapter(MusicHub *hub, PlaybackCoordinator *playback,
                                   QObject *parent = nullptr);

    OnlineListModel *recommendSongs() const;
    OnlineListModel *categoryItems() const;
    OnlineListModel *favoriteSongs() const;
    OnlineListModel *favoriteLists() const;
    OnlineListModel *searchSongs() const;
    QVariantList sourceOptions() const;
    QString selectedSourceInstanceId() const;
    void setSelectedSourceInstanceId(const QString &id);

    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE bool browse(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid play(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid enqueue(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &presentationItem, bool favorite);
    Q_INVOKABLE QVariantMap fullItem(const QVariantMap &presentationItem) const;

signals:
    void sourceOptionsChanged();
    void selectedSourceInstanceIdChanged();

private:
    QVariantMap presentationItem(const QVariantMap &fullItem);
    void rebuild();

    QPointer<MusicHub> m_hub;
    QPointer<PlaybackCoordinator> m_playback;
    OnlineListModel *m_recommendSongs;
    OnlineListModel *m_categoryItems;
    OnlineListModel *m_favoriteSongs;
    OnlineListModel *m_favoriteLists;
    OnlineListModel *m_searchSongs;
    QHash<quint64, QVariantMap> m_fullItems;
    quint64 m_nextAdapterKey = 1;
};
