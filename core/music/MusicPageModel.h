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

    // expectedSections is the total number of standard-section queries for this
    // generation, NOT the number of source instances, emissions or displayed rows.
    // Each query delivers exactly one non-cached complete result OR applyFailure;
    // an empty final result counts too. Cached/incomplete results do not count.
    // Call after terminal callbacks (or with zero queries). False means stale,
    // invalid count, or still pending. Only an exact count match sets a terminal
    // state. Once finished, further results/failures for this generation are rejected.
    // PageResultV2 has no query ID: the caller must deduplicate terminal callbacks.
    bool finishGeneration(quint64 generation, int expectedSections);

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
    };

    bool accepts(quint64 generation) const;
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
