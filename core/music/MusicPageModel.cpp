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

bool MusicPageModel::resetGeneration(quint64 generation)
{
    if (generation != m_generation) return false;
    const auto oldCached = cached();
    const auto oldSources = sourceStates();
    const auto oldError = errorMap();
    ++m_generation;
    m_finished = true;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    m_emptyCached = false;
    m_previewSourceStates.clear();
    m_terminalSourceStates.clear();
    m_requestError.reset();
    m_state = PageLoadStateV2::Idle;
    notifyProperties(oldCached, oldSources, oldError);
    emit stateChanged();
    return true;
}

bool MusicPageModel::cancelGeneration(quint64 generation)
{
    if (generation != m_generation) return false;
    const auto previousSources = sourceStates();
    ++m_generation;
    m_finished = true;
    auto settleSources = [this](auto &sources) {
        for (auto it = sources.begin(); it != sources.end(); ++it) {
            if (it->state != SourcePageLoadStateV2::Loading) continue;
            bool hasItems = false;
            for (const auto &row : m_rows) for (const auto &item : row.section.items)
                hasItems |= item.ref.sourceInstanceId == it.key();
            it->state = hasItems ? SourcePageLoadStateV2::Ready : SourcePageLoadStateV2::Empty;
        }
    };
    settleSources(m_previewSourceStates);
    settleSources(m_terminalSourceStates);
    for (auto &row : m_rows) { row.loading = false; settleSources(row.sources); }
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1), {LoadingMoreRole});
    const bool hasItems = std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return !r.section.items.isEmpty(); });
    m_state = hasItems ? PageLoadStateV2::Ready : PageLoadStateV2::Idle;
    if (previousSources != sourceStates()) emit sourceStatesChanged();
    emit stateChanged();
    return true;
}

bool MusicPageModel::beginSectionRequest(quint64 generation, const QString &id)
{
    if (!generation || generation != m_generation || !m_finished) return false;
    for (int i = 0; i < m_rows.size(); ++i) {
        auto &row = m_rows[i];
        if (row.section.sectionId != id) continue;
        if (row.loading) return false;
        row.loading = true;
        emit dataChanged(index(i), index(i), {LoadingMoreRole});
        return true;
    }
    return false;
}

int MusicPageModel::pendingSection(quint64 generation, const QString &id) const
{
    if (!generation || generation != m_generation || !m_finished) return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].section.sectionId == id && m_rows[i].loading) return i;
    return -1;
}

void MusicPageModel::settleSectionState()
{
    m_terminalSourceStates.clear();
    bool hasItems = false, failed = m_requestError.has_value();
    for (const auto &row : m_rows) {
        mergeSources(m_terminalSourceStates, row.sources);
        hasItems |= !row.section.items.isEmpty();
        failed |= !row.errors.isEmpty();
        for (const auto &source : row.sources)
            failed |= source.state == SourcePageLoadStateV2::Failed;
    }
    m_state = hasItems ? PageLoadStateV2::Ready : failed ? PageLoadStateV2::Failed : PageLoadStateV2::Empty;
    emit stateChanged();
}

bool MusicPageModel::applySectionResult(quint64 generation, const QString &id,
                                        const PageResultV2 &result, bool append)
{
    const int i = pendingSection(generation, id);
    if (i < 0 || result.cached || !result.complete) return false;
    const auto oldCached = cached();
    const auto oldSources = sourceStates();
    const auto oldError = errorMap();
    auto &row = m_rows[i];
    row.loading = false;
    row.errors.clear();
    row.sources = result.sourceStates;
    bool allFailed = !result.sourceStates.isEmpty();
    for (auto it = result.sourceStates.cbegin(); it != result.sourceStates.cend(); ++it) {
        allFailed &= it->state == SourcePageLoadStateV2::Failed;
        if (it->error) row.errors.insert(it.key(), errorToMap(*it->error));
    }
    if (!allFailed) {
        auto replacement = row.section;
        replacement.items.clear();
        replacement.hasMore = false;
        replacement.nextCursor.clear();
        for (const auto &section : result.sections)
            if (section.sectionId == id) { replacement = section; break; }
        if (append) replacement.items = row.section.items + replacement.items;
        row.section = replacement;
        row.cached = false;
    }
    emit dataChanged(index(i), index(i));
    settleSectionState();
    notifyProperties(oldCached, oldSources, oldError);
    return true;
}

bool MusicPageModel::applySectionFailure(quint64 generation, const QString &id,
                                         const SourceErrorV2 &error)
{
    const int i = pendingSection(generation, id);
    if (i < 0) return false;
    auto &row = m_rows[i];
    row.loading = false;
    row.errors.insert(QString(), errorToMap(error));
    emit dataChanged(index(i), index(i), {LoadingMoreRole, ErrorRole});
    settleSectionState();
    return true;
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
    case LoadingMoreRole: return row.loading;
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
    for (auto &row : m_rows) {
        row.cached = true;
        row.loading = false;
    }
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1), {LoadingMoreRole});
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
    const bool allSourcesFailed = terminal && !result.sourceStates.isEmpty()
        && std::all_of(result.sourceStates.cbegin(), result.sourceStates.cend(), [](const auto &source) {
            return source.state == SourcePageLoadStateV2::Failed;
        });
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
        row.sources = result.sourceStates;
        if (existing == m_rows.end()) {
            const int position = m_rows.size();
            beginInsertRows({}, position, position);
            m_rows.append(row);
            endInsertRows();
        } else if (terminal || existing->generation != generation || !existing->terminal) {
            const int position = std::distance(m_rows.begin(), existing);
            if (allSourcesFailed && section.items.isEmpty() && existing->cached) {
                // The query completed with errors, not a successful empty section.
                // Retain its cached content while recording this generation's outcome.
                row.section = existing->section;
                row.cached = true;
            }
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
