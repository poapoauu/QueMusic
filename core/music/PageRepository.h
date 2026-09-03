#pragma once
#include "AggregateComposer.h"
#include "PageCache.h"
#include "SourceRegistry.h"
#include <memory>

class PageRepository final : public QObject {
    Q_OBJECT
public:
    // Sources/composer are borrowed and must outlive this repository. Cache is
    // shared by value into IO work; its lifetime does not depend on this QObject.
    PageRepository(SourceRegistry *sources, AggregateComposer *composer,
                   QObject *parent = nullptr, std::shared_ptr<PageCache> cache = {});
    ~PageRepository() override;
    QUuid requestPage(const PageQueryV2 &query, quint64 generation);
    void cancel(const QUuid &groupId);
signals:
    void pageReady(QUuid requestId, quint64 generation, PageResultV2 result);
    void pageFailed(QUuid requestId, quint64 generation, SourceErrorV2 error);
private:
    struct Request;
    struct Group;
    void begin(const QUuid &id);
    void fanOut(const QUuid &id);
    void dispatch(const QUuid &id, const QString &source);
    void receive(const QUuid &id, const QString &source, PageResultV2 page,
                 std::optional<SourceErrorV2> error = {});
    void finish(const QUuid &id);
    void fail(const QUuid &id, SourceErrorV2 error);
    void sourceChanged(const QString &source);
    QPointer<SourceRegistry> m_sources;
    AggregateComposer *m_composer;
    std::shared_ptr<PageCache> m_cache;
    QHash<QUuid, std::shared_ptr<Group>> m_groups;
};
