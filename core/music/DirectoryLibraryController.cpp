#include "DirectoryLibraryController.h"
#include "PageRepository.h"

namespace {
SourceErrorV2 unavailable()
{
    return {SourceErrorKindV2::Unavailable, QStringLiteral("music.directoryUnavailable"), {}, {}, false};
}
QString rootSectionId(const QString &instanceId)
{
    return QStringLiteral("directory/") + instanceId;
}
}

DirectoryLibraryController::DirectoryLibraryController(SourceRegistry *sources,
                                                       PageRepository *repository,
                                                       QObject *parent)
    : QObject(parent), m_sources(sources), m_repository(repository),
      m_model(new MusicPageModel(MusicPageKindV2::Category, this))
{
    connect(m_model, &MusicPageModel::stateChanged, this, &DirectoryLibraryController::changed);
    connect(m_model, &QAbstractItemModel::modelReset, this, &DirectoryLibraryController::changed);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &DirectoryLibraryController::changed);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &DirectoryLibraryController::changed);
    if (m_repository) {
        connect(m_repository, &PageRepository::pageReady, this,
                [this](QUuid id, quint64 gen, PageResultV2 result) { receive(id, gen, result); });
        connect(m_repository, &PageRepository::pageFailed, this,
                [this](QUuid id, quint64 gen, SourceErrorV2 error) {
                    receive(id, gen, {}, error);
                });
    }
    if (m_sources) {
        connect(m_sources, &SourceRegistry::instanceChanged, this,
                [this] { if (m_activated) refresh(); });
        connect(m_sources, &SourceRegistry::instanceContentChanged, this,
                [this](const QString &source, quint64) {
                    if (!m_activated) return;
                    if (m_stack.isEmpty()) { refresh(); return; }
                    if (m_stack.last().value("ref").toMap().value("sourceInstanceId") == source)
                        refresh();
                });
        if (auto *plugins = m_sources->pluginManager()) {
            for (const auto &value : plugins->plugins()) {
                const auto packageId = value.toMap().value("id").toString();
                m_pluginStates.insert(packageId, plugins->plugin(packageId).state);
            }
            connect(plugins, &PluginManager::pluginChanged, this, [this](const QString &packageId) {
                if (!m_sources || !m_sources->pluginManager()) return;
                const auto state = m_sources->pluginManager()->plugin(packageId).state;
                // Callable/session leases and Busy diagnostics are not content
                // changes. Restarting here cancels the request acquiring that lease.
                if (m_pluginStates.contains(packageId) && m_pluginStates.value(packageId) == state) return;
                m_pluginStates.insert(packageId, state);
                if (m_activated) refresh();
            });
        }
        connect(m_sources, &QObject::destroyed, this, [this] {
            m_sources = nullptr;
            cancelPending();
        });
    }
}

DirectoryLibraryController::~DirectoryLibraryController() { cancelPending(); }
MusicPageModel *DirectoryLibraryController::model() const { return m_model; }
bool DirectoryLibraryController::canNavigateBack() const { return !m_stack.isEmpty(); }

void DirectoryLibraryController::cancelPending()
{
    const auto ids = m_pending.keys();
    m_pending.clear();
    for (const auto &id : ids) if (m_repository) m_repository->cancel(id);
}

void DirectoryLibraryController::activate()
{
    if (m_activated) return;
    m_activated = true;
    refresh();
}

void DirectoryLibraryController::refresh()
{
    if (!m_activated) return;
    cancelPending();
    m_origins.clear();
    if (m_generation) m_model->resetGeneration(m_generation);
    m_generation = m_model->beginRequest();
    const quint64 generation = m_generation;
    if (!m_repository || !m_sources) {
        m_model->applyFailure(generation, unavailable());
        m_model->finishGeneration(generation, 1);
        return;
    }
    if (!m_stack.isEmpty()) {
        const auto ref = m_stack.last().value("ref").toMap();
        PageQueryV2 query;
        query.page = MusicPageKindV2::Category;
        query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = ref.value("sourceInstanceId").toString();
        query.filters = {{QStringLiteral("directoryId"), ref.value("entityId").toString()}};
        m_expected = 1;
        const auto id = m_repository->requestPage(query, generation);
        m_pending.insert(id, {generation, query, QStringLiteral("directory/content"), false, false});
        return;
    }
    m_expected = 0;
    for (const auto &instance : m_sources->enabledInstances()) {
        if (!instance.enabled) continue;
        PageQueryV2 query;
        query.page = MusicPageKindV2::Category;
        query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = instance.sourceInstanceId;
        query.filters = {{QStringLiteral("entityType"), int(MediaEntityTypeV2::Directory)}};
        ++m_expected;
        const auto id = m_repository->requestPage(query, generation);
        m_pending.insert(id, {generation, query, rootSectionId(instance.sourceInstanceId), false, false});
    }
    if (!m_expected) m_model->finishGeneration(generation, 0);
}

