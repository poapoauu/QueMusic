#pragma once
#include "v2/SourceV2Types.h"
#include <QSet>

// Continuations are host-owned, bounded, in-memory snapshots. Provider cursors
// and overfetched DTOs never travel inside a public/UI cursor.
struct AggregateCursorState {
    QHash<QString, QString> sourceCursors;
    QSet<QString> exhaustedSources;
    int nextSource = 0;
    QString scope;
    QList<SourcePageResultV2> buffered;
    QSet<QString> seenIds;
};

class AggregateComposer final {
public:
    PageResultV2 compose(const QList<SourcePageResultV2> &inputs, int requestedLimit,
                         const QString &cursor = {}, const QString &scope = {}) const;
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
