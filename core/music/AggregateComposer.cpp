#include "AggregateComposer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace {
QString reliableId(const MediaItemV2 &item)
{
    for (const auto &name : {QStringLiteral("isrc"), QStringLiteral("musicBrainzRecordingId")}) {
        auto value = item.externalIds.value(name);
        if (value.metaType().id() == QMetaType::QString && !value.toString().trimmed().isEmpty())
            return QString::number(int(item.ref.entityType)) + '/' + name + '/' + value.toString().trimmed().toUpper();
    }
    return {};
}
QList<MediaItemV2> drain(QList<QList<MediaItemV2>> &rows, int limit, int &next,
                         QSet<QString> &seen)
{
    QList<MediaItemV2> result;
    if (rows.isEmpty()) return result;
    int empty = 0;
    while (result.size() < limit && empty < rows.size()) {
        auto &source = rows[next];
        next = (next + 1) % rows.size();
        if (source.isEmpty()) { ++empty; continue; }
        empty = 0;
        auto item = source.takeFirst();
        const auto key = reliableId(item);
        if (!key.isEmpty() && seen.contains(key)) continue;
        if (!key.isEmpty()) seen.insert(key);
        result.append(item);
    }
    return result;
}
}

QList<MediaItemV2> AggregateComposer::composeItems(const QList<QList<MediaItemV2>> &inputs,
                                                 int limit) const
{
    auto rows = inputs;
    int next = 0;
    QSet<QString> seen;
    return drain(rows, limit, next, seen);
}

PageResultV2 AggregateComposer::compose(const QList<SourcePageResultV2> &inputs,
                                        int limit, const QString &cursor,
                                        const QString &scope) const
{
    PageResultV2 out;
    AggregateCursorState prior;
    if (!cursor.isEmpty()) {
        auto value = decodeCursor(cursor, scope);
        if (!value) { out.complete = false; return out; }
        prior = *value;
    }
    // A continuation contains one rendered section, including every source's
    // residual rows. New inputs replace only sources that have been refetched.
    auto merged = prior.buffered;
    for (const auto &input : inputs) {
        auto found = std::find_if(merged.begin(), merged.end(), [&](const auto &p) {
            return p.sourceInstanceId == input.sourceInstanceId;
        });
        if (found == merged.end()) merged.append(input); else *found = input;
    }
    std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
        return a.sourceInstanceId < b.sourceInstanceId;
    });
    QMap<int, PageSectionV2> sections;
    for (const auto &input : merged) {
        bool empty = true;
        for (const auto &section : input.page.sections) {
            empty &= section.items.isEmpty();
            if (!sections.contains(int(section.kind))) sections.insert(int(section.kind), section);
        }
        out.sourceStates.insert(input.sourceInstanceId,
            {input.error ? SourcePageLoadStateV2::Failed : empty ? SourcePageLoadStateV2::Empty
                                                               : SourcePageLoadStateV2::Ready, input.error});
    }
    for (auto section : sections) {
        AggregateCursorState state;
        state.scope = scope; state.nextSource = prior.nextSource; state.seenIds = prior.seenIds;
        QList<QList<MediaItemV2>> rows;
        for (const auto &input : merged) {
            SourcePageResultV2 buffered{input.sourceInstanceId, {}, input.error};
            PageSectionV2 sourceSection = section;
            sourceSection.items.clear(); sourceSection.hasMore = false; sourceSection.nextCursor.clear();
            for (const auto &s : input.page.sections)
                if (s.kind == section.kind && !input.error) sourceSection = s;
            buffered.page.sections = {sourceSection};
            state.buffered.append(buffered);
            rows.append(sourceSection.items);
            if (sourceSection.hasMore && !sourceSection.nextCursor.isEmpty())
                state.sourceCursors.insert(input.sourceInstanceId, sourceSection.nextCursor);
            else state.exhaustedSources.insert(input.sourceInstanceId);
        }
        section.items = drain(rows, limit, state.nextSource, state.seenIds);
        bool more = !state.sourceCursors.isEmpty();
        for (int i = 0; i < rows.size(); ++i) {
            state.buffered[i].page.sections[0].items = rows[i];
            more |= !rows[i].isEmpty();
        }
        section.hasMore = more;
        section.nextCursor = more ? encodeCursor(state) : QString{};
        out.sections.append(section);
    }
    return out;
}

QString AggregateComposer::encodeCursor(const AggregateCursorState &state) const
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cursors.insert(token, state);
    m_cursorOrder.append(token);
    while (m_cursorOrder.size() > 256) m_cursors.remove(m_cursorOrder.takeFirst());
    return QString::fromLatin1(QJsonDocument(QJsonObject{{"v", 1}, {"scope", state.scope}, {"token", token}})
        .toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::optional<AggregateCursorState> AggregateComposer::decodeCursor(const QString &cursor) const
{
    if (cursor.isEmpty() || cursor.size() > 1024
        || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(cursor).hasMatch()) return {};
    const auto bytes = QByteArray::fromBase64(cursor.toLatin1(), QByteArray::Base64UrlEncoding
                                             | QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != cursor.toLatin1()) return {};
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) return {};
    const auto obj = doc.object();
    if (obj.size() != 3 || obj.value("v") != QJsonValue(1) || !obj.value("scope").isString()
        || !obj.value("token").isString()) return {};
    const auto found = m_cursors.constFind(obj.value("token").toString());
    if (found == m_cursors.cend() || found->scope != obj.value("scope").toString()) return {};
    return *found;
}
std::optional<AggregateCursorState> AggregateComposer::decodeCursor(const QString &cursor,
                                                                   const QString &scope) const
{
    auto state = decodeCursor(cursor);
    return state && state->scope == scope ? state : std::nullopt;
}
void AggregateComposer::invalidateSource(const QString &source)
{
    for (auto it = m_cursors.begin(); it != m_cursors.end();) {
        const bool matches = std::any_of(it->buffered.begin(), it->buffered.end(), [&](const auto &p) {
            return p.sourceInstanceId == source;
        });
        if (matches) { m_cursorOrder.removeAll(it.key()); it = m_cursors.erase(it); }
        else ++it;
    }
}
