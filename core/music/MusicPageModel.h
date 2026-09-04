#pragma once

#include "v2/SourceV2Types.h"

#include <QAbstractListModel>

class MusicPageModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(PageLoadStateV2 state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool cached READ cached NOTIFY cachedChanged)
    Q_PROPERTY(QVariantList sourceStates READ sourceStates NOTIFY sourceStatesChanged)
    Q_PROPERTY(QVariantMap error READ errorMap NOTIFY errorChanged)

public:
    enum Role {
        SectionIdRole = Qt::UserRole + 1,
        TitleRole,
        LayoutHintRole,
        ItemsRole,
        HasMoreRole,
        LoadingMoreRole,
        ErrorRole,
    };

    explicit MusicPageModel(MusicPageKindV2 page, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    PageSectionV2 section(int index) const;
    Q_INVOKABLE QVariantMap itemAt(int section, int item) const;

    PageLoadStateV2 state() const;
    bool cached() const;
    QVariantList sourceStates() const;
    SourceErrorV2 error() const;
    QVariantMap errorMap() const;

    quint64 beginRequest();
    bool applyResult(quint64 generation, const PageResultV2 &result);
    bool applyFailure(quint64 generation, const SourceErrorV2 &error);
    // Hub full-query failure: counts exactly one terminal and retains a terminal
    // error row (or existing cached items) with a stable section ID for retry.
    // Its row-owned error is cleared by successful scoped replacement. Legacy
    // applyFailure remains a page-wide failure without a section identity.
    bool applyQueryFailure(quint64 generation, const PageSectionV2 &section,
                           const SourceErrorV2 &error);

    // expectedSections is the total number of standard-section queries for this
    // generation, NOT the number of source instances, emissions or displayed rows.
    // Each query delivers exactly one non-cached complete result OR one of
    // applyFailure/applyQueryFailure;
    // an empty final result counts too. Cached/incomplete results do not count.
    // Call after terminal callbacks (or with zero queries). False means stale,
    // invalid count, or still pending. Only an exact count match sets a terminal
    // state. Once finished, further results/failures for this generation are rejected.
    // PageResultV2 has no query ID: the caller must deduplicate terminal callbacks.
    bool finishGeneration(quint64 generation, int expectedSections);

    // Owner-thread orchestration APIs. Reset/cancel invalidate the supplied
    // generation (including a finished one). Reset drops context; cancel keeps
    // accepted rows and settles Ready/Idle. Success advances the token by one;
    // beginRequest starts a new full refresh and returns its token.
    bool resetGeneration(quint64 generation);
    bool cancelGeneration(quint64 generation);
    // Independent section work is allowed after full refresh settles (completion
    // or cancellation), using the current token.
    // One pending operation per ID. Terminal scoped results append or replace
    // only that ID; failure retains items/cursor. These never count full queries.
    bool beginSectionRequest(quint64 generation, const QString &sectionId);
    bool applySectionResult(quint64 generation, const QString &sectionId,
                            const PageResultV2 &result, bool append);
    bool applySectionFailure(quint64 generation, const QString &sectionId,
                             const SourceErrorV2 &error);

signals:
    void stateChanged();
    void cachedChanged();
    void sourceStatesChanged();
    void errorChanged();

private:
    struct Row {
        PageSectionV2 section;
        QVariantMap errors; // sourceInstanceId -> SourceErrorV2 fields
        quint64 generation = 0;
        bool cached = false;
        bool terminal = false;
        bool loading = false;
        QHash<QString, SourcePageStateV2> sources;
        std::optional<SourceErrorV2> queryError;
    };

    bool accepts(quint64 generation) const;
    int pendingSection(quint64 generation, const QString &sectionId) const;
    void settleSectionState();
    QMap<QString, SourcePageStateV2> mergedSourceStates() const;
    void notifyProperties(bool previousCached, const QVariantList &previousSources,
                          const QVariantMap &previousError);

    QList<Row> m_rows;
    QMap<QString, SourcePageStateV2> m_previewSourceStates;
    QMap<QString, SourcePageStateV2> m_terminalSourceStates;
    std::optional<SourceErrorV2> m_requestError;
    quint64 m_generation = 0;
    int m_completedSections = 0;
    int m_successfulSections = 0;
    PageLoadStateV2 m_state = PageLoadStateV2::Idle;
    bool m_finished = false;
    bool m_emptyCached = false;
};
