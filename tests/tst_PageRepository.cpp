#if defined(QUEMUSIC_PAGE_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

class PageFixtureSession : public IMusicSourceSessionV2, public IPageProviderV2,
                           public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2)
public:
    SourceConfigurationV2 config;
    SourceSessionStateV2 current = SourceSessionStateV2::Ready;
    PageFixtureSession(SourceConfigurationV2 c, QObject *p) : IMusicSourceSessionV2(p), config(c) {}
    SourceIdentityV2 identity() const override { return {"task5", config.sourceInstanceId, config.accountId, config.displayName}; }
    SourceSessionStateV2 state() const override { return current; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override { auto id = QUuid::createUuid(); emit requestStarted(id); emit actionCompleted(id, {}); return id; }
    void close() override { current = SourceSessionStateV2::Closing; emit stateChanged(current); }
    void cancel(const QUuid &id) override { setProperty("cancelled", id); }
    QUuid fetchPage(const PageQueryV2 &query) override
    {
        auto id = QUuid::createUuid();
        setProperty("lastRequest", id);
        setProperty("lastCursor", query.cursor);
        setProperty("calls", property("calls").toInt() + 1);
        emit requestStarted(id);
        if (property("inlineFailure").toBool()) emit requestFailed(id, {SourceErrorKindV2::Network});
        else if (property("inline").toBool()) emit pageReady(id, sample(query));
        return id;
    }
    PageResultV2 sample(const PageQueryV2 &query)
    {
        PageSectionV2 s;
        s.kind = query.section; s.sectionId = "recent";
        if (!property("empty").toBool()) {
            MediaItemV2 i;
            i.ref = {"task5", config.sourceInstanceId, config.accountId, MediaEntityTypeV2::Track, "song"};
            i.title = config.accountId; s.items.append(i);
        }
        return {{s}, {}, false, true};
    }
    QUuid resolveStream(const MediaRefV2 &) override { return {}; }
    QUuid fetchArtwork(const MediaRefV2 &ref) override { return asset(ref, SourceActionV2::Artwork); }
    QUuid fetchLyrics(const MediaRefV2 &ref) override { return asset(ref, SourceActionV2::Lyrics); }
    QUuid asset(const MediaRefV2 &ref, SourceActionV2 action)
    {
        auto id = QUuid::createUuid();
        setProperty("lastRequest", id);
        setProperty("assetCalls", property("assetCalls").toInt()+1);
        emit requestStarted(id);
        if (property("inline").toBool()) {
            ActionResultV2 r{action, ref, {}};
            if (action == SourceActionV2::Artwork) r.payload = {{"bytes", QByteArray("image-bytes")}, {"mimeType", QString("image/jpeg")}};
            else r.payload = {{"lyrics", QString("[00:01]Words")}};
            emit actionCompleted(id, r);
        }
        return id;
    }
};
class PageFixturePlugin : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override { return {"org.quemusic.source.task5", "task5", "Task5", "2.0.0", 2, {}}; }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &c, QObject *p) override { return new PageFixtureSession(c,p); }
};
#else
#include "PageRepository.h"
#include "MusicPageModel.h"
#include "SourceAccountStore.h"
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTimer>
#include <QtConcurrentRun>
#include <QThreadPool>

