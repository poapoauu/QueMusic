#if defined(QUEMUSIC_PLAYBACK_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QPointer>

class PlaybackSession : public IMusicSourceSessionV2, public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPlaybackProviderV2)
public:
    SourceConfigurationV2 config;
    SourceSessionStateV2 status = SourceSessionStateV2::Ready;
    PlaybackSession(SourceConfigurationV2 c, QObject *p) : IMusicSourceSessionV2(p), config(c) {}
    SourceIdentityV2 identity() const override {
        return {"task12c", config.sourceInstanceId, property("wrongIdentity").toBool() ? "foreign" : config.accountId, {}};
    }
    SourceSessionStateV2 state() const override { return status; }
    CapabilitySetV2 capabilities() const override {
        CapabilitySetV2 c;
        for (auto a : {SourceActionV2::Play, SourceActionV2::Scrobble}) {
            if (!property("missingServer").toBool()) c.serverActions[a] = {AvailabilityV2::Available, {}, property("constraints").toMap()};
            if (!property("missingAccount").toBool()) c.accountActions[a] = {AvailabilityV2::Available, {}, {}};
        }
        if (property("denyScrobble").toBool()) c.accountActions[SourceActionV2::Scrobble] = {AvailabilityV2::Forbidden, {}, {}};
        emit const_cast<PlaybackSession *>(this)->capabilitiesRead();
        return c;
    }
    QUuid open() override { auto id=QUuid::createUuid(); emit requestStarted(id); emit actionCompleted(id,{}); return id; }
    void close() override { status=SourceSessionStateV2::Closing; emit stateChanged(status); }
    void cancel(const QUuid &id) override { setProperty("cancelled",id); setProperty("cancelCount",property("cancelCount").toInt()+1); }
    QUuid fetchArtwork(const MediaRefV2 &) override { return {}; }
    QUuid fetchLyrics(const MediaRefV2 &) override { return {}; }
    QUuid resolveStream(const MediaRefV2 &ref) override {
        auto id=QUuid::createUuid(); const bool mismatch=property("mismatchId").toBool();
        const bool async=property("async").toBool();
        StreamDescriptorV2 stream; stream.media=ref;
        stream.url=QUrl(property("url").isValid()?property("url").toString():QString("https://example.org/song?token=private"));
        stream.headers={{"Authorization","Bearer private"}};
        if (property("wrongSubject").toBool()) stream.media.entityId="other";
        if (property("expired").toBool()) stream.expiresAt=QDateTime::currentDateTimeUtc().addSecs(-1);
        setProperty("resolutions",property("resolutions").toInt()+1); setProperty("request",id);
        setProperty("stream",QVariant::fromValue(stream));
        QPointer<PlaybackSession> guard(this);
        if (!property("skipStarted").toBool()) emit requestStarted(id);
        if (guard && !async) emit streamReady(id,stream);
        return mismatch?QUuid::createUuid():id;
    }
signals:
    void capabilitiesRead();
};
class ScrobbleSession : public PlaybackSession, public IScrobbleProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IScrobbleProviderV2)
public:
    using PlaybackSession::PlaybackSession;
    QUuid scrobble(const MediaRefV2 &ref,qint64 position,bool submission) override {
        const auto id=QUuid::createUuid(); const bool failure=property("failure").toBool();
        const bool async=property("asyncScrobble").toBool();
        auto attempts=property("attempts").toList();
        attempts.append(QVariantMap{{"submission",submission},{"position",position}}); setProperty("attempts",attempts);
        setProperty("scrobbleRequest",id);
        QPointer<ScrobbleSession> guard(this); emit requestStarted(id);
        if (guard && !async) {
            if (failure) emit requestFailed(id,{SourceErrorKindV2::Unknown,"secret","private",{},false});
            else emit actionCompleted(id,{SourceActionV2::Scrobble,ref,{{"positionMs",position},{"submission",submission}}});
        }
        return id;
    }
};
class PlaybackPlugin : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override {
        SourceDescriptorV2 d{"org.quemusic.source.task12c","task12c","Task12c","2.0.0",2,{}};
        if (!property("missingPlay").toBool()) d.declaredActions[SourceActionV2::Play]={AvailabilityV2::Available,{},property("constraints").toMap()};
        d.declaredActions[SourceActionV2::Scrobble]={AvailabilityV2::Available,{},{}};
        emit const_cast<PlaybackPlugin *>(this)->descriptorRead(); return d;
    }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &c,QObject *p) override {
        if (c.accountId=="bare") return new PlaybackSession(c,p);
        return new ScrobbleSession(c,p);
    }
