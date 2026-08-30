#pragma once

#include "MediaTypes.h"

#include <QAbstractListModel>
#include <QUuid>

class MediaListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(MediaRequestState requestState READ requestState NOTIFY requestStateChanged)
    Q_PROPERTY(MediaErrorKind errorKind READ errorKind NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY canRetryChanged)

public:
    enum Role {
        SourceIdRole = Qt::UserRole + 1,
        AccountIdRole,
        NativeIdRole,
        KindRole,
        TitleRole,
        SubtitleRole,
        ArtistsRole,
        AlbumTitleRole,
        DurationMsRole,
        ArtworkUrlRole,
        PlayableRole,
        ContainerRole,
        ExtraRole,
    };

    explicit MediaListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    MediaRequestState requestState() const;
    MediaErrorKind errorKind() const;
    QString errorMessage() const;
    bool hasMore() const;
    bool canRetry() const;

    Q_INVOKABLE QVariantMap get(int index) const;

    void beginRequest(const QUuid &requestId);
    void replacePage(const MediaPage &page);
    void appendPage(const MediaPage &page);
    void setFailure(const MediaError &error);
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void requestStateChanged();
    void errorChanged();
    void hasMoreChanged();
    void canRetryChanged();

private:
    void setRequestState(MediaRequestState state);
    void setError(const MediaError &error);
    void setHasMore(bool hasMore);
    void setCanRetry(bool canRetry);

    QList<MediaItem> m_items;
    QUuid m_requestId;
    MediaRequestState m_requestState = MediaRequestState::Idle;
    MediaError m_error;
    bool m_hasMore = false;
    bool m_canRetry = false;
};