void DirectoryLibraryController::receive(const QUuid &id, quint64 generation,
                                         const PageResultV2 &result,
                                         std::optional<SourceErrorV2> failure)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend() || it->generation != generation || generation != m_generation) return;
    const auto request = it.value();
    const bool terminal = failure.has_value() || (!result.cached && result.complete);
    if (terminal) m_pending.remove(id);
    if (request.continuation) {
        if (!terminal) return;
        if (failure) m_model->applySectionFailure(generation, request.sectionId, *failure);
        else {
            PageResultV2 projected = result;
            projected.sections.clear();
            for (const auto &section : result.sections) {
                PageSectionV2 row = section;
                row.sectionId = request.sectionId;
                row.items.clear();
                for (const auto &item : section.items)
                    if (!m_stack.isEmpty() || item.ref.entityType == MediaEntityTypeV2::Directory)
                        row.items.append(item);
                projected.sections.append(row);
            }
            m_model->applySectionResult(generation, request.sectionId, projected, request.append);
        }
        return;
    }
    if (failure) {
        PageSectionV2 section;
        section.sectionId = request.sectionId;
        section.kind = PageSectionKindV2::Tracks;
        if (failure->kind == SourceErrorKindV2::Unsupported && m_stack.isEmpty()) {
            PageResultV2 empty;
            empty.sections.append(section);
            m_model->applyResult(generation, empty);
        } else {
            section.titleKey = QStringLiteral("music.directoryFailed");
            m_origins.insert(section.sectionId, request.query);
            m_model->applyQueryFailure(generation, section, *failure);
        }
    } else {
        PageResultV2 projected = result;
        projected.sections.clear();
        for (const auto &section : result.sections) {
            PageSectionV2 row = section;
            row.sectionId = request.sectionId;
            row.items.clear();
            for (const auto &item : section.items)
                if (!m_stack.isEmpty() || item.ref.entityType == MediaEntityTypeV2::Directory)
                    row.items.append(item);
            projected.sections.append(row);
        }
        if (projected.sections.isEmpty()) {
            PageSectionV2 empty;
            empty.sectionId = request.sectionId;
            empty.kind = PageSectionKindV2::Tracks;
            projected.sections.append(empty);
        }
        m_origins.insert(request.sectionId, request.query);
        m_model->applyResult(generation, projected);
    }
    if (terminal) m_model->finishGeneration(generation, m_expected);
}

bool DirectoryLibraryController::browse(const QVariantMap &fullItem)
{
    const auto ref = fullItem.value("ref").toMap();
    if (ref.value("entityType").toInt() != int(MediaEntityTypeV2::Directory)
        || m_stack.size() >= 32) return false;
    bool found = false;
    for (int section = 0; section < m_model->rowCount(); ++section)
        for (int item = 0; item < m_model->section(section).items.size(); ++item)
            found |= m_model->itemAt(section, item) == fullItem;
    if (!found) return false;
    m_stack.append(fullItem);
    emit changed();
    refresh();
    return true;
}

bool DirectoryLibraryController::navigateBack()
{
    if (m_stack.isEmpty()) return false;
    m_stack.removeLast();
    emit changed();
    refresh();
    return true;
}

void DirectoryLibraryController::loadMore(const QString &sectionId)
{
    requestSection(sectionId, true);
}

void DirectoryLibraryController::retry(const QString &sectionId)
{
    requestSection(sectionId, false);
}

void DirectoryLibraryController::requestSection(const QString &sectionId, bool append)
{
    if (!m_repository || !m_origins.contains(sectionId)) return;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const auto section = m_model->section(row);
        if (section.sectionId != sectionId) continue;
        const auto errors = m_model->data(m_model->index(row), MusicPageModel::ErrorRole).toMap();
        if (append && (!section.hasMore || section.nextCursor.isEmpty() || !errors.isEmpty())) return;
        bool retryable = false;
        for (const auto &error : errors)
            retryable |= error.toMap().value("kind").toInt() != int(SourceErrorKindV2::Unsupported);
        if (!append && !retryable) return;
        auto query = m_origins.value(sectionId);
        // Retry replaces only this section from its original query, like MusicHub.
        query.cursor = append ? section.nextCursor : QString{};
        const auto generation = m_generation;
        if (!m_model->beginSectionRequest(generation, sectionId) || generation != m_generation) return;
        const auto id = m_repository->requestPage(query, generation);
        m_pending.insert(id, {generation, query, sectionId, append, true});
        return;
    }
}

QVariantMap DirectoryLibraryController::settingsTarget(const QVariantMap &fullItem) const
{
    if (!m_sources || !m_stack.isEmpty()
        || fullItem.value("ref").toMap().value("entityType").toInt()
            != int(MediaEntityTypeV2::Directory)) return {};
    bool displayed = false;
    for (int row = 0; row < m_model->rowCount(); ++row)
        for (int item = 0; item < m_model->section(row).items.size(); ++item)
            displayed |= m_model->itemAt(row, item) == fullItem;
    if (!displayed) return {};
    const auto ref = fullItem.value("ref").toMap();
    for (const auto &instance : m_sources->enabledInstances()) {
        if (!instance.enabled || instance.sourceInstanceId != ref.value("sourceInstanceId")
            || instance.sourceId != ref.value("sourcePluginId")
            || instance.accountId != ref.value("accountId")) continue;
        return {{QStringLiteral("packageId"), instance.pluginPackageId},
                {QStringLiteral("instanceId"), instance.sourceInstanceId}};
    }
    return {};
}
