#include "v2/SourceV2Types.h"

#include <QJsonObject>
#include <QTest>

class SourceV2TypesTest : public QObject {
    Q_OBJECT

private slots:
    void mediaIdentityIncludesSourceInstance();
    void availabilityPreservesReasonAndConstraints();
    void negotiatedLayersRequireExplicitGrants();
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

void SourceV2TypesTest::negotiatedLayersRequireExplicitGrants()
{
    CapabilitySetV2 capabilities;
    capabilities.serverActions.insert(SourceActionV2::Play,
                                {AvailabilityV2::Available, {}, {}});
    QCOMPARE(capabilities.serverAction(SourceActionV2::Play).state, AvailabilityV2::Available);
    QCOMPARE(capabilities.accountAction(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
    QCOMPARE(capabilities.accountAction(SourceActionV2::Play).reasonKey, QString("source.permission.unknown"));
    QCOMPARE(capabilities.action(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
    capabilities.accountActions.insert(SourceActionV2::Play, {AvailabilityV2::Available, {}, {}});
    QCOMPARE(capabilities.action(SourceActionV2::Play).state, AvailabilityV2::Available);
    QCOMPARE(capabilities.action(SourceActionV2::Download).state,
             AvailabilityV2::Unavailable);
    QCOMPARE(capabilities.serverAction(SourceActionV2::Download).reasonKey, QString("source.capability.unknown"));
    capabilities.accountActions[SourceActionV2::Play] = {AvailabilityV2::Forbidden, "account.denied", {}};
    QCOMPARE(capabilities.action(SourceActionV2::Play).reasonKey, QString("account.denied"));
    capabilities.serverActions[SourceActionV2::Play] = {AvailabilityV2::Available, {}, {{"maxBitrate", 128}}};
    capabilities.accountActions[SourceActionV2::Play] = {AvailabilityV2::Available, {}, {{"maxBitrate", 256}}};
    QCOMPARE(capabilities.action(SourceActionV2::Play).constraints.value("maxBitrate").toDouble(), 128.0);
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
