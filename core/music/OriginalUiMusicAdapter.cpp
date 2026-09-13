#include "OriginalUiMusicAdapter.h"

#include "MusicHub.h"
#include "MusicPageModel.h"
#include "PlaybackCoordinator.h"
#include "OnlineListModel.h"

namespace {

QString artistName(const QVariantMap &item)
{
    const QStringList artists = item.value(QStringLiteral("artists")).toStringList();
    return artists.join(QStringLiteral(", "));
}

} // namespace

OriginalUiMusicAdapter::OriginalUiMusicAdapter(MusicHub *hub, PlaybackCoordinator *playback,
                                               QObject *parent)
    : QObject(parent), m_hub(hub), m_playback(playback),
      m_recommendSongs(new OnlineListModel(this)), m_categoryItems(new OnlineListModel(this)),
      m_favoriteSongs(new OnlineListModel(this)), m_favoriteLists(new OnlineListModel(this)),
      m_searchSongs(new OnlineListModel(this))
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
    observe(m_hub->favorites());
    observe(m_hub->searchResults());
    connect(m_hub, &MusicHub::sourceOptionsChanged, this, &OriginalUiMusicAdapter::sourceOptionsChanged);
    connect(m_hub, &MusicHub::selectedSourceInstanceIdChanged,
            this, &OriginalUiMusicAdapter::selectedSourceInstanceIdChanged);
    connect(m_hub, &QObject::destroyed, this, [this] {
        m_hub = nullptr;
        clearPresentationState();
        emit sourceOptionsChanged();
        emit selectedSourceInstanceIdChanged();
    });
    rebuild();
}

OnlineListModel *OriginalUiMusicAdapter::recommendSongs() const { return m_recommendSongs; }
OnlineListModel *OriginalUiMusicAdapter::categoryItems() const { return m_categoryItems; }
OnlineListModel *OriginalUiMusicAdapter::favoriteSongs() const { return m_favoriteSongs; }
OnlineListModel *OriginalUiMusicAdapter::favoriteLists() const { return m_favoriteLists; }
OnlineListModel *OriginalUiMusicAdapter::searchSongs() const { return m_searchSongs; }
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

void OriginalUiMusicAdapter::search(const QString &text)
{
    if (m_hub) m_hub->search(text);
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
    return m_hub && !item.isEmpty() && m_hub->browse(item);
}

QUuid OriginalUiMusicAdapter::play(const QVariantMap &row)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_playback && !item.isEmpty() ? m_playback->play(item) : QUuid{};
}

QUuid OriginalUiMusicAdapter::enqueue(const QVariantMap &row)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_playback && !item.isEmpty() ? m_playback->enqueue(item) : QUuid{};
}

QUuid OriginalUiMusicAdapter::setFavorite(const QVariantMap &row, bool favorite)
{
    const QVariantMap item = resolvePresentationItem(row);
    return m_hub && m_hub->actions() && !item.isEmpty()
        ? m_hub->actions()->setFavorite(item, favorite) : QUuid{};
}

QVariantMap OriginalUiMusicAdapter::presentationItem(const QVariantMap &full)
{
    const QVariantMap ref = full.value(QStringLiteral("ref")).toMap();
    const quint64 key = m_nextAdapterKey++;
    m_fullItems.insert(key, full);
    return {{QStringLiteral("title"), full.value(QStringLiteral("title")).toString()},
            {QStringLiteral("artist"), artistName(full)},
            {QStringLiteral("album"), full.value(QStringLiteral("album")).toString()},
            {QStringLiteral("cover"), full.value(QStringLiteral("artworkId")).toString()},
            {QStringLiteral("duration"), full.value(QStringLiteral("durationMs"))},
            {QStringLiteral("source"), ref.value(QStringLiteral("sourcePluginId")).toString()},
            {QStringLiteral("entityType"), ref.value(QStringLiteral("entityType"))},
            {QStringLiteral("subtitle"), full.value(QStringLiteral("subtitle")).toString()},
            {QStringLiteral("_adapterKey"), QVariant::fromValue<qulonglong>(key)}};
}

void OriginalUiMusicAdapter::clearPresentationState()
{
    m_fullItems.clear();
    m_recommendSongs->setItems({});
    m_categoryItems->setItems({});
    m_favoriteSongs->setItems({});
    m_favoriteLists->setItems({});
    m_searchSongs->setItems({});
}

void OriginalUiMusicAdapter::rebuild()
{
    QVariantList recommendations;
    QVariantList category;
    QVariantList favoriteSongs;
    QVariantList favoriteLists;
    QVariantList search;
    m_fullItems.clear();

    const auto append = [this](MusicPageModel *model, QVariantList *target,
                               QVariantList *tracks = nullptr, QVariantList *playlists = nullptr) {
        for (int section = 0; section < model->rowCount(); ++section) {
            const auto items = model->data(model->index(section), MusicPageModel::ItemsRole).toList();
            for (int index = 0; index < items.size(); ++index) {
                const QVariantMap full = model->itemAt(section, index);
                const int entityType = full.value(QStringLiteral("ref")).toMap()
                    .value(QStringLiteral("entityType")).toInt();
                if (tracks || playlists) {
                    if (entityType == int(MediaEntityTypeV2::Track)) tracks->append(presentationItem(full));
                    else if (entityType == int(MediaEntityTypeV2::Playlist)) playlists->append(presentationItem(full));
                } else target->append(presentationItem(full));
            }
        }
    };

    if (m_hub) {
        append(m_hub->recommendation(), &recommendations);
        append(m_hub->category(), &category);
        append(m_hub->favorites(), nullptr, &favoriteSongs, &favoriteLists);
        append(m_hub->searchResults(), &search);
    }
    m_recommendSongs->setItems(recommendations);
    m_categoryItems->setItems(category);
    m_favoriteSongs->setItems(favoriteSongs);
    m_favoriteLists->setItems(favoriteLists);
    m_searchSongs->setItems(search);
}
