#pragma once

#include "MusicPageModel.h"
#include "SourceRegistry.h"
#include <QHash>
#include <QPointer>

class PageRepository;

// Separate directory projection. It never changes MusicHub's shared Category
// context and borrows Registry/Repository for its own request lifecycle.
class DirectoryLibraryController final : public QObject {
    Q_OBJECT
public:
    DirectoryLibraryController(SourceRegistry *sources, PageRepository *repository,
                               QObject *parent = nullptr);
    ~DirectoryLibraryController() override;
    MusicPageModel *model() const;
    void activate();
    bool browse(const QVariantMap &fullItem);
    bool navigateBack();
    void refresh();
    void loadMore(const QString &sectionId);
    void retry(const QString &sectionId);
    bool canNavigateBack() const;
    QVariantMap settingsTarget(const QVariantMap &fullItem) const;
signals:
    void changed();
private:
    struct Pending {
        quint64 generation = 0;
        PageQueryV2 query;
        QString sectionId;
        bool append = false;
        bool continuation = false;
    };
    void receive(const QUuid &requestId, quint64 generation, const PageResultV2 &result,
                 std::optional<SourceErrorV2> failure = {});
    void requestSection(const QString &sectionId, bool append);
    void cancelPending();
    QPointer<SourceRegistry> m_sources;
    QPointer<PageRepository> m_repository;
    MusicPageModel *m_model;
    QHash<QUuid, Pending> m_pending;
    QHash<QString, PageQueryV2> m_origins;
    QList<QVariantMap> m_stack;
    quint64 m_generation = 0;
    int m_expected = 0;
    bool m_activated = false;
};
