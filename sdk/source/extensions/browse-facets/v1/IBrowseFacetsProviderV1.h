#pragma once
#include "v2/SourceV2Types.h"
#include <QSet>
#include <QVector>
#include <QtPlugin>
#include <optional>

// Independent optional catalog extension, not settings, permissions or media.
struct BrowseFacetChoiceV1 { QString value, title; };
struct BrowseFacetV1 { QString id, title; QVector<BrowseFacetChoiceV1> choices; };
struct BrowseFacetSchemaV1 {
    PageSectionKindV2 section = PageSectionKindV2::Artists;
    QString revision;
    QVector<BrowseFacetV1> facets;
};
struct BrowseFacetSelectionV1 { QString revision; QVariantMap values; };
class IBrowseFacetsProviderV1 {
public:
    virtual ~IBrowseFacetsProviderV1() = default;
    // Ready session, owner thread, callable lease. Synchronous, no I/O, signals
    // or mutation. nullopt withdraws filtering, NEVER requests fallback. Only
    // Category/Artists in v1. Semantic changes/withdrawn choices must change
    // revision; title translations alone do not. Recheck on EVERY fetch.
    virtual std::optional<BrowseFacetSchemaV1> browseFacets(PageSectionKindV2 section) const = 0;
};
#define QUEMUSIC_BROWSE_FACETS_PROVIDER_V1_IID "org.quemusic.source.extensions.BrowseFacetsProvider/1.0"
Q_DECLARE_INTERFACE(IBrowseFacetsProviderV1, QUEMUSIC_BROWSE_FACETS_PROVIDER_V1_IID)
inline QString browseFacetsFilterKeyV1() { return QStringLiteral("quemusic.browse-facets.v1"); }

// 1..64 ASCII characters, initial alphanumeric, then alphanumeric/._-.
// Values (including numeric strings) are opaque. No coercion/normalization.
inline bool isBrowseFacetIdentifierV1(const QString &text)
{
    if (text.isEmpty() || text.size() > 64) return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text[i].unicode();
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '-'))) return false;
    }
    return true;
}
inline bool isBrowseFacetIdV1(const QString &id)
{
    return isBrowseFacetIdentifierV1(id) && !id.startsWith(QStringLiteral("quemusic."))
        && id != QStringLiteral("facetRevision") && id != QStringLiteral("facetValues")
        && id != QStringLiteral("artistId") && id != QStringLiteral("playlistId") && id != QStringLiteral("genreId");
}
// Host renders PlainText. Controls and malformed UTF-16 are invalid; valid
// Unicode titles (including surrogate pairs) are preserved.
inline bool isBrowseFacetTitleV1(const QString &title)
{
    if (title.size() > 256 || title.trimmed().isEmpty()) return false;
    for (qsizetype i = 0; i < title.size(); ++i) {
        const auto ch = title[i];
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
        if (ch.isHighSurrogate()) {
            if (++i >= title.size() || !title[i].isLowSurrogate()) return false;
        } else if (ch.isLowSurrogate()) return false;
    }
    return true;
}
inline bool isValidBrowseFacetSchemaV1(const BrowseFacetSchemaV1 &schema)
{
    if (schema.section != PageSectionKindV2::Artists || !isBrowseFacetIdentifierV1(schema.revision)
        || schema.facets.isEmpty() || schema.facets.size() > 8) return false;
    QSet<QString> ids;
    for (const auto &facet : schema.facets) {
        if (!isBrowseFacetIdV1(facet.id) || ids.contains(facet.id) || !isBrowseFacetTitleV1(facet.title)
            || facet.choices.isEmpty() || facet.choices.size() > 64) return false;
        ids.insert(facet.id);
        QSet<QString> values;
        for (const auto &choice : facet.choices) {
            if (!isBrowseFacetIdentifierV1(choice.value) || values.contains(choice.value)
                || !isBrowseFacetTitleV1(choice.title)) return false;
            values.insert(choice.value);
        }
    }
    return true;
}
namespace BrowseFacetsDetailV1 {
inline std::optional<BrowseFacetSelectionV1> parse(const PageQueryV2 &query, bool hostSelector)
{
    if (query.page != MusicPageKindV2::Category || query.section != PageSectionKindV2::Artists
        || query.scope.isAggregate() || query.scope.sourceInstanceId.trimmed().isEmpty()
        || !query.searchText.isEmpty() || query.limit < 1 || query.limit > 100
        || query.filters.size() != (hostSelector ? 3 : 2)) return {};
    if (hostSelector) {
        const auto selector = query.filters.value(browseFacetsFilterKeyV1());
        if (selector.metaType().id() != QMetaType::QString || selector.toString() != QStringLiteral("selected")) return {};
    }
    const auto revision = query.filters.value(QStringLiteral("facetRevision"));
    const auto values = query.filters.value(QStringLiteral("facetValues"));
    if (revision.metaType().id() != QMetaType::QString || !isBrowseFacetIdentifierV1(revision.toString())
        || values.metaType().id() != QMetaType::QVariantMap) return {};
    const auto selected = values.toMap();
    if (selected.isEmpty() || selected.size() > 8) return {};
    for (auto it = selected.cbegin(); it != selected.cend(); ++it)
        if (!isBrowseFacetIdV1(it.key()) || it.value().metaType().id() != QMetaType::QString
            || !isBrowseFacetIdentifierV1(it.value().toString())) return {};
    return BrowseFacetSelectionV1{revision.toString(), selected};
}
}
// Parsing is NOT authorization/discovery. Never forward to an old provider.
// Remove ONLY selector after live schema/identity/lease checks in Host.
inline std::optional<BrowseFacetSelectionV1> browseFacetSelectionV1(const PageQueryV2 &query)
{ return BrowseFacetsDetailV1::parse(query, true); }
// Provider receives exactly revision/values without Host selector; plugin must
// independently match its CURRENT schema before performing any I/O.
inline std::optional<BrowseFacetSelectionV1> browseFacetProviderSelectionV1(const PageQueryV2 &query)
{ return BrowseFacetsDetailV1::parse(query, false); }
inline bool matchesBrowseFacetSelectionV1(const BrowseFacetSchemaV1 &schema, const BrowseFacetSelectionV1 &selection)
{
    if (!isValidBrowseFacetSchemaV1(schema) || selection.revision != schema.revision
        || selection.values.isEmpty() || selection.values.size() > schema.facets.size()) return false;
    for (auto it = selection.values.cbegin(); it != selection.values.cend(); ++it) {
        if (!isBrowseFacetIdV1(it.key()) || it.value().metaType().id() != QMetaType::QString) return false;
        bool found = false;
        for (const auto &facet : schema.facets) {
            if (facet.id != it.key()) continue;
            for (const auto &choice : facet.choices) if (choice.value == it.value().toString()) { found = true; break; }
            break;
        }
        if (!found) return false;
    }
    return true;
}
