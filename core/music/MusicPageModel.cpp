#include "MusicPageModel.h"

#include <algorithm>

namespace {

QVariantMap errorToMap(const SourceErrorV2 &error)
{
    QVariantMap map{{"kind", static_cast<int>(error.kind)},
                    {"messageKey", error.messageKey}, {"detail", error.detail},
                    {"retryable", error.retryable}};
    if (error.httpStatus)
        map.insert("httpStatus", *error.httpStatus);
    return map;
}

QVariantMap itemToMap(const MediaItemV2 &item)
{
    QVariantMap actions;
    for (auto it = item.availableActions.cbegin(); it != item.availableActions.cend(); ++it) {
        actions.insert(QString::number(static_cast<int>(it.key())),
                       actionAvailabilityV2ToJson(it.value()).toVariantMap());
    }
    return {{"ref", mediaRefV2ToVariantMap(item.ref)}, {"title", item.title},
            {"subtitle", item.subtitle}, {"artists", item.artists}, {"album", item.album},
            {"durationMs", item.durationMs}, {"artworkId", item.artworkId},
            {"externalIds", item.externalIds}, {"metadata", item.metadata},
            {"availableActions", actions}};
}

int sourceStatePriority(SourcePageLoadStateV2 state)
{
    switch (state) {
    case SourcePageLoadStateV2::Failed: return 3;
    case SourcePageLoadStateV2::Ready: return 2;
    case SourcePageLoadStateV2::Empty: return 1;
    case SourcePageLoadStateV2::Loading: return 0;
    }
    return 0;
}

// A source can fail one section and succeed another. Keep its failure and detail
// for this generation; Ready likewise must not be downgraded by an empty sibling.
void mergeSources(QMap<QString, SourcePageStateV2> &target,
                  const QHash<QString, SourcePageStateV2> &incoming)
{
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
        const auto existing = target.constFind(it.key());
        if (existing == target.cend()
            || sourceStatePriority(it->state) > sourceStatePriority(existing->state)
            || (it->state == existing->state && !existing->error && it->error)) {
            target.insert(it.key(), it.value());
        }
    }
}

} // namespace

MusicPageModel::MusicPageModel(MusicPageKindV2 page, QObject *parent)
    : QAbstractListModel(parent)
{
    // DTOs carry no page kind. MusicHub routes queries/results to the model for
    // this page; state/serialization are identical for all four page kinds.
    Q_UNUSED(page)
}

int MusicPageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MusicPageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto &row = m_rows.at(index.row());
    switch (role) {
    case SectionIdRole: return row.section.sectionId;
    case TitleRole: return row.section.titleKey;
    case LayoutHintRole: return row.section.layoutHint;
    case ItemsRole: {
        QVariantList items;
        items.reserve(row.section.items.size());
        for (const auto &item : row.section.items)
            items.append(itemToMap(item));
        return items;
    }
    case HasMoreRole: return row.section.hasMore;
    case LoadingMoreRole: return false; // Pagination orchestration is not part of this model task.
    case ErrorRole: return row.errors;
    default: return {};
    }
}

QHash<int, QByteArray> MusicPageModel::roleNames() const
{
    return {{SectionIdRole, "sectionId"}, {TitleRole, "title"},
            {LayoutHintRole, "layoutHint"}, {ItemsRole, "items"},
            {HasMoreRole, "hasMore"}, {LoadingMoreRole, "loadingMore"}, {ErrorRole, "error"}};
}

PageSectionV2 MusicPageModel::section(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows.at(index).section : PageSectionV2{};
}

QVariantMap MusicPageModel::itemAt(int section, int item) const
{
    if (section < 0 || section >= m_rows.size())
        return {};
    const auto &items = m_rows.at(section).section.items;
    return item >= 0 && item < items.size() ? itemToMap(items.at(item)) : QVariantMap{};
}

PageLoadStateV2 MusicPageModel::state() const
{
    return m_state;
}

bool MusicPageModel::cached() const
{
    if (m_rows.isEmpty())
        return m_emptyCached;
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &row) { return row.cached; });
}

QMap<QString, SourcePageStateV2> MusicPageModel::mergedSourceStates() const
{
    auto states = m_previewSourceStates;
    // Final network outcomes supersede provisional cache/stream outcomes.
    for (auto it = m_terminalSourceStates.cbegin(); it != m_terminalSourceStates.cend(); ++it)
        states.insert(it.key(), it.value());
    return states;
}

QVariantList MusicPageModel::sourceStates() const
{
    QVariantList result;
    const auto states = mergedSourceStates();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        result.append(QVariantMap{{"sourceInstanceId", it.key()},
                                  {"state", static_cast<int>(it->state)},
                                  {"error", it->error ? errorToMap(*it->error) : QVariantMap{}}});
    }
    return result;
}

SourceErrorV2 MusicPageModel::error() const
{
    if (m_requestError)
        return *m_requestError;
    const auto states = mergedSourceStates();
    for (const auto &state : states) {
        if (state.error)
            return *state.error;
    }
    return {};
}

