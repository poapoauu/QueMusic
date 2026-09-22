#include "CapabilityResolver.h"
#include <QTest>
#include <limits>

class CapabilityResolverTest : public QObject {
    Q_OBJECT
private slots:
    void precedenceAndSpecificReason()
    {
        CapabilityResolver resolver;
        const ActionAvailabilityV2 yes{AvailabilityV2::Available, {}, {}};
        const ActionAvailabilityV2 no{AvailabilityV2::Forbidden, "source.permission.download", {}};
        QCOMPARE(resolver.resolve(SourceActionV2::Download, yes, yes, no, yes).reasonKey,
                 QString("source.permission.download"));
        QCOMPARE(resolver.resolve(SourceActionV2::Download, {}, yes, no, yes).state,
                 AvailabilityV2::Unsupported);
        QCOMPARE(resolver.resolve(SourceActionV2::Download, yes, {AvailabilityV2::Unavailable, "server", {}}, no, yes).state,
                 AvailabilityV2::Forbidden);
        QCOMPARE(resolver.resolve(SourceActionV2::Download, no, no, no,
                 {AvailabilityV2::Forbidden, "object", {}}).reasonKey, QString("object"));
    }
    void constraintsNeverBecomeWeaker()
    {
        CapabilityResolver resolver;
        const ActionAvailabilityV2 a{AvailabilityV2::Available, {}, {{"sameSourceOnly", true}, {"maxBitrate", 192}}};
        const ActionAvailabilityV2 b{AvailabilityV2::Available, {}, {{"sameSourceOnly", false}, {"maxBitrate", 320}}};
        auto result = resolver.resolve(SourceActionV2::Download, a, b, b, b);
        QCOMPARE(result.state, AvailabilityV2::Available);
        QCOMPARE(result.constraints.value("maxBitrate").toDouble(), 192.0);
        QVERIFY(result.constraints.value("sameSourceOnly").toBool());
    }
    void unsafeRestrictionsFailClosedInEveryLayer()
    {
        QObject object;
        const QList<QVariantMap> invalid = {
            {{"sameSourceOnly", "false"}}, {{"maxBitrate", -1}},
            {{"maxBitrate", std::numeric_limits<double>::infinity()}},
            {{"maxBitrate", QVariant::fromValue(&object)}}, {{"credential", "secret"}},
            {{"maxBitrate", QVariant{}}}, {{"maxBitrate", true}}
        };
        for (const auto &constraints : invalid) for (int layer = 0; layer < 4; ++layer) {
            QList<ActionAvailabilityV2> values(4, {AvailabilityV2::Available, {}, {}});
            values[layer].constraints = constraints;
            const auto result = CapabilityResolver{}.resolve(SourceActionV2::Download,
                values[0], values[1], values[2], values[3]);
            QCOMPARE(result.state, AvailabilityV2::Unavailable);
            QCOMPARE(result.reasonKey, QString("music.actionConstraintsUnsupported"));
            QVERIFY(result.constraints.isEmpty());
        }
    }
    void unknownConstraintConflictCannotGrant()
    {
        const ActionAvailabilityV2 a{AvailabilityV2::Available, {}, {{"mode", "a"}}};
        const ActionAvailabilityV2 b{AvailabilityV2::Available, {}, {{"mode", "b"}}};
        QCOMPARE(CapabilityResolver{}.resolve(SourceActionV2::Download,a,b,a,b).state,
                 AvailabilityV2::Unavailable);
    }
    void sameSourceConstraintRejectsForeignPlaylistItem()
    {
        const MediaRefV2 playlist{"navidrome", "navidrome/home", "home", MediaEntityTypeV2::Playlist, "p"};
        const MediaRefV2 local{"local", "local/default", "default", MediaEntityTypeV2::Track, "t"};
        auto result = CapabilityResolver{}.canAddToPlaylist(playlist, local);
        QCOMPARE(result.state, AvailabilityV2::Unsupported);
        QVERIFY(result.constraints.value("sameSourceOnly").toBool());
        auto own = playlist; own.entityType = MediaEntityTypeV2::Track;
        QCOMPARE(CapabilityResolver{}.canAddToPlaylist(playlist, own).state, AvailabilityV2::Available);
        own.accountId = "office";
        QCOMPARE(CapabilityResolver{}.canAddToPlaylist(playlist, own).state, AvailabilityV2::Unsupported);
    }
};
QTEST_GUILESS_MAIN(CapabilityResolverTest)
#include "tst_CapabilityResolver.moc"
