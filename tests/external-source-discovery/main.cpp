#include <extensions/discovery/v1/IDiscoveryProviderV1.h>
#include <extensions/artist-browse/v1/IArtistBrowseProviderV1.h>
#include <extensions/browse-facets/v1/IBrowseFacetsProviderV1.h>
#include <v2/IMusicSourceSessionV2.h>
#include <v2/ISourceProvidersV2.h>
#include <v2/IMusicSourcePluginV2.h>
#include <QSignalSpy>
#include <QTest>
#include <type_traits>

namespace {
BrowseFacetSchemaV1 facetSchema()
{
    return {PageSectionKindV2::Artists, "catalog-v1", {
        {"origin", "来源", {{"alpha", "甲"}, {"beta", "乙"}}},
        {"period", "时期", {{"recent", "Recent"}, {"classic", "Classic"}}}}};
}
PageQueryV2 facetQuery()
{
    PageQueryV2 query; query.page = MusicPageKindV2::Category; query.section = PageSectionKindV2::Artists;
    query.scope.sourceInstanceId = "source/account";
    query.filters = {{browseFacetsFilterKeyV1(), "selected"}, {"facetRevision", "catalog-v1"},
                     {"facetValues", QVariantMap{{"origin", "alpha"}}}};
    return query;
}
}

class OldSession : public IMusicSourceSessionV2, public IPageProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2)
public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;
    SourceIdentityV2 identity() const override { return {}; }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Ready; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override { return {}; }
    void close() override {}
    void cancel(const QUuid &id) override { cancelled=id; }
    QUuid fetchPage(const PageQueryV2 &) override { ++ordinaryCalls; return {}; }
    QUuid cancelled;
    int ordinaryCalls=0;
};
class ExtendedSession final : public OldSession, public IDiscoveryProviderV1, public IArtistBrowseProviderV1,
                              public IBrowseFacetsProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IDiscoveryProviderV1 IArtistBrowseProviderV1 IBrowseFacetsProviderV1)
public:
    std::optional<BrowseFacetSchemaV1> browseFacets(PageSectionKindV2 section) const override
    { ++facetCalls; return section == PageSectionKindV2::Artists ? std::optional(facetSchema()) : std::nullopt; }
    mutable int facetCalls = 0;
    std::optional<ArtistBrowseTargetV1> artistBrowseTarget(const MediaRefV2 &ref) const override
    { return ref.entityType == MediaEntityTypeV2::Artist ? std::optional(ArtistBrowseTargetV1::Tracks) : std::nullopt; }
    AvailabilityV2 discoveryAvailability(DiscoveryKindV1 kind) const override
    { return kind==DiscoveryKindV1::PersonalRadio ? AvailabilityV2::Available : AvailabilityV2::Forbidden; }
    QUuid fetchDiscovery(DiscoveryKindV1, const PageQueryV2 &) override
    {
        const auto id=QUuid::createUuid();
        emit requestStarted(id);
        PageSectionV2 tracks; tracks.kind=PageSectionKindV2::Tracks;
        emit pageReady(id,{{tracks},{},false,true});
        return id;
    }
};
static_assert(!std::is_base_of_v<QObject,IDiscoveryProviderV1>);
static_assert(!std::is_base_of_v<IDiscoveryProviderV1,IMusicSourceSessionV2>);
static_assert(QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI==2);
static_assert(!std::is_base_of_v<QObject,IArtistBrowseProviderV1>);
static_assert(!std::is_base_of_v<IArtistBrowseProviderV1,IMusicSourceSessionV2>);
static_assert(!std::is_base_of_v<QObject,IBrowseFacetsProviderV1>);
static_assert(!std::is_base_of_v<IBrowseFacetsProviderV1,IMusicSourceSessionV2>);

