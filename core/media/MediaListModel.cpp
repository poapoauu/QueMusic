#include "MediaListModel.h"

namespace {

const QHash<int, QByteArray> mediaRoles{
    {MediaListModel::SourceIdRole, "sourceId"},
    {MediaListModel::AccountIdRole, "accountId"},
    {MediaListModel::NativeIdRole, "nativeId"},
    {MediaListModel::KindRole, "kind"},
    {MediaListModel::TitleRole, "title"},
    {MediaListModel::SubtitleRole, "subtitle"},
    {MediaListModel::ArtistsRole, "artists"},
    {MediaListModel::AlbumTitleRole, "albumTitle"},
    {MediaListModel::DurationMsRole, "durationMs"},
    {MediaListModel::ArtworkUrlRole, "artworkUrl"},
    {MediaListModel::PlayableRole, "playable"},
    {MediaListModel::ContainerRole, "container"},
    {MediaListModel::ExtraRole, "extra"},
};

}

MediaListModel::MediaListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MediaListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MediaListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const MediaItem &item = m_items.at(index.row());
    switch (role) {
    case SourceIdRole:
        return item.id.sourceId;
    case AccountIdRole:
        return item.id.accountId;
    case NativeIdRole:
        return item.id.nativeId;
    case KindRole:
        return static_cast<int>(item.id.kind);
    case TitleRole:
        return item.title;
    case SubtitleRole:
        return item.subtitle;
    case ArtistsRole:
        return item.artists;
    case AlbumTitleRole:
        return item.albumTitle;
    case DurationMsRole:
        return item.durationMs;
    case ArtworkUrlRole:
        return item.artworkUrl;
    case PlayableRole:
        return item.playable;
    case ContainerRole:
        return item.container;
    case ExtraRole:
        return item.extra;
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaListModel::roleNames() const
{
    return mediaRoles;
}

MediaRequestState MediaListModel::requestState() const
{
    return m_requestState;
}

MediaErrorKind MediaListModel::errorKind() const
{
    return m_error.kind;
}

QString MediaListModel::errorMessage() const
{
    return m_error.message;
}

bool MediaListModel::hasMore() const
{
    return m_hasMore;
}

bool MediaListModel::canRetry() const
{
    return m_canRetry;
}

QVariantMap MediaListModel::get(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    return mediaItemToVariantMap(m_items.at(index));
}

void MediaListModel::beginRequest(const QUuid &requestId)
{
    m_requestId = requestId;
    setError({});
    setCanRetry(false);
    setRequestState(MediaRequestState::Loading);
}

void MediaListModel::replacePage(const MediaPage &page)
{
    beginResetModel();
    m_items = page.items;
    endResetModel();
    emit countChanged();

    setError({});
    setCanRetry(false);
    setHasMore(page.hasMore);
    setRequestState(m_items.isEmpty() ? MediaRequestState::Empty : MediaRequestState::Ready);
}

void MediaListModel::appendPage(const MediaPage &page)
{
    if (!page.items.isEmpty()) {
        const int first = m_items.size();
        beginInsertRows(QModelIndex(), first, first + page.items.size() - 1);
        m_items.append(page.items);
        endInsertRows();
        emit countChanged();
    }

    setError({});
    setCanRetry(false);
    setHasMore(page.hasMore);
    setRequestState(m_items.isEmpty() ? MediaRequestState::Empty : MediaRequestState::Ready);
}

void MediaListModel::setFailure(const MediaError &error)
{
    setError(error);
    setHasMore(false);
    setCanRetry(error.retryable);
    setRequestState(MediaRequestState::Failed);
}

void MediaListModel::clear()
{
    m_requestId = {};
    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }
    setError({});
    setHasMore(false);
    setCanRetry(false);
    setRequestState(MediaRequestState::Idle);
}

void MediaListModel::setRequestState(MediaRequestState state)
{
    if (m_requestState == state) {
        return;
    }
    m_requestState = state;
    emit requestStateChanged();
}

void MediaListModel::setError(const MediaError &error)
{
    if (m_error.kind == error.kind && m_error.message == error.message) {
        return;
    }
    m_error = error;
    emit errorChanged();
}

void MediaListModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore) {
        return;
    }
    m_hasMore = hasMore;
    emit hasMoreChanged();
}

void MediaListModel::setCanRetry(bool canRetry)
{
    if (m_canRetry == canRetry) {
        return;
    }
    m_canRetry = canRetry;
    emit canRetryChanged();
}