signals:
    void descriptorRead();
};
#else
#include "PlaybackCoordinator.h"
#include "PlaybackSink.h"
#include "SourceAccountStore.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QDir>
#include <QFile>
#include <functional>

class Sink final : public PlaybackSink {
public:
    int prepares=0, plays=0, stops=0; bool accepted=true;
    StreamDescriptorV2 stream; QUuid generation;
    std::function<void()> onPrepare,onPlay,onStop;
    bool prepare(StreamDescriptorV2 s,QUuid g) override { ++prepares; stream=s; generation=g; auto callback=onPrepare; bool result=accepted; if(callback)callback(); return result; }
    void play(QUuid g) override { ++plays; generation=g; auto callback=onPlay; if(callback)callback(); }
    void stop(QUuid) override { ++stops; auto callback=onStop; if(callback)callback(); }
};
class Secrets final : public ISecretStore {
public:
    bool write(const QString &,const QByteArray &,QString *) override { return true; }
    std::optional<QByteArray> read(const QString &,QString *) const override { return QByteArray("secret"); }
    bool remove(const QString &,QString *) override { return true; }
};
struct Harness {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"),QSettings::IniFormat};
    Secrets secrets; SourceAccountStore accounts{&settings,&secrets};
    PluginManager plugins; SourceRegistry registry{&plugins,&accounts};
    Sink sink; PlaybackCoordinator coordinator{&registry,&sink};
    bool init(bool isolated=false) {
        QString packages=QUEMUSIC_TASK12C_PACKAGES;
        if (isolated) {
            packages=dir.filePath("packages"); const auto target=packages+"/task12c";
            if (!QDir().mkpath(target)) return false;
            QDir source(QString(QUEMUSIC_TASK12C_PACKAGES)+"/task12c");
            for (const auto &file:source.entryList(QDir::Files)) if(!QFile::copy(source.filePath(file),target+'/'+file))return false;
        }
        plugins.addSearchPath(packages);
        return plugins.discover()==1 && plugins.load("org.quemusic.source.task12c")
            && accounts.upsert({"task12c","home","Home",{},{}}) && accounts.upsert({"task12c","bare","Bare",{},{}});
    }
    IMusicSourceSessionV2 *session(QString account="home") { return registry.sessionFor("task12c/"+account); }
    QObject *root() { return plugins.pluginInstance("org.quemusic.source.task12c"); }
};
static QVariantMap item(qint64 duration=10000,QString account="home") {
    QVariantMap actions;
    for(auto a:{SourceActionV2::Play,SourceActionV2::Scrobble}) actions[QString::number(int(a))]=QVariantMap{{"state",int(AvailabilityV2::Available)},{"constraints",QVariantMap{}}};
    return {{"ref",mediaRefV2ToVariantMap({"task12c","task12c/"+account,account,MediaEntityTypeV2::Track,"42"})},
        {"availableActions",actions},{"title","Track"},{"durationMs",duration},{"url","private"},{"headers",QVariantMap{{"secret","private"}}}};
}
class PlaybackCoordinatorTest : public QObject {
    Q_OBJECT
private slots:
    void thresholds_data() {
        QTest::addColumn<qint64>("duration"); QTest::addColumn<qint64>("threshold");
        QTest::newRow("half")<<qint64(10000)<<qint64(5000);
        QTest::newRow("unknown")<<qint64(0)<<qint64(240000);
        QTest::newRow("long")<<qint64(900000)<<qint64(240000);
    }
    void thresholds() {
        QFETCH(qint64,duration); QFETCH(qint64,threshold);
        Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("failure",true);
        auto id=h.coordinator.play(item(duration)); QVERIFY(!id.isNull()); QTRY_COMPARE(h.sink.plays,1);
        auto attempts=s->property("attempts").toList(); QCOMPARE(attempts.size(),1); QVERIFY(!attempts[0].toMap()["submission"].toBool());
        QVERIFY(h.coordinator.reportPosition(id,threshold-1)); QCOMPARE(s->property("attempts").toList().size(),1);
        QVERIFY(h.coordinator.reportPosition(id,threshold)); QCOMPARE(s->property("attempts").toList().size(),2);
        h.coordinator.reportPosition(id,threshold+100); QCOMPARE(s->property("attempts").toList().size(),2);
        QCOMPARE(s->property("attempts").toList()[1].toMap()["position"].toLongLong(),threshold);
        QCOMPARE(h.sink.stream.url,QUrl("https://example.org/song?token=private"));
        QCOMPARE(h.sink.stream.headers.value("Authorization"),QString("Bearer private"));
    }
    void queueOccurrencesAreSafeAndDoNotResolve() {
        Harness h; QVERIFY(h.init()); auto s=h.session();
        auto a=h.coordinator.enqueue(item()),b=h.coordinator.enqueue(item()); QVERIFY(a!=b); QVERIFY(!a.isNull());
        QCOMPARE(s->property("resolutions").toInt(),0); QCOMPARE(h.coordinator.queue().size(),2);
        for(auto value:h.coordinator.queue()) { auto m=value.toMap(); QVERIFY(!m.contains("url")); QVERIFY(!m.contains("headers")); QCOMPARE(m["title"].toString(),QString("Track")); }
        auto first=h.coordinator.playQueueEntry(0); QTRY_COMPARE(h.sink.plays,1); h.coordinator.reportPosition(first,5000);
        auto second=h.coordinator.playQueueEntry(1); QVERIFY(first!=second); QTRY_COMPARE(h.sink.plays,2); h.coordinator.reportPosition(second,5000);
        QCOMPARE(s->property("attempts").toList().size(),4); QCOMPARE(h.coordinator.currentIndex(),1);
        QVERIFY(!h.coordinator.reportPosition(first,99999)); QVERIFY(!h.coordinator.reportPosition(second,-1));
        QVERIFY(h.coordinator.reportStopped(second)); QVERIFY(h.coordinator.currentItem().isEmpty()); QCOMPARE(h.coordinator.currentIndex(),-1);
        QVERIFY(!h.coordinator.reportPosition(second,99999));
    }
    void switchCancelsAndIgnoresLateStreams() {
        Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        auto first=h.coordinator.play(item()); auto pid=s->property("request").toUuid(); auto stream=s->property("stream").value<StreamDescriptorV2>();
        s->setProperty("async",false); auto second=h.coordinator.play(item());
        QCOMPARE(s->property("cancelled").toUuid(),pid); emit s->streamReady(pid,stream);
        QTRY_COMPARE(h.sink.plays,1); QCOMPARE(h.sink.generation,second); QVERIFY(first!=second);
        QVERIFY(h.coordinator.stop()); QVERIFY(h.coordinator.currentGeneration().isNull());
    }
    void optionalScrobble() {
        Harness h; QVERIFY(h.init());
        auto id=h.coordinator.play(item(10000,"bare")); QTRY_COMPARE(h.sink.plays,1); h.coordinator.reportPosition(id,5000);
        auto s=h.session(); s->setProperty("denyScrobble",true);
        id=h.coordinator.play(item()); QTRY_COMPARE(h.sink.plays,2); h.coordinator.reportPosition(id,5000); QCOMPARE(s->property("attempts").toList().size(),0);
    }
    void invalidItemsAndLivePermissions() {
        Harness h; QVERIFY(h.init()); auto s=h.session();
        auto bad=item(); auto ref=bad["ref"].toMap(); ref["accountId"]="foreign"; bad["ref"]=ref;
        h.coordinator.play(bad); h.coordinator.play({});
        bad=item(); bad["availableActions"]=QVariantMap{}; h.coordinator.play(bad);
        bad=item(); ref=bad["ref"].toMap(); ref["entityType"]=int(MediaEntityTypeV2::Album); bad["ref"]=ref; QVERIFY(h.coordinator.enqueue(bad).isNull());
        for(auto p:{"missingAccount","missingServer","wrongIdentity"}) { s->setProperty(p,true); h.coordinator.play(item()); s->setProperty(p,false); }
        h.coordinator.enqueue(item()); h.root()->setProperty("missingPlay",true); h.coordinator.playQueueEntry(h.coordinator.queue().size()-1); h.root()->setProperty("missingPlay",false);
        s->setProperty("constraints",QVariantMap{{"maxBitrate",128}}); h.coordinator.play(item());
        QCOMPARE(s->property("resolutions").toInt(),0); QCoreApplication::processEvents(); QCOMPARE(h.sink.plays,0);
    }
    void invalidStreams_data() {
        QTest::addColumn<QString>("property"); QTest::addColumn<QVariant>("value");
        for(auto p:{"wrongSubject","mismatchId","expired","skipStarted"})QTest::newRow(p)<<QString(p)<<QVariant(true);
        QTest::newRow("ftp")<<QString("url")<<QVariant("ftp://example.org/song");
        QTest::newRow("relative")<<QString("url")<<QVariant("song.mp3");
        QTest::newRow("empty")<<QString("url")<<QVariant("");
    }
    void invalidStreams() {
        QFETCH(QString,property); QFETCH(QVariant,value); Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty(property.toUtf8(),value);
        QSignalSpy failed(&h.coordinator,&PlaybackCoordinator::playbackFailed);
        h.coordinator.play(item()); QTRY_COMPARE(failed.size(),1); QCOMPARE(h.sink.prepares,0); QCOMPARE(h.sink.plays,0);
        QVERIFY(!failed[0][1].toString().contains("private"));
    }
    void prepareFailureAndSinkDestruction() {
        Harness h; QVERIFY(h.init()); h.sink.accepted=false;
        QSignalSpy failed(&h.coordinator,&PlaybackCoordinator::playbackFailed);
        h.coordinator.play(item()); QTRY_COMPARE(failed.size(),1); QCOMPARE(h.sink.plays,0); QCOMPARE(h.session()->property("attempts").toList().size(),0);
        auto sink=new Sink; PlaybackCoordinator c(&h.registry,sink); c.play(item()); QTRY_COMPARE(sink->plays,1); delete sink;
        QVERIFY(c.currentGeneration().isNull());
    }
    void leaseAndReentrantBoundaries() {
        Harness h; QVERIFY(h.init()); auto s=h.session();
        h.coordinator.play(item()); QTRY_COMPARE(h.sink.plays,1);
        QCOMPARE(h.plugins.plugin("org.quemusic.source.task12c").activeLeases,2);
        h.coordinator.stop(); QTRY_COMPARE(h.plugins.plugin("org.quemusic.source.task12c").activeLeases,1);
        h.sink.onPrepare=[&] { h.registry.disableInstance("task12c/home"); QCOMPARE(h.plugins.unload("org.quemusic.source.task12c"),PluginOperationResult::Busy); };
        h.coordinator.play(item()); QTRY_VERIFY(h.coordinator.currentGeneration().isNull()); QCOMPARE(h.sink.plays,1);
        QVERIFY(h.registry.enableInstance("task12c/home")); s=h.session(); h.sink.onPrepare={};
        connect(s,&IMusicSourceSessionV2::requestStarted,&h.coordinator,[&] { h.registry.disableInstance("task12c/home"); QCOMPARE(h.plugins.unload("org.quemusic.source.task12c"),PluginOperationResult::Busy); QCoreApplication::processEvents(); });
        h.coordinator.play(item()); QTRY_VERIFY(h.coordinator.currentGeneration().isNull()); QCOMPARE(h.sink.plays,1);
    }
    void destroyedDuringProviderAndSink() {
        Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        QPointer<PlaybackCoordinator> c=new PlaybackCoordinator(&h.registry,&h.sink);
        auto connection=connect(s,&IMusicSourceSessionV2::requestStarted,&h.coordinator,[&] { delete c.data(); });
        c->play(item()); QVERIFY(!c); QCOMPARE(s->property("cancelled").toUuid(),s->property("request").toUuid()); disconnect(connection);
        s->setProperty("async",false); c=new PlaybackCoordinator(&h.registry,&h.sink);
        h.sink.onPrepare=[&] { delete c.data(); }; c->play(item()); QTRY_VERIFY(!c); QCOMPARE(h.sink.plays,0);
    }
    void externalSessionDestruction() {
        Harness h; QVERIFY(h.init(true)); auto s=h.session(); s->setProperty("async",true);
        h.coordinator.play(item()); delete s; QTRY_VERIFY(h.coordinator.currentGeneration().isNull()); QCOMPARE(h.sink.plays,0);
    }
    void asyncTerminalObserverCannotUnloadEmittingPlugin() {
        Harness h; QVERIFY(h.init()); auto s=h.session("bare"); s->setProperty("async",true);
        h.coordinator.play(item(10000,"bare")); const auto pid=s->property("request").toUuid();
        const auto stream=s->property("stream").value<StreamDescriptorV2>();
        PluginOperationResult unload=PluginOperationResult::Success;
        connect(s,&IMusicSourceSessionV2::streamReady,&h.coordinator,[&] {
            QCoreApplication::processEvents();
            h.coordinator.stop(); h.registry.closeInstance("task12c/bare");
            unload=h.plugins.unload("org.quemusic.source.task12c");
        });
        emit s->streamReady(pid,stream);
        QCOMPARE(unload,PluginOperationResult::Busy);
        QTRY_COMPARE(h.plugins.plugin("org.quemusic.source.task12c").activeLeases,0);
    }
    void scrobbleDowngradeAndCorrelation() {
        Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("asyncScrobble",true);
        QSignalSpy done(&h.coordinator,&PlaybackCoordinator::scrobbleFinished);
        auto id=h.coordinator.play(item()); QTRY_COMPARE(h.sink.plays,1);
        auto pid=s->property("scrobbleRequest").toUuid();
        auto ref=mediaRefV2FromVariantMap(item()["ref"].toMap());
        auto foreign=ref; foreign.entityId="wrong";
        emit s->actionCompleted(QUuid::createUuid(),{SourceActionV2::Scrobble,ref,{}});
        emit s->actionCompleted(pid,{SourceActionV2::Scrobble,foreign,{}});
        QTRY_COMPARE(done.size(),1); QCOMPARE(done[0][2].toBool(),false);
        s->setProperty("denyScrobble",true); emit s->capabilitiesChanged({}); QCoreApplication::processEvents();
        QCOMPARE(h.coordinator.currentGeneration(),id); h.coordinator.reportPosition(id,5000);
        QCOMPARE(s->property("attempts").toList().size(),1); QCOMPARE(h.sink.stops,0);
    }
    void asynchronousStreamAndWrongTerminalKind() {
        Harness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        h.coordinator.play(item()); auto pid=s->property("request").toUuid();
        auto stream=s->property("stream").value<StreamDescriptorV2>();
        emit s->streamReady(QUuid::createUuid(),stream); QCoreApplication::processEvents(); QCOMPARE(h.sink.plays,0);
        emit s->streamReady(pid,stream); QTRY_COMPARE(h.sink.plays,1);
        h.coordinator.play(item()); pid=s->property("request").toUuid();
        QSignalSpy failed(&h.coordinator,&PlaybackCoordinator::playbackFailed);
        emit s->actionCompleted(pid,{SourceActionV2::Play,stream.media,{}});
        QTRY_COMPARE(failed.size(),1); QCOMPARE(h.sink.plays,1);
    }
    void destructionCannotReopenPlaybackFromStop() {
        Harness h; QVERIFY(h.init()); auto s=h.session();
        auto c=new PlaybackCoordinator(&h.registry,&h.sink);
        c->play(item()); QTRY_COMPARE(h.sink.plays,1);
        QUuid reopened;
        h.sink.onStop=[&] { reopened=c->play(item()); };
        delete c; h.sink.onStop={};
        QVERIFY(reopened.isNull()); QCOMPARE(s->property("resolutions").toInt(),1);
        QTRY_COMPARE(h.plugins.plugin("org.quemusic.source.task12c").activeLeases,1);
    }
};
QTEST_GUILESS_MAIN(PlaybackCoordinatorTest)
#endif
#include "tst_PlaybackCoordinator.moc"