class DiscoveryContractTest final : public QObject {
    Q_OBJECT
private slots:
    void installedBrowseFacetsIsOptionalIndependentAndPureValidatorsDoNotCallProviders()
    {
        OldSession old; QVERIFY(!qobject_cast<IBrowseFacetsProviderV1 *>(&old));
        ExtendedSession extended; auto *provider = qobject_cast<IBrowseFacetsProviderV1 *>(&extended); QVERIFY(provider);
        QCOMPARE(QString::fromLatin1(QUEMUSIC_BROWSE_FACETS_PROVIDER_V1_IID),
                 QString("org.quemusic.source.extensions.BrowseFacetsProvider/1.0"));
        const auto schema = provider->browseFacets(PageSectionKindV2::Artists); QVERIFY(schema);
        QVERIFY(isValidBrowseFacetSchemaV1(*schema));
        QVERIFY(!provider->browseFacets(PageSectionKindV2::Albums)); QCOMPARE(extended.facetCalls, 2);
        const auto selection = browseFacetSelectionV1(facetQuery()); QVERIFY(selection);
        QVERIFY(matchesBrowseFacetSelectionV1(*schema, *selection));
        QCOMPARE(extended.facetCalls, 2); QCOMPARE(extended.ordinaryCalls, 0); QCOMPARE(old.ordinaryCalls, 0);
    }
    void browseFacetSchemaRejectsWholeMalformedDeclarationsAndPreservesUnicode()
    {
        QVERIFY(isValidBrowseFacetSchemaV1(facetSchema()));
        for (int variant = 0; variant < 15; ++variant) {
            auto schema = facetSchema();
            if (variant == 0) schema.revision.clear();
            if (variant == 1) schema.revision = QString(65, 'r');
            if (variant == 2) schema.section = PageSectionKindV2::Albums;
            if (variant == 3) schema.section = static_cast<PageSectionKindV2>(999);
            if (variant == 4) schema.facets.clear();
            if (variant == 5) schema.facets[1].id = schema.facets[0].id;
            if (variant == 6) schema.facets[0].id = "artistId";
            if (variant == 7) schema.facets[0].id = "quemusic.private";
            if (variant == 8) schema.facets[0].title = " \t ";
            if (variant == 9) schema.facets[0].title = QString(257, 't');
            if (variant == 10) schema.facets[0].choices.clear();
            if (variant == 11) schema.facets[0].choices[1].value = schema.facets[0].choices[0].value;
            if (variant == 12) schema.facets[0].choices[0].value = "alpha&token=PRIVATE";
            if (variant == 13) schema.facets[0].choices[0].title = "line\nfeed";
            if (variant == 14) schema.facets[0].choices[0].title = QString(QChar(ushort(0xD800)));
            QVERIFY2(!isValidBrowseFacetSchemaV1(schema), qPrintable(QString::number(variant)));
        }
        for (const auto &id : {QString("facetRevision"), QString("facetValues"), QString("playlistId"), QString("genreId")})
            QVERIFY(!isBrowseFacetIdV1(id));
        for (const auto &bad : {QString(), QString(" alpha"), QString("alpha "), QString("_alpha"),
             QString(".alpha"), QString("-alpha"), QString("甲"), QString("alpha/one"), QString("alpha\n")})
            QVERIFY(!isBrowseFacetIdentifierV1(bad));
        QVERIFY(isBrowseFacetIdentifierV1("0001")); QVERIFY(isBrowseFacetIdentifierV1("A.z_0-9"));
        QVERIFY(isBrowseFacetIdentifierV1(QString(64, 'a'))); QVERIFY(!isBrowseFacetIdentifierV1(QString(65, 'a')));
        QVERIFY(isBrowseFacetTitleV1(QString::fromUtf8("歌曲 🎵")));
        QVERIFY(!isBrowseFacetTitleV1(QString(QChar(ushort(0xDC00)))));
        QVERIFY(!isBrowseFacetTitleV1(QString(QChar(0))));
        BrowseFacetSchemaV1 maximum; maximum.revision = QString(64, 'r');
        for (int i = 0; i < 8; ++i) {
            BrowseFacetV1 facet{QString("facet%1").arg(i), QString(256, 't'), {}};
            for (int j = 0; j < 64; ++j) facet.choices.append(BrowseFacetChoiceV1{QString("value%1").arg(j), QString(256, 'c')});
            maximum.facets.append(facet);
        }
        QVERIFY(isValidBrowseFacetSchemaV1(maximum));
        maximum.facets[0].choices.append(BrowseFacetChoiceV1{"extra", "Extra"}); QVERIFY(!isValidBrowseFacetSchemaV1(maximum));
        maximum.facets[0].choices.removeLast(); maximum.facets.append(BrowseFacetV1{"extra", "Extra", {{"one", "One"}}});
        QVERIFY(!isValidBrowseFacetSchemaV1(maximum));
    }
    void browseFacetHostAndProviderShapesAreExactAndNeverCoerceValues()
    {
        auto valid = facetQuery(); valid.limit = 1; valid.cursor = "opaque-cursor";
        QVERIFY(browseFacetSelectionV1(valid)); valid.limit = 100; QVERIFY(browseFacetSelectionV1(valid));
        QVERIFY(!browseFacetProviderSelectionV1(valid));
        auto providerQuery = valid; providerQuery.filters.remove(browseFacetsFilterKeyV1());
        QVERIFY(browseFacetProviderSelectionV1(providerQuery)); QVERIFY(!browseFacetSelectionV1(providerQuery));
        for (int variant = 0; variant < 19; ++variant) {
            auto query = facetQuery();
            if (variant == 0) query.page = MusicPageKindV2::Search;
            if (variant == 1) query.section = PageSectionKindV2::Tracks;
            if (variant == 2) query.scope.sourceInstanceId.clear();
            if (variant == 3) query.scope.sourceInstanceId = "   ";
            if (variant == 4) query.searchText = "unexpected";
            if (variant == 5) query.limit = 0;
            if (variant == 6) query.limit = 101;
            if (variant == 7) query.filters[browseFacetsFilterKeyV1()] = true;
            if (variant == 8) query.filters[browseFacetsFilterKeyV1()] = "other";
            if (variant == 9) query.filters["facetRevision"] = 1;
            if (variant == 10) query.filters["facetRevision"] = "";
            if (variant == 11) query.filters["facetValues"] = QVariantMap{};
            if (variant == 12) query.filters["facetValues"] = QVariantHash{{"origin", "alpha"}};
            if (variant == 13) query.filters["facetValues"] = QStringList{"alpha"};
            if (variant == 14) query.filters["facetValues"] = QVariantMap{{"origin", 1}};
            if (variant == 15) query.filters["facetValues"] = QVariantMap{{"origin", true}};
            if (variant == 16) query.filters["facetValues"] = QVariantMap{{"artistId", "alpha"}};
            if (variant == 17) query.filters.insert(artistBrowseFilterKeyV1(), "tracks");
            if (variant == 18) query.filters.remove("facetRevision");
            QVERIFY2(!browseFacetSelectionV1(query), qPrintable(QString::number(variant)));
            query.filters.remove(browseFacetsFilterKeyV1());
            // Removing a bad Host selector can leave a valid Provider shape.
            // This is why Host must validate BEFORE removing anything.
            if (variant == 7 || variant == 8) QVERIFY(browseFacetProviderSelectionV1(query));
            else QVERIFY2(!browseFacetProviderSelectionV1(query), qPrintable(QString::number(variant)));
        }
        auto excessive = facetQuery(); QVariantMap values;
        for (int i = 0; i < 9; ++i) values.insert(QString("f%1").arg(i), "one");
        excessive.filters["facetValues"] = values; QVERIFY(!browseFacetSelectionV1(excessive));
    }
    void browseFacetSelectionsRequireCurrentRevisionKnownChoicesAndStrictTypes()
    {
        const auto schema = facetSchema(); auto selection = *browseFacetSelectionV1(facetQuery());
        QVERIFY(matchesBrowseFacetSelectionV1(schema, selection));
        selection.values.insert("period", "recent"); QVERIFY(matchesBrowseFacetSelectionV1(schema, selection));
        for (int variant = 0; variant < 7; ++variant) {
            auto wrong = selection;
            if (variant == 0) wrong.revision = "catalog-v2";
            if (variant == 1) wrong.values.clear();
            if (variant == 2) wrong.values.insert("unknown", "alpha");
            if (variant == 3) wrong.values["origin"] = "unknown";
            if (variant == 4) wrong.values["origin"] = true;
            if (variant == 5) wrong.values["origin"] = QStringList{"alpha", "beta"};
            if (variant == 6) wrong.values["origin"] = 1;
            QVERIFY(!matchesBrowseFacetSelectionV1(schema, wrong));
        }
        auto changed = schema; changed.revision = "catalog-v2";
        QVERIFY(!matchesBrowseFacetSelectionV1(changed, selection));
        changed = schema; changed.facets[0].choices.removeFirst();
        QVERIFY(!matchesBrowseFacetSelectionV1(changed, selection));
        changed = schema; changed.facets[0].title = "Translated label";
        QVERIFY(matchesBrowseFacetSelectionV1(changed, selection));
        changed.facets[0].choices[0].title.clear(); QVERIFY(!matchesBrowseFacetSelectionV1(changed, selection));
        changed = schema; changed.facets[0].choices[0].value = "0001";
        selection.values["origin"] = "0001"; QVERIFY(matchesBrowseFacetSelectionV1(changed, selection));
        selection.values["origin"] = 1; QVERIFY(!matchesBrowseFacetSelectionV1(changed, selection));
    }
    void installedArtistBrowseExtensionIsOptionalAndQueryIsExact()
    {
        OldSession old; QVERIFY(!qobject_cast<IArtistBrowseProviderV1 *>(&old));
        ExtendedSession extended; auto *provider = qobject_cast<IArtistBrowseProviderV1 *>(&extended); QVERIFY(provider);
        QCOMPARE(QString::fromLatin1(QUEMUSIC_ARTIST_BROWSE_PROVIDER_V1_IID),
                 QString("org.quemusic.source.extensions.ArtistBrowseProvider/1.0"));
        MediaRefV2 ref; ref.entityType = MediaEntityTypeV2::Artist;
        QCOMPARE(provider->artistBrowseTarget(ref).value(), ArtistBrowseTargetV1::Tracks);
        ref.entityType = MediaEntityTypeV2::Track; QVERIFY(!provider->artistBrowseTarget(ref));
        PageQueryV2 query; query.page = MusicPageKindV2::Category; query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = "source/account";
        query.filters = {{"artistId", "artist"}, {artistBrowseFilterKeyV1(), "tracks"}};
        QVERIFY(isArtistTracksQueryV1(query));
        query.filters[artistBrowseFilterKeyV1()] = "albums"; QVERIFY(!isArtistTracksQueryV1(query));
        query.filters[artistBrowseFilterKeyV1()] = "tracks"; query.scope.sourceInstanceId.clear();
        QVERIFY(!isArtistTracksQueryV1(query)); QCOMPARE(extended.ordinaryCalls, 0);
    }
    void interfaceRemainsOptionalAndSeparatelyVersioned()
    {
        QCOMPARE(QString::fromLatin1(QUEMUSIC_DISCOVERY_PROVIDER_V1_IID),
                 QString("org.quemusic.source.extensions.DiscoveryProvider/1.0"));
        OldSession old; QVERIFY(qobject_cast<IPageProviderV2 *>(&old));
        QVERIFY(!qobject_cast<IDiscoveryProviderV1 *>(&old));
        ExtendedSession extended;
        auto *provider=qobject_cast<IDiscoveryProviderV1 *>(&extended); QVERIFY(provider);
        QCOMPARE(provider->discoveryAvailability(DiscoveryKindV1::PersonalRadio),AvailabilityV2::Available);
        QCOMPARE(provider->discoveryAvailability(DiscoveryKindV1::PersonalRadar),AvailabilityV2::Forbidden);
        QSignalSpy started(&extended,&IMusicSourceSessionV2::requestStarted);
        QSignalSpy ready(&extended,&IMusicSourceSessionV2::pageReady);
        auto query=discoveryQueryV1(DiscoveryKindV1::PersonalRadio,{"source/account"});
        query.filters.clear();
        const auto id=provider->fetchDiscovery(DiscoveryKindV1::PersonalRadio,query);
        QCOMPARE(started.size(),1); QCOMPARE(ready.size(),1);
        QCOMPARE(started[0][0].toUuid(),id); QCOMPARE(ready[0][0].toUuid(),id);
        extended.cancel(id); QCOMPARE(extended.cancelled,id);
        QCOMPARE(extended.ordinaryCalls,0);
    }
    void selectorsAreExactAndNeverRandom()
    {
        auto radio=discoveryQueryV1(DiscoveryKindV1::PersonalRadio);
        auto radar=discoveryQueryV1(DiscoveryKindV1::PersonalRadar);
        QVERIFY(discoveryKindV1(radio)==DiscoveryKindV1::PersonalRadio);
        QVERIFY(discoveryKindV1(radar)==DiscoveryKindV1::PersonalRadar);
        QVERIFY(!discoveryKindV1({}));
        radio.section=PageSectionKindV2::Random; QVERIFY(!discoveryKindV1(radio));
        radar.filters["genre"]="Rock"; QVERIFY(!discoveryKindV1(radar));
        QVERIFY(!discoveryKindV1(discoveryQueryV1(DiscoveryKindV1(999))));
    }
};
QTEST_GUILESS_MAIN(DiscoveryContractTest)
#include "main.moc"
