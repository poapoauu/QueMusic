#pragma once
#include "v2/SourceV2Types.h"
#include <QSet>

enum class AggregateCompositionMode { Discovery, SourceOrdered };

// Continuations are host-owned, bounded, in-memory snapshots. Provider cursors
// and overfetched DTOs never travel inside a public/UI cursor.
struct AggregateCursorState {
    QHash<QString, QString> sourceCursors;
    QSet<QString> exhaustedSources;
    int nextSource = 0;
    QString scope;
    QList<SourcePageResultV2> buffered;
    QSet<QString> seenIds;
    AggregateCompositionMode mode = AggregateCompositionMode::Discovery;
};

class AggregateComposer final {
public:
    // Repository supplies a query hash for each returned section kind. This
    // binds sibling tokens independently; scope validates an input continuation.
    PageResultV2 compose(const QList<SourcePageResultV2> &inputs, int requestedLimit,
                         const QString &cursor = {}, const QString &scope = {},
                         const QHash<PageSectionKindV2,QString> &sectionScopes = {},
                         AggregateCompositionMode mode = AggregateCompositionMode::Discovery) const;
    QList<MediaItemV2> composeItems(const QList<QList<MediaItemV2>> &inputs,
                                  int requestedLimit) const;
    QString encodeCursor(const AggregateCursorState &state) const;
    std::optional<AggregateCursorState> decodeCursor(const QString &cursor) const;
    std::optional<AggregateCursorState> decodeCursor(const QString &cursor,
                                                    const QString &scope) const;
    void invalidateSource(const QString &sourceInstanceId);
private:
    mutable QHash<QString, AggregateCursorState> m_cursors;
    mutable QStringList m_cursorOrder;
};
