#include "v2/SourceV2Types.h"

#include <QJsonObject>
#include <QTest>

class SourceV2TypesTest : public QObject {
    Q_OBJECT

private slots:
    void mediaIdentityIncludesSourceInstance();
    void availabilityPreservesReasonAndConstraints();
    void capabilityLookupDefaultsToUnsupported();
    void pageQueryDefaultsToAggregateRecommendations();
};

void SourceV2TypesTest::mediaIdentityIncludesSourceInstance()
{
    const MediaRefV2 a{"navidrome", "home", "admin", MediaEntityTypeV2::Album, "42"};
    const MediaRefV2 b{"navidrome", "office", "admin", MediaEntityTypeV2::Album, "42"};
    const QVariantMap serialized = mediaRefV2ToVariantMap(a);

    QVERIFY(a != b);
    QCOMPARE(serialized.value(QStringLiteral("entityType")).toInt(),
             static_cast<int>(MediaEntityTypeV2::Album));
    QCOMPARE(mediaRefV2FromVariantMap(serialized), a);
}

void SourceV2TypesTest::availabilityPreservesReasonAndConstraints()
{
    const ActionAvailabilityV2 value{AvailabilityV2::Forbidden,
                                     "source.permission.download",
                                     {{"sameSourceOnly", true}}};

    const auto restored = actionAvailabilityV2FromJson(actionAvailabilityV2ToJson(value));

    QCOMPARE(restored, value);
}

void SourceV2TypesTest::capabilityLookupDefaultsToUnsupported()
{
    CapabilitySetV2 capabilities;
    capabilities.actions.insert(SourceActionV2::Play,
                                {AvailabilityV2::Available, {}, {}});

    QCOMPARE(capabilities.action(SourceActionV2::Play).state, AvailabilityV2::Available);
    QCOMPARE(capabilities.action(SourceActionV2::Download).state,
             AvailabilityV2::Unsupported);
}

void SourceV2TypesTest::pageQueryDefaultsToAggregateRecommendations()
{
    const PageQueryV2 query;

    QCOMPARE(query.page, MusicPageKindV2::Recommendation);
    QCOMPARE(query.section, PageSectionKindV2::RecentlyPlayed);
    QVERIFY(query.scope.isAggregate());
    QCOMPARE(query.limit, 50);
}

QTEST_MAIN(SourceV2TypesTest)
#include "tst_SourceV2Types.moc"