QVariantMap MusicPageModel::errorMap() const
{
    if (m_requestError)
        return errorToMap(*m_requestError);
    const auto states = mergedSourceStates();
    for (const auto &state : states) {
        if (state.error)
            return errorToMap(*state.error);
    }
    return {};
}

quint64 MusicPageModel::beginRequest()
{
    const bool previousCached = cached();
    const auto previousSources = sourceStates();
    const auto previousError = errorMap();
    ++m_generation;
    m_completedSections = 0;
    m_successfulSections = 0;
    m_finished = false;
    m_emptyCached = false;
    m_previewSourceStates.clear();
    m_terminalSourceStates.clear();
    m_requestError.reset();
    for (auto &row : m_rows)
        row.cached = true;
    const bool stateChanges = m_state != PageLoadStateV2::Loading;
    m_state = PageLoadStateV2::Loading;
    notifyProperties(previousCached, previousSources, previousError);
    if (stateChanges)
        emit stateChanged();
    return m_generation;
}

bool MusicPageModel::accepts(quint64 generation) const
{
    return generation != 0 && generation == m_generation && !m_finished;
}

bool MusicPageModel::applyResult(quint64 generation, const PageResultV2 &result)
{
    if (!accepts(generation))
        return false;
    const bool previousCached = cached();
    const auto previousSources = sourceStates();
    const auto previousError = errorMap();
    const bool terminal = !result.cached && result.complete;
    QVariantMap sectionErrors;
    for (auto it = result.sourceStates.cbegin(); it != result.sourceStates.cend(); ++it) {
        if (it->error)
            sectionErrors.insert(it.key(), errorToMap(*it->error));
    }
    for (const auto &section : result.sections) {
        const auto existing = std::find_if(m_rows.begin(), m_rows.end(), [&](const Row &row) {
            return row.section.sectionId == section.sectionId;
        });
        Row row{section, sectionErrors, generation, result.cached, terminal};
        if (existing == m_rows.end()) {
            const int position = m_rows.size();
            beginInsertRows({}, position, position);
            m_rows.append(row);
            endInsertRows();
        } else if (terminal || existing->generation != generation || !existing->terminal) {
            const int position = std::distance(m_rows.begin(), existing);
            *existing = row;
            emit dataChanged(index(position), index(position));
        }
    }
    m_emptyCached = result.cached;
    mergeSources(terminal ? m_terminalSourceStates : m_previewSourceStates, result.sourceStates);
    if (terminal) {
        ++m_completedSections;
        const bool successful = result.sourceStates.isEmpty()
            || std::any_of(result.sourceStates.cbegin(), result.sourceStates.cend(), [](const auto &source) {
                return source.state == SourcePageLoadStateV2::Ready
                    || source.state == SourcePageLoadStateV2::Empty;
            });
        if (successful)
            ++m_successfulSections;
    }
    notifyProperties(previousCached, previousSources, previousError);
    return true;
}

bool MusicPageModel::applyFailure(quint64 generation, const SourceErrorV2 &error)
{
    if (!accepts(generation))
        return false;
    const auto previousError = errorMap();
    if (!m_requestError)
        m_requestError = error;
    ++m_completedSections;
    if (previousError != errorMap())
        emit errorChanged();
    return true;
}

bool MusicPageModel::finishGeneration(quint64 generation, int expectedSections)
{
    if (generation == 0 || generation != m_generation || expectedSections < 0
        || m_completedSections != expectedSections)
        return false;
    if (m_finished)
        return true;
    const bool previousCached = cached();
    const auto previousSources = sourceStates();
    const auto previousError = errorMap();
    m_finished = true;
    // Successful refreshes discard obsolete/provisional rows, including cached
    // rows followed by an empty final response. All-failed refreshes retain them.
    if (m_successfulSections > 0 || expectedSections == 0) {
        for (int i = m_rows.size() - 1; i >= 0; --i) {
            if (m_rows.at(i).generation != generation || !m_rows.at(i).terminal) {
                beginRemoveRows({}, i, i);
                m_rows.removeAt(i);
                endRemoveRows();
            }
        }
    }
    m_emptyCached = false;
    m_previewSourceStates.clear();
    const bool failed = m_requestError.has_value()
        || std::any_of(m_terminalSourceStates.cbegin(), m_terminalSourceStates.cend(), [](const auto &source) {
            return source.state == SourcePageLoadStateV2::Failed;
        });
    const bool hasItems = std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &row) {
        return !row.section.items.isEmpty();
    });
    if (failed && m_successfulSections == 0)
        m_state = PageLoadStateV2::Failed;
    else if (hasItems)
        m_state = PageLoadStateV2::Ready;
    else
        m_state = failed ? PageLoadStateV2::Failed : PageLoadStateV2::Empty;
    notifyProperties(previousCached, previousSources, previousError);
    emit stateChanged();
    return true;
}

void MusicPageModel::notifyProperties(bool previousCached, const QVariantList &previousSources,
                                     const QVariantMap &previousError)
{
    if (previousCached != cached())
        emit cachedChanged();
    if (previousSources != sourceStates())
        emit sourceStatesChanged();
    if (previousError != errorMap())
        emit errorChanged();
}
