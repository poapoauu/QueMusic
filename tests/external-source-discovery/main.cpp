#include <extensions/discovery/v1/IDiscoveryProviderV1.h>
#include <v2/IMusicSourceSessionV2.h>
#include <v2/ISourceProvidersV2.h>
#include <v2/IMusicSourcePluginV2.h>
#include <QSignalSpy>
#include <QTest>
#include <type_traits>

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
class ExtendedSession final : public OldSession, public IDiscoveryProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IDiscoveryProviderV1)
public:
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

class DiscoveryContractTest final : public QObject {
    Q_OBJECT
private slots:
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