class MemorySecrets final : public ISecretStore {
    QHash<QString,QByteArray> values;
public:
    bool write(const QString &key,const QByteArray &value,QString *) override { values.insert(key,value); return true; }
    std::optional<QByteArray> read(const QString &key,QString *) const override { return values.contains(key)?std::optional<QByteArray>(values.value(key)):std::nullopt; }
    bool remove(const QString &key,QString *) override { values.remove(key); return true; }
};
class PageHarness {
public:
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    MemorySecrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    AggregateComposer composer;
    std::shared_ptr<PageCache> cache = std::make_shared<PageCache>(dir.filePath("pages"));
    PageRepository repo{&registry, &composer, nullptr, cache};
    bool init()
    {
        plugins.addSearchPath(QUEMUSIC_TASK5_PACKAGES);
        return plugins.discover() == 1 && plugins.load("org.quemusic.source.task5")
            && accounts.upsert({"task5", "home", "Home", {}, {}})
            && accounts.upsert({"task5", "office", "Office", {}, {}});
    }
    IMusicSourceSessionV2 *session(QString id) { return registry.sessionFor("task5/" + id); }
};
static PageResultV2 sample(QString source, QStringList ids = {"one"}, bool more = false)
{
    PageSectionV2 s; s.sectionId="recent"; s.kind=PageSectionKindV2::RecentlyPlayed;
    s.hasMore=more; s.nextCursor=more ? "private-next" : "";
    for (auto id : ids) {
        MediaItemV2 i; i.ref={"task5", "task5/"+source, source, MediaEntityTypeV2::Track,id};
        i.title=id; s.items.append(i);
    }
    return {{s},{},false,true};
}
class PageRepositoryTest : public QObject {
    Q_OBJECT
private slots:
    void partialSuccessWaitsForEverySource()
    {
        PageHarness h; QVERIFY(h.init());
        auto a=h.session("home"), b=h.session("office");
        QSignalSpy ready(&h.repo,&PageRepository::pageReady), failed(&h.repo,&PageRepository::pageFailed);
        auto id=h.repo.requestPage({},7);
        QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        emit b->requestFailed(b->property("lastRequest").toUuid(), {SourceErrorKindV2::Network});
        QCOMPARE(ready.size(),0);
        emit a->pageReady(a->property("lastRequest").toUuid(),sample("home"));
        QTRY_COMPARE(ready.size(),1);
        QCOMPARE(ready[0][0].toUuid(),id); QCOMPARE(ready[0][1].toULongLong(),7);
        auto result=qvariant_cast<PageResultV2>(ready[0][2]);
        QCOMPARE(result.sections[0].items.size(),1);
        QCOMPARE(result.sourceStates["task5/office"].state,SourcePageLoadStateV2::Failed);
        QVERIFY(result.complete && !result.cached); QCOMPARE(failed.size(),0);
    }
    void synchronousProvidersReturnIdBeforeOneTerminal()
    {
        PageHarness h; QVERIFY(h.init());
        h.session("home")->setProperty("inline",true);
        h.session("office")->setProperty("inline",true);
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        auto id=h.repo.requestPage({},3); QCOMPARE(ready.size(),0);
        QTRY_COMPARE(ready.size(),1); QCOMPARE(ready[0][0].toUuid(),id);
        QCOMPARE(qvariant_cast<PageResultV2>(ready[0][2]).sections[0].items.size(),2);
    }
    void allFailureAndInvalidScopeAreDeferred()
    {
        PageHarness h; QVERIFY(h.init());
        h.session("home")->setProperty("inlineFailure",true);
        h.session("office")->setProperty("inlineFailure",true);
        QSignalSpy failed(&h.repo,&PageRepository::pageFailed), ready(&h.repo,&PageRepository::pageReady);
        h.repo.requestPage({},1); QCOMPARE(failed.size(),0); QTRY_COMPARE(failed.size(),1);
        PageQueryV2 q; q.scope.sourceInstanceId="missing";
        h.repo.requestPage(q,1); QCOMPARE(failed.size(),1); QTRY_COMPARE(failed.size(),2);
        QCOMPARE(ready.size(),0);
    }
    void disabledSourcesAreNotResolvedAndEmptyCompletesOnce()
    {
        PageHarness h; QVERIFY(h.init());
        QVERIFY(h.registry.disableInstance("task5/office"));
        auto a=h.session("home"); a->setProperty("inline",true); a->setProperty("empty",true);
        MusicPageModel model(MusicPageKindV2::Recommendation); auto gen=model.beginRequest();
        int terminal=0;
        connect(&h.repo,&PageRepository::pageReady,&model,[&](QUuid,quint64 g,PageResultV2 p) {
            model.applyResult(g,p); if (!p.cached && p.complete) ++terminal;
        });
        h.repo.requestPage({},gen); QTRY_COMPARE(terminal,1);
        QVERIFY(model.finishGeneration(gen,1)); QCOMPARE(model.state(),PageLoadStateV2::Empty);
    }
    void cancellationDiscardsLateAndDuplicateSignals()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home");
        QSignalSpy ready(&h.repo,&PageRepository::pageReady), failed(&h.repo,&PageRepository::pageFailed);
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        auto id=h.repo.requestPage(q,1); QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        auto providerId=a->property("lastRequest").toUuid(); h.repo.cancel(id);
        QCOMPARE(a->property("cancelled").toUuid(),providerId);
        emit a->pageReady(providerId,sample("home")); emit a->requestFailed(providerId,{});
        QTest::qWait(20); QCOMPARE(ready.size(),0); QCOMPARE(failed.size(),0);
    }
    void registryCloseDuringRequestFinishesFailure()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home");
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        QSignalSpy failed(&h.repo,&PageRepository::pageFailed);
        h.repo.requestPage(q,1); QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        QVERIFY(h.registry.closeInstance("task5/home")); QTRY_COMPARE(failed.size(),1);
        QCOMPARE(h.plugins.unload("org.quemusic.source.task5"),PluginOperationResult::Success);
    }
    void paginationKeepsOverflowAndHidesProviderCursor()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home"), b=h.session("office");
        PageQueryV2 q; q.limit=1;
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        h.repo.requestPage(q,1); QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        emit a->pageReady(a->property("lastRequest").toUuid(),sample("home",{"h1","h2"}));
        emit b->pageReady(b->property("lastRequest").toUuid(),sample("office",{"o1","o2"}));
        QTRY_COMPARE(ready.size(),1);
        QStringList actual={"h1"};
        for (int n=1;n<4;++n) {
            q.cursor=qvariant_cast<PageResultV2>(ready.last()[2]).sections[0].nextCursor;
            h.repo.requestPage(q,1); QTRY_COMPARE(ready.size(),n+1);
            actual.append(qvariant_cast<PageResultV2>(ready.last()[2]).sections[0].items[0].ref.entityId);
        }
        QCOMPARE(actual,QStringList({"h1","o1","h2","o2"}));
        QCOMPARE(a->property("calls").toInt(),1); QCOMPARE(b->property("calls").toInt(),1);
        q.scope.sourceInstanceId="task5/home";
        QSignalSpy failed(&h.repo,&PageRepository::pageFailed);
        h.repo.requestPage(q,1); QTRY_COMPARE(failed.size(),1);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed[0][2]).kind,SourceErrorKindV2::InvalidRequest);
    }
    void cachePreviewDoesNotCountAsAnotherQuery()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home"); a->setProperty("inline",true);
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        h.repo.requestPage(q,1); QTRY_COMPARE(ready.size(),1);
        h.repo.requestPage(q,2); QCOMPARE(ready.size(),1); QTRY_COMPARE(ready.size(),3);
        QVERIFY(qvariant_cast<PageResultV2>(ready[1][2]).cached);
        QVERIFY(!qvariant_cast<PageResultV2>(ready[2][2]).cached);
        MusicPageModel model(MusicPageKindV2::Recommendation); auto gen=model.beginRequest();
        QVERIFY(model.applyResult(gen,qvariant_cast<PageResultV2>(ready[1][2])));
        QVERIFY(!model.finishGeneration(gen,1));
        QVERIFY(model.applyResult(gen,qvariant_cast<PageResultV2>(ready[2][2])));
        QVERIFY(model.finishGeneration(gen,1));
    }
    void cacheIoIsOrderedOffTheCallerThread()
    {
        PageHarness h; QVERIFY(h.init());
        auto a=h.session("home"); a->setProperty("inline",true);
        QSemaphore entered, release;
        auto blocker=QtConcurrent::run(musicCacheIoPool(),[&] { entered.release(); release.acquire(); });
        auto cleanup=qScopeGuard([&] { release.release(); blocker.waitForFinished(); });
        QVERIFY(entered.tryAcquire(1,2000));
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        auto id=h.repo.requestPage(q,1);
        bool responsive=false; QTimer::singleShot(0,&h.repo,[&] { responsive=true; });
        QTRY_VERIFY(responsive);
        QCOMPARE(ready.size(),0); QCOMPARE(a->property("calls").toInt(),0);
        release.release(); QTRY_COMPARE(ready.size(),1);
        QCOMPARE(ready[0][0].toUuid(),id);
    }
    void completedSourceThatClosesBecomesPartialFailure()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home"), b=h.session("office");
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        h.repo.requestPage({},1); QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        emit a->pageReady(a->property("lastRequest").toUuid(),sample("home",{"obsolete"}));
        QVERIFY(h.registry.closeInstance("task5/home"));
        emit b->pageReady(b->property("lastRequest").toUuid(),sample("office",{"valid"}));
        QTRY_COMPARE(ready.size(),1);
        auto p=qvariant_cast<PageResultV2>(ready[0][2]);
        QCOMPARE(p.sections[0].items.size(),1); QCOMPARE(p.sections[0].items[0].ref.entityId,QString("valid"));
        QCOMPARE(p.sourceStates["task5/home"].state,SourcePageLoadStateV2::Failed);
    }
    void sourceDisabledDuringCacheLookupCannotPublishOldPreview()
    {
        PageHarness h; QVERIFY(h.init());
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        QVERIFY(h.cache->store({q,{"task5/home"}},sample("home",{"obsolete"}),QDateTime::currentDateTimeUtc()));
        QSemaphore entered,release;
        auto blocker=QtConcurrent::run(musicCacheIoPool(),[&] { entered.release(); release.acquire(); });
        auto cleanup=qScopeGuard([&] { release.release(); blocker.waitForFinished(); });
        QVERIFY(entered.tryAcquire(1,2000));
        QSignalSpy ready(&h.repo,&PageRepository::pageReady),failed(&h.repo,&PageRepository::pageFailed);
        h.repo.requestPage(q,1);
        QCoreApplication::processEvents(); // begin() has queued lookup behind the barrier
        QVERIFY(h.registry.disableInstance("task5/home"));
        release.release(); QTRY_COMPARE(failed.size(),1);
        QCOMPARE(ready.size(),0);
    }
    void providerCursorContinuesAfterBufferedRowsAreConsumed()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home");
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home"; q.limit=1;
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        h.repo.requestPage(q,1); QTRY_VERIFY(!a->property("lastRequest").toUuid().isNull());
        emit a->pageReady(a->property("lastRequest").toUuid(),sample("home",{"h1","h2"},true));
        QTRY_COMPARE(ready.size(),1);
        q.cursor=qvariant_cast<PageResultV2>(ready.last()[2]).sections[0].nextCursor;
        h.repo.requestPage(q,1); QTRY_COMPARE(ready.size(),2);
        QCOMPARE(a->property("calls").toInt(),1);
        q.cursor=qvariant_cast<PageResultV2>(ready.last()[2]).sections[0].nextCursor;
        h.repo.requestPage(q,1); QTRY_COMPARE(a->property("calls").toInt(),2);
        QCOMPARE(a->property("lastCursor").toString(),QString("private-next"));
        emit a->pageReady(a->property("lastRequest").toUuid(),sample("home",{"h3"}));
        QTRY_COMPARE(ready.size(),3);
        auto p=qvariant_cast<PageResultV2>(ready.last()[2]);
        QCOMPARE(p.sections[0].items[0].ref.entityId,QString("h3")); QVERIFY(!p.sections[0].hasMore);
    }
    void concurrentGroupsMatchProviderIdsAndIgnoreDuplicateTerminals()
    {
        PageHarness h; QVERIFY(h.init()); auto a=h.session("home");
        PageQueryV2 q; q.scope.sourceInstanceId="task5/home";
        QSignalSpy ready(&h.repo,&PageRepository::pageReady);
        const auto first=h.repo.requestPage(q,1); QTRY_COMPARE(a->property("calls").toInt(),1);
        const auto pid1=a->property("lastRequest").toUuid();
        const auto second=h.repo.requestPage(q,2); QTRY_COMPARE(a->property("calls").toInt(),2);
        const auto pid2=a->property("lastRequest").toUuid();
        emit a->pageReady(pid2,sample("home",{"second"}));
        emit a->pageReady(pid2,sample("home",{"duplicate"}));
        emit a->pageReady(pid1,sample("home",{"first"}));
        QTRY_COMPARE(ready.size(),2);
        QCOMPARE(ready[0][0].toUuid(),second); QCOMPARE(ready[1][0].toUuid(),first);
        QCOMPARE(qvariant_cast<PageResultV2>(ready[0][2]).sections[0].items[0].title,QString("second"));
    }
};
QTEST_GUILESS_MAIN(PageRepositoryTest)
#endif
#include "tst_PageRepository.moc"
