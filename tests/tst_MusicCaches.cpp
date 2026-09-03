#include "PageCache.h"
#include "ArtworkCache.h"
#include "MediaAssetRepository.h"
#include "SourceAccountStore.h"
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonDocument>
#include <limits>

class MemorySecrets final : public ISecretStore {
    QHash<QString,QByteArray> values;
public:
    bool write(const QString &key,const QByteArray &value,QString *) override { values.insert(key,value); return true; }
    std::optional<QByteArray> read(const QString &key,QString *) const override { return values.contains(key)?std::optional<QByteArray>(values.value(key)):std::nullopt; }
    bool remove(const QString &key,QString *) override { values.remove(key); return true; }
};
class AssetHarness {
public:
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"),QSettings::IniFormat};
    MemorySecrets secrets;
    SourceAccountStore accounts{&settings,&secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins,&accounts};
    ArtworkCache cache{dir.filePath("art")};
    MediaAssetRepository repo{&registry,&cache};
    MediaRefV2 ref{"task5","task5/home","home",MediaEntityTypeV2::Track,"42"};
    IMusicSourceSessionV2 *session=nullptr;
    QSignalSpy art{&repo,&MediaAssetRepository::artworkReady};
    QSignalSpy lyrics{&repo,&MediaAssetRepository::lyricsReady};
    QSignalSpy failed{&repo,&MediaAssetRepository::failed};
    bool init()
    {
        plugins.addSearchPath(QUEMUSIC_TASK5_PACKAGES);
        if (plugins.discover()!=1 || !plugins.load("org.quemusic.source.task5")
            || !accounts.upsert({"task5","home","Home",{}, {}})) return false;
        session=registry.sessionFor(ref.sourceInstanceId);
        return session!=nullptr;
    }
};
class MusicCachesTest : public QObject {
    Q_OBJECT
private slots:
    // A disk-only lookup fails after the backing file disappears; a memory hit
    // must return the sanitized DTO and recompute staleness for the new time.
    void memoryHitSurvivesMissingDiskAndRecomputesStaleness()
    {
        QTemporaryDir dir; PageCache cache(dir.path()); PageCacheKeyV2 key;
        key.sourceInstanceIds={"home"};
        const auto now=QDateTime::currentDateTimeUtc();
        QVERIFY(cache.store(key,{},now));
        QVERIFY(QFile::remove(cache.filePath(key)));
        auto hit=cache.lookup(key,now.addSecs(301),std::chrono::minutes(5));
        QVERIFY(hit); QVERIFY(hit->stale); QVERIFY(hit->cached);
        QVERIFY(hit->page.cached); QVERIFY(!hit->page.complete);
    }
    void memoryLruPromotesHitsAndEvictsLeastRecentlyUsed()
    {
        QTemporaryDir dir; PageCache cache(dir.path());
        const auto now=QDateTime::currentDateTimeUtc();
        QList<PageCacheKeyV2> keys;
        // The cache contract bounds the working set to 64 pages. Deleting disk
        // copies makes retention/eviction observable without cache test hooks.
        for (int i=0;i<64;++i) {
            PageCacheKeyV2 k; k.query.searchText=QString::number(i); keys.append(k);
            QVERIFY(cache.store(k,{},now)); QVERIFY(QFile::remove(cache.filePath(k)));
        }
        QVERIFY(cache.lookup(keys[0],now,std::chrono::minutes(5))); // promote oldest
        PageCacheKeyV2 newest; newest.query.searchText="newest";
        QVERIFY(cache.store(newest,{},now));
        QVERIFY(!cache.lookup(keys[1],now,std::chrono::minutes(5)));
        QVERIFY(cache.lookup(keys[0],now,std::chrono::minutes(5)));
        QVERIFY(cache.lookup(keys[2],now,std::chrono::minutes(5)));
    }
    void diskFallbackPopulatesMemoryAndSourceInvalidationPurgesIt()
    {
        QTemporaryDir dir;
        const auto now=QDateTime::currentDateTimeUtc();
        PageCacheKeyV2 mixed, office; mixed.sourceInstanceIds={"home","office"}; office.sourceInstanceIds={"office"};
        { PageCache writer(dir.path()); QVERIFY(writer.store(mixed,{},now)); QVERIFY(writer.store(office,{},now)); }
        PageCache cache(dir.path());
        QVERIFY(cache.lookup(mixed,now,std::chrono::minutes(5)));
        QVERIFY(cache.lookup(office,now,std::chrono::minutes(5)));
        QVERIFY(QFile::remove(cache.filePath(mixed))); QVERIFY(QFile::remove(cache.filePath(office)));
        QVERIFY(cache.lookup(mixed,now,std::chrono::minutes(5)));
        PageCache invalidator(dir.path()); invalidator.invalidateSource("home");
        QVERIFY(!cache.lookup(mixed,now,std::chrono::minutes(5)));
        QVERIFY(cache.lookup(office,now,std::chrono::minutes(5)));
    }
    void safeActionConstraintsSurviveLiveDiskAndMemory()
    {
        QTemporaryDir dir; PageCache cache(dir.path()); PageCacheKeyV2 key;
        MediaItemV2 item;
        item.availableActions[SourceActionV2::Play]={AvailabilityV2::Available,"",{{"sameSourceOnly",true},{"maxBitrate",192.5}}};
        PageSectionV2 section; section.items={item}; PageResultV2 page{{section},{},false,true};
        auto check=[](const PageResultV2 &p) {
            QCOMPARE(p.sections.size(),1); QCOMPARE(p.sections[0].items.size(),1);
            const auto a=p.sections[0].items[0].availableActions.value(SourceActionV2::Play);
            QCOMPARE(a.state,AvailabilityV2::Available);
            QCOMPARE(a.constraints.value("sameSourceOnly").metaType().id(),QMetaType::Bool);
            QCOMPARE(a.constraints.value("sameSourceOnly").toBool(),true);
            QCOMPARE(a.constraints.value("maxBitrate").toDouble(),192.5);
        };
        check(PageCache::sanitized(page));
        const auto now=QDateTime::currentDateTimeUtc(); QVERIFY(cache.store(key,page,now));
        auto memory=cache.lookup(key,now,std::chrono::minutes(5)); QVERIFY(memory); check(memory->page);
        PageCache diskReader(dir.path()); auto disk=diskReader.lookup(key,now,std::chrono::minutes(5));
        QVERIFY(disk); check(disk->page);
        QVERIFY(QFile::remove(cache.filePath(key)));
        auto hydratedMemory=diskReader.lookup(key,now,std::chrono::minutes(5)); QVERIFY(hydratedMemory); check(hydratedMemory->page);
    }
    void unrepresentableActionConstraintsFailClosed_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("unknown-header")<<0;
        QTest::newRow("boolean-as-string")<<1;
        QTest::newRow("bitrate-as-string")<<2;
        QTest::newRow("qobject-under-known-key")<<3;
        QTest::newRow("nonfinite-number")<<4;
        QTest::newRow("negative-bitrate")<<5;
        QTest::newRow("nested-map")<<6;
    }
    void unrepresentableActionConstraintsFailClosed()
    {
        QFETCH(int,mode); QObject object;
        QVariantMap constraints{{"sameSourceOnly",true},{"maxBitrate",192}};
        if (mode==0) constraints.insert("Authorization",QString("forbidden-constraint-data"));
        if (mode==1) constraints["sameSourceOnly"]=QString("true");
        if (mode==2) constraints["maxBitrate"]=QString("192");
        if (mode==3) constraints["maxBitrate"]=QVariant::fromValue(&object);
        if (mode==4) constraints["maxBitrate"]=std::numeric_limits<double>::infinity();
        if (mode==5) constraints["maxBitrate"]=-1;
        if (mode==6) constraints["maxBitrate"]=QVariantMap{{"token",QString("forbidden-constraint-data")}};
        MediaItemV2 item; item.availableActions[SourceActionV2::Play]={AvailabilityV2::Available,"provider.reason",constraints};
        PageSectionV2 section; section.items={item}; PageResultV2 page{{section},{},false,true};
        auto check=[](const PageResultV2 &p) {
            QCOMPARE(p.sections.size(),1); QCOMPARE(p.sections[0].items.size(),1);
            const auto a=p.sections[0].items[0].availableActions.value(SourceActionV2::Play);
            QCOMPARE(a.state,AvailabilityV2::Unavailable);
            QVERIFY(!a.reasonKey.isEmpty()); QVERIFY(a.reasonKey!="provider.reason");
            QVERIFY(!a.constraints.contains("Authorization"));
        };
        check(PageCache::sanitized(page));
        QTemporaryDir dir; PageCache cache(dir.path()); PageCacheKeyV2 key;
        const auto now=QDateTime::currentDateTimeUtc(); QVERIFY(cache.store(key,page,now));
        auto memory=cache.lookup(key,now,std::chrono::minutes(5)); QVERIFY(memory); check(memory->page);
        PageCache disk(dir.path()); auto roundtrip=disk.lookup(key,now,std::chrono::minutes(5)); QVERIFY(roundtrip); check(roundtrip->page);
        QFile file(cache.filePath(key)); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll();
        QVERIFY(!bytes.contains("forbidden-constraint-data")); QVERIFY(!bytes.contains("Authorization")); QVERIFY(!bytes.contains("QObject"));
    }
    void legacyCacheWithoutConstraintProvenanceIsAMiss()
    {
        QTemporaryDir dir; PageCacheKeyV2 key; const auto now=QDateTime::currentDateTimeUtc();
        QString path;
        { PageCache writer(dir.path()); QVERIFY(writer.store(key,{},now)); path=writer.filePath(key); }
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); auto object=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        object["version"]=1; // Old schema dropped restrictions, so Available is unsafe.
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(object).toJson()); file.close();
        PageCache reader(dir.path()); QVERIFY(!reader.lookup(key,now,std::chrono::minutes(5)));
    }
    void pageCacheAllowlistStalenessAndInvalidation()
    {
        QTemporaryDir dir; PageCache cache(dir.path());
        PageCacheKeyV2 key; key.sourceInstanceIds={"task5/home"};
        PageSectionV2 s; s.sectionId="recent"; s.nextCursor="private-provider-cursor"; s.hasMore=true;
        MediaItemV2 i; i.ref={"task5","task5/home","home",MediaEntityTypeV2::Track,"42"};
        QObject object;
        i.metadata={{"genre",QString("Jazz")},{"streamUrl",QString("https://secret/url")},
                    {"password",QString("forbidden-value")},{"object",QVariant::fromValue(&object)},
                    {"year",QVariant::fromValue(&object)}};
        i.availableActions[SourceActionV2::Play]={AvailabilityV2::Available,"",{{"Authorization","secret-header"}}};
        s.items={i}; PageResultV2 p{{s},{},false,true};
        QVERIFY(cache.store(key,p,QDateTime::currentDateTimeUtc().addSecs(-3600)));
        QFile file(cache.filePath(key)); QVERIFY(file.open(QIODevice::ReadOnly)); auto bytes=file.readAll();
        for (const auto &secret : {"streamUrl","password","forbidden-value","secret-header","private-provider-cursor","object"}) QVERIFY(!bytes.contains(secret));
        QVERIFY(bytes.contains("Jazz"));
        auto value=cache.lookup(key,QDateTime::currentDateTimeUtc(),std::chrono::minutes(5));
        QVERIFY(value); QVERIFY(value->cached); QVERIFY(value->stale);
        QCOMPARE(value->page.sections[0].items[0].metadata.value("genre").toString(),QString("Jazz"));
        QVERIFY(value->page.sections[0].nextCursor.isEmpty());
        PageCache reopened(dir.path()); reopened.invalidateSource("task5/home");
        QVERIFY(!cache.lookup(key,QDateTime::currentDateTimeUtc(),std::chrono::minutes(5)));
    }
    void pageCacheKeysCoverEveryQueryDimension()
    {
        QTemporaryDir dir; PageCache c(dir.path()); PageCacheKeyV2 base; base.sourceInstanceIds={"a"};
        QSet<QString> paths{c.filePath(base)};
        auto k=base; k.query.page=MusicPageKindV2::Search; paths.insert(c.filePath(k));
        k=base; k.query.section=PageSectionKindV2::Albums; paths.insert(c.filePath(k));
        k=base; k.query.scope.sourceInstanceId="a"; paths.insert(c.filePath(k));
        k=base; k.query.searchText="jazz"; paths.insert(c.filePath(k));
        k=base; k.query.filters={{"genre","jazz"}}; paths.insert(c.filePath(k));
        k=base; k.query.cursor="opaque"; paths.insert(c.filePath(k));
        k=base; k.query.limit=1; paths.insert(c.filePath(k));
        k=base; k.sourceInstanceIds={"b"}; paths.insert(c.filePath(k));
        QCOMPARE(paths.size(),9);
    }
    void artworkStoresBytesLocallyWithSafePaths()
    {
        QTemporaryDir dir; ArtworkCache c(dir.path());
        auto url=c.store("../../source","../../42",QByteArray("image-bytes"),"image/jpeg");
        QVERIFY(url.isLocalFile()); QVERIFY(url.toLocalFile().startsWith(dir.path()+"/"));
        QCOMPARE(c.lookup("../../source","../../42").value(),url);
        QVERIFY(!c.lookup("other","../../42"));
        QVERIFY(c.store("a","42","html","text/html").isEmpty());
        QFile f(url.toLocalFile()); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("image-bytes"));
    }
    void artworkRepositoryReturnsLocalCacheAfterInlineProvider()
    {
        AssetHarness h; QVERIFY(h.init()); h.session->setProperty("inline",true);
        auto first=h.repo.requestArtwork(h.ref); QCOMPARE(h.art.size(),0); QTRY_COMPARE(h.art.size(),1);
        QCOMPARE(h.art[0][0].toUuid(),first); QVERIFY(h.art[0][2].toUrl().isLocalFile());
        h.repo.requestArtwork(h.ref); QCOMPARE(h.art.size(),1); QTRY_COMPARE(h.art.size(),2);
        QCOMPARE(h.session->property("assetCalls").toInt(),1);
    }
    void lyricsStayInMemoryAndReturnAfterRequestId()
    {
        AssetHarness h; QVERIFY(h.init()); h.session->setProperty("inline",true);
        auto id=h.repo.requestLyrics(h.ref); QCOMPARE(h.lyrics.size(),0); QTRY_COMPARE(h.lyrics.size(),1);
        QCOMPARE(h.lyrics[0][0].toUuid(),id); QCOMPARE(h.lyrics[0][2].toString(),QString("[00:01]Words"));
        h.repo.requestLyrics(h.ref); QCOMPARE(h.lyrics.size(),1); QTRY_COMPARE(h.lyrics.size(),2);
        QCOMPARE(h.session->property("assetCalls").toInt(),1);
        MediaAssetRepository other(&h.registry,&h.cache);
        QSignalSpy fresh(&other,&MediaAssetRepository::lyricsReady);
        other.requestLyrics(h.ref); QTRY_COMPARE(fresh.size(),1);
        QCOMPARE(h.session->property("assetCalls").toInt(),2);
    }
    void artworkRejectsMismatchedOrUntypedPayload_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("wrong-action")<<0;
        QTest::newRow("wrong-subject")<<1;
        QTest::newRow("url-only")<<2;
        QTest::newRow("string-not-bytes")<<3;
        QTest::newRow("bytes-not-mime")<<4;
    }
    void artworkRejectsMismatchedOrUntypedPayload()
    {
        QFETCH(int,mode);
        AssetHarness h; QVERIFY(h.init());
        auto request=h.repo.requestArtwork(h.ref); QTRY_COMPARE(h.session->property("assetCalls").toInt(),1);
        ActionResultV2 result{SourceActionV2::Artwork,h.ref,{{"bytes",QByteArray("image")},{"mimeType",QString("image/png")}}};
        if (mode==0) result.action=SourceActionV2::Lyrics;
        if (mode==1) result.subject.entityId="elsewhere";
        if (mode==2) result.payload={{"url","https://private"}};
        if (mode==3) result.payload["bytes"]=QString("not-a-byte-array");
        if (mode==4) result.payload["mimeType"]=QByteArray("image/png");
        emit h.session->actionCompleted(h.session->property("lastRequest").toUuid(),result);
        QTRY_COMPARE(h.failed.size(),1); QCOMPARE(h.failed[0][0].toUuid(),request);
        QCOMPARE(qvariant_cast<SourceErrorV2>(h.failed[0][1]).kind,SourceErrorKindV2::InvalidRequest);
        QCOMPARE(h.art.size(),0);
    }
    void cancelledAssetDropsLateCompletion()
    {
        AssetHarness h; QVERIFY(h.init());
        auto id=h.repo.requestArtwork(h.ref); QTRY_COMPARE(h.session->property("assetCalls").toInt(),1);
        auto pending=h.session->property("lastRequest").toUuid(); h.repo.cancel(id);
        QCOMPARE(h.session->property("cancelled").toUuid(),pending);
        emit h.session->actionCompleted(pending,{SourceActionV2::Artwork,h.ref,{{"bytes",QByteArray("late")},{"mimeType",QString("image/png")}}});
        QTest::qWait(20); QCOMPARE(h.art.size(),0); QCOMPARE(h.failed.size(),0);
    }
    void queuedAssetFailureRemainsCancellable()
    {
        AssetHarness h; QVERIFY(h.init());
        auto id=h.repo.requestArtwork(h.ref); QTRY_COMPARE(h.session->property("assetCalls").toInt(),1);
        emit h.session->requestFailed(h.session->property("lastRequest").toUuid(),{SourceErrorKindV2::Network});
        h.repo.cancel(id); QTest::qWait(20); QCOMPARE(h.failed.size(),0);
    }
    void closingSessionAfterLyricsTerminalDiscardsQueuedSuccess()
    {
        AssetHarness h; QVERIFY(h.init());
        h.repo.requestLyrics(h.ref); QTRY_COMPARE(h.session->property("assetCalls").toInt(),1);
        emit h.session->actionCompleted(h.session->property("lastRequest").toUuid(),{SourceActionV2::Lyrics,h.ref,{{"lyrics",QString("obsolete")}}});
        QVERIFY(h.registry.closeInstance("task5/home"));
        QTest::qWait(30); QCOMPARE(h.lyrics.size(),0); QCOMPARE(h.failed.size(),1);
        QCOMPARE(h.plugins.unload("org.quemusic.source.task5"),PluginOperationResult::Success);
    }
};
QTEST_GUILESS_MAIN(MusicCachesTest)
#include "tst_MusicCaches.moc"
