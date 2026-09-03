#include "AggregateComposer.h"
#include <QJsonDocument>
#include <QTest>

static MediaItemV2 track(QString source, QString id, QVariantMap ids = {})
{
    MediaItemV2 item;
    item.ref = {"fixture", source, source, MediaEntityTypeV2::Track, id};
    item.title = "Same title";
    item.externalIds = ids;
    return item;
}
static SourcePageResultV2 page(QString source, QStringList ids, bool more = false)
{
    PageSectionV2 section;
    section.sectionId = "recent";
    section.kind = PageSectionKindV2::RecentlyPlayed;
    for (const auto &id : ids) section.items.append(track(source, id));
    section.hasMore = more;
    section.nextCursor = more ? "provider-private-token" : "";
    return {source, {{section}, {}, false, true}, {}};
}
static QStringList ids(const PageResultV2 &result)
{
    QStringList out;
    for (const auto &s : result.sections)
        for (const auto &i : s.items) out.append(i.ref.entityId);
    return out;
}
class AggregateComposerTest : public QObject {
    Q_OBJECT
private slots:
    // A source-order dependent traversal or concatenation breaks fairness.
    void roundRobinsDeterministically()
    {
        AggregateComposer c;
        QCOMPARE(ids(c.compose({page("office", {"o1", "o2"}),
                                page("home", {"h1", "h2"})}, 4)),
                 QStringList({"h1", "o1", "h2", "o2"}));
    }
    // Title/local-ID dedup loses unrelated recordings; no dedup repeats reliable IDs.
    void deduplicatesOnlyReliableIds()
    {
        AggregateComposer c;
        auto a = track("home", "a", {{"isrc", "CN-A01-24-00001"}});
        auto b = track("office", "b", a.externalIds);
        auto d = track("office", "d", {{"musicBrainzRecordingId", "recording-1"}});
        auto e = track("home", "e", d.externalIds);
        QCOMPARE(c.composeItems({{a, e}, {b, track("office", "a"), d}}, 10).size(), 3);
    }
    // Truncation must retain all five overfetched rows across requests.
    void preservesOverflowAndRoundRobinPosition()
    {
        AggregateComposer c;
        auto first = c.compose({page("home", {"h1", "h2", "h3"}),
                                page("office", {"o1", "o2", "o3"})}, 1, {}, "scope");
        QCOMPARE(ids(first), QStringList({"h1"}));
        QString cursor = first.sections[0].nextCursor;
        QStringList rest;
        for (int n = 0; n < 5; ++n) {
            auto next = c.compose({}, 1, cursor, "scope");
            rest.append(ids(next));
            cursor = next.sections[0].nextCursor;
        }
        QCOMPARE(rest, QStringList({"o1", "h2", "o2", "h3", "o3"}));
        QVERIFY(cursor.isEmpty());
    }
    void rejectsMalformedForeignAndWrongScopeCursors()
    {
        AggregateComposer c, other;
        auto result = c.compose({page("home", {"h1"}, true)}, 1, {}, "scope");
        const auto cursor = result.sections[0].nextCursor;
        QVERIFY(c.decodeCursor(cursor, "scope"));
        QVERIFY(!c.decodeCursor(cursor, "elsewhere"));
        QVERIFY(!other.decodeCursor(cursor, "scope"));
        for (const QString &bad : {QString("!"), QString("e30"), cursor + "="})
            QVERIFY(!c.decodeCursor(bad, "scope"));
        const auto json = QByteArray::fromBase64(cursor.toLatin1(), QByteArray::Base64UrlEncoding);
        QVERIFY(!json.contains("provider-private-token"));
        QVERIFY(!json.contains("sourceCursors"));
    }
    void deduplicatesAcrossPagination()
    {
        AggregateComposer c;
        auto a = page("home", {"h1", "h2"});
        auto b = page("office", {"o1", "o2"});
        a.page.sections[0].items[0].externalIds = {{"isrc", "same"}};
        b.page.sections[0].items[0].externalIds = {{"isrc", "same"}};
        auto first = c.compose({a,b}, 1, {}, "scope");
        auto next = c.compose({}, 10, first.sections[0].nextCursor, "scope");
        QCOMPARE(ids(next), QStringList({"h2", "o2"}));
    }
    void siblingSectionsKeepIndependentDedupHistory()
    {
        AggregateComposer c;
        auto a=page("home",{"h1","h2"}), b=page("office",{"o1","o2"});
        for (auto source:{&a,&b}) {
            source->page.sections[0].items[0].externalIds={{"isrc","shared-recording"}};
            auto sibling=source->page.sections[0]; sibling.kind=PageSectionKindV2::FrequentlyPlayed;
            sibling.sectionId="frequent";
            for (auto &item:sibling.items) item.ref.entityId="f"+item.ref.entityId;
            source->page.sections.append(sibling);
        }
        const QHash<PageSectionKindV2,QString> scopes{{PageSectionKindV2::RecentlyPlayed,"recent-query"},
                                                     {PageSectionKindV2::FrequentlyPlayed,"frequent-query"}};
        auto first=c.compose({a,b},1,{},"root-query",scopes);
        QCOMPARE(ids(first),QStringList({"h1","fh1"}));
        const auto recentToken=first.sections[0].nextCursor, frequentToken=first.sections[1].nextCursor;
        QVERIFY(!c.decodeCursor(recentToken,"frequent-query"));
        auto recent=c.compose({},1,recentToken,"recent-query",scopes);
        QCOMPARE(ids(recent),QStringList({"h2"}));
        auto frequent=c.compose({},1,frequentToken,"frequent-query",scopes);
        QCOMPARE(ids(frequent),QStringList({"fh2"}));
        QCOMPARE(ids(c.compose({},1,recent.sections[0].nextCursor,"recent-query",scopes)),QStringList({"o2"}));
        QCOMPARE(ids(c.compose({},1,frequent.sections[0].nextCursor,"frequent-query",scopes)),QStringList({"fo2"}));
    }
};
QTEST_GUILESS_MAIN(AggregateComposerTest)
#include "tst_AggregateComposer.moc"
