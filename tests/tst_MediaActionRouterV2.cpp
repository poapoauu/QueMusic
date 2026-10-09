#if defined(QUEMUSIC_ACTION_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QPointer>

class ActionSessionBase : public IMusicSourceSessionV2 {
    Q_OBJECT
public:
    SourceConfigurationV2 config;
    SourceSessionStateV2 current = SourceSessionStateV2::Ready;
    ActionSessionBase(SourceConfigurationV2 c, QObject *parent) : IMusicSourceSessionV2(parent), config(c) {}
    SourceIdentityV2 identity() const override
    {
        return {"task6", config.sourceInstanceId, property("wrongIdentity").toBool() ? "wrong" : config.accountId, config.displayName};
    }
    SourceSessionStateV2 state() const override { return current; }
    CapabilitySetV2 capabilities() const override
    {
        CapabilitySetV2 result;
        for (int n=0; n<=int(SourceActionV2::DeleteBookmark); ++n) {
            const auto action=SourceActionV2(n);
            result.serverActions[action]={AvailabilityV2::Available, {}, property("serverConstraints").toMap()};
            if (!property("missingAccount").toBool())
                result.accountActions[action]={AvailabilityV2::Available, {}, property("accountConstraints").toMap()};
        }
        if (property("forbidden").isValid()) result.accountActions[SourceActionV2(property("forbidden").toInt())]={AvailabilityV2::Forbidden,"private.permission",{}};
        if (property("unavailable").isValid()) result.serverActions[SourceActionV2(property("unavailable").toInt())]={AvailabilityV2::Unavailable,"private.server",{}};
        const bool revoke=property("revokeDuringRead").toBool();
        QPointer<ActionSessionBase> guard(const_cast<ActionSessionBase *>(this));
        emit const_cast<ActionSessionBase *>(this)->capabilitiesRead();
        if (guard && revoke) emit guard->capabilitiesChanged({});
        return result;
    }
    QUuid open() override { const auto id=QUuid::createUuid(); emit requestStarted(id); emit actionCompleted(id,{}); return id; }
    void close() override { current=SourceSessionStateV2::Closing; emit stateChanged(current); }
    void cancel(const QUuid &id) override {
        setProperty("cancelled",id); setProperty("cancelCount",property("cancelCount").toInt()+1);
        emit cancelObserved();
    }
signals:
    void capabilitiesRead();
    void cancelObserved();
};
class ActionSession : public ActionSessionBase, public IFavoriteProviderV2,
    public IRatingProviderV2, public IDownloadProviderV2, public IPlaylistProviderV2,
    public IBookmarkProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IFavoriteProviderV2 IRatingProviderV2 IDownloadProviderV2 IPlaylistProviderV2 IBookmarkProviderV2)
public:
    using ActionSessionBase::ActionSessionBase;
    QUuid perform(SourceActionV2 action, const MediaRefV2 &subject, QVariantMap payload)
    {
        const auto id=QUuid::createUuid();
        const bool inlineResult=!property("async").toBool(), failure=property("failure").toBool();
        const bool mismatch=property("mismatchId").toBool();
        ActionResultV2 result{action,subject,payload};
        if (property("wrongAction").toBool()) result.action=SourceActionV2::Play;
        if (property("wrongSubject").toBool()) result.subject.accountId="office";
        if (property("wrongValue").toBool()) result.payload={{"favorite",QString("true")}};
        if (property("taintedPayload").toBool()) result.payload.insert("credentials",QVariant::fromValue(this));
        setProperty("calls",property("calls").toInt()+1);
        setProperty("lastRequest",id); setProperty("action",int(action));
        setProperty("subject",mediaRefV2ToVariantMap(subject)); setProperty("arguments",payload);
        QPointer<ActionSession> guard(this);
        if (!property("skipStarted").toBool()) emit requestStarted(id);
        if (guard && inlineResult) {
            if (failure) emit requestFailed(id,{SourceErrorKindV2::Authorization,"secret","credential",{},false});
            else emit actionCompleted(id,result);
        }
        return mismatch ? QUuid::createUuid() : id;
    }
    QUuid setFavorite(const MediaRefV2 &m,bool f) override { return perform(f?SourceActionV2::Favorite:SourceActionV2::Unfavorite,m,{{"favorite",f}}); }
    QUuid setRating(const MediaRefV2 &m,int r) override { return perform(SourceActionV2::Rating,m,{{"rating",r}}); }
    QUuid download(const MediaRefV2 &m,const QUrl &d) override { return perform(SourceActionV2::Download,m,{{"destination",d}}); }
    QUuid createPlaylist(const QString &name,const QList<MediaRefV2> &tracks) override
    {
        setProperty("trackCount",tracks.size());
        return perform(SourceActionV2::CreatePlaylist,{"task6",config.sourceInstanceId,config.accountId,MediaEntityTypeV2::Playlist,"created"},{{"name",name}});
    }
    QUuid updatePlaylist(const MediaRefV2 &m,const PlaylistChangeV2 &c) override
    {
        QVariantList added,removed;
        for (const auto &t:c.tracksToAdd) added.append(mediaRefV2ToVariantMap(t));
        for (int i:c.trackIndexesToRemove) removed.append(i);
        return perform(SourceActionV2::UpdatePlaylist,m,{{"name",c.newName},{"tracksToAdd",added},{"trackIndexesToRemove",removed}});
    }
    QUuid deletePlaylist(const MediaRefV2 &m) override { return perform(SourceActionV2::DeletePlaylist,m,{}); }
    QUuid fetchBookmarks() override { return {}; }
    QUuid createBookmark(const MediaRefV2 &m,qint64 p,const QString &comment) override
    {
        setProperty("comment",comment); return perform(SourceActionV2::CreateBookmark,m,{{"positionMs",p}});
    }
    QUuid deleteBookmark(const MediaRefV2 &) override { return {}; }
};
class ActionPlugin : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override
    {
        SourceDescriptorV2 result{"org.quemusic.source.task6","task6","Task6","2.0.0",2,{}};
        for (int n=0; n<=int(SourceActionV2::DeleteBookmark); ++n)
            result.declaredActions[SourceActionV2(n)]={AvailabilityV2::Available,{},property("constraints").toMap()};
        if (property("omit").isValid()) result.declaredActions.remove(SourceActionV2(property("omit").toInt()));
        emit const_cast<ActionPlugin *>(this)->descriptorRead();
        return result;
    }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &c,QObject *parent) override
    {
        if (c.accountId=="bare") return new ActionSessionBase(c,parent);
        return new ActionSession(c,parent);
    }
signals:
    void descriptorRead();
};
#else
#include "MediaActionRouter.h"
#include "MusicPageModel.h"
#include "SourceAccountStore.h"
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>

class ActionHook : public QObject {
    Q_OBJECT
public:
    std::function<void()> callback;
public slots:
    void run() { callback(); }
};

class ActionSecrets final : public ISecretStore {
public:
    bool write(const QString &,const QByteArray &,QString *) override { return true; }
    std::optional<QByteArray> read(const QString &,QString *) const override { return QByteArray("secret"); }
    bool remove(const QString &,QString *) override { return true; }
};
class ActionHarness {
public:
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"),QSettings::IniFormat};
    ActionSecrets secrets;
    SourceAccountStore accounts{&settings,&secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins,&accounts};
    MediaActionRouter router{&registry};
    bool init(bool isolatePackage = false)
    {
        QString packages = QUEMUSIC_TASK6_PACKAGES;
        if (isolatePackage) {
            // External session destruction pins this exact loaded library path
            // until process exit. Isolate it without making tests order-dependent.
            packages = dir.filePath("packages");
            const QString target = packages + "/task6";
            if (!QDir().mkpath(target)) return false;
            const QDir source(QString(QUEMUSIC_TASK6_PACKAGES) + "/task6");
            for (const auto &file : source.entryList(QDir::Files))
                if (!QFile::copy(source.filePath(file),target + '/' + file)) return false;
        }
        plugins.addSearchPath(packages);
        return plugins.discover()==1 && plugins.load("org.quemusic.source.task6")
            && accounts.saveResolvedV2({"task6","home","Home",{},{}})
            && accounts.saveResolvedV2({"task6","office","Office",{},{}})
            && accounts.saveResolvedV2({"task6","bare","Bare",{},{}});
    }
    IMusicSourceSessionV2 *session(QString account="home") { return registry.sessionFor("task6/"+account); }
    QObject *root() { return plugins.pluginInstance("org.quemusic.source.task6"); }
};
static QVariantMap item(QString account="home", MediaEntityTypeV2 type=MediaEntityTypeV2::Track, QString id="42")
{
    MediaItemV2 media; media.ref={"task6","task6/"+account,account,type,id};
    media.title="Actual page item"; media.durationMs=10000;
    for (int n=0;n<=int(SourceActionV2::DeleteBookmark);++n)
        media.availableActions[SourceActionV2(n)]={AvailabilityV2::Available,{},{}};
    PageSectionV2 section; section.sectionId="tracks"; section.items={media};
    MusicPageModel model(MusicPageKindV2::Recommendation);
    model.applyResult(model.beginRequest(),{{section},{},false,true});
    return model.itemAt(0,0);
}
static QVariantMap withAction(QVariantMap media,SourceActionV2 action,QVariant value)
{
    auto actions=media.value("availableActions").toMap();
    if (value.isValid()) actions[QString::number(int(action))]=value;
    else actions.remove(QString::number(int(action)));
    media["availableActions"]=actions; return media;
}
class MediaActionRouterV2Test : public QObject {
    Q_OBJECT
private slots:
    void downloadCancellationOwnsTheRequestAndDefersLeaseProtectedCleanup()
    {
        ActionHarness h; QVERIFY(h.init()); auto *home = h.session(); auto *office = h.session("office");
        home->setProperty("async", true); office->setProperty("async", true);
        QSignalSpy failed(&h.router, &MediaActionRouter::actionFailed), done(&h.router, &MediaActionRouter::actionSucceeded);
        const auto target = QUrl::fromLocalFile(h.dir.filePath("A.bin"));
        const auto a = h.router.download(item(), target); const auto providerA = home->property("lastRequest").toUuid();
        const auto b = h.router.download(item("office"), QUrl::fromLocalFile(h.dir.filePath("B.bin")));
        const auto providerB = office->property("lastRequest").toUuid();
        const auto favorite = h.router.setFavorite(item(), true);
        QVERIFY(!h.router.cancelDownload({})); QVERIFY(!h.router.cancelDownload(QUuid::createUuid()));
        QVERIFY(!h.router.cancelDownload(providerA)); QVERIFY(!h.router.cancelDownload(favorite));
        MediaActionRouter other(&h.registry); QVERIFY(!other.cancelDownload(a));
        PluginOperationResult unload = PluginOperationResult::Success;
        ActionHook hook; hook.callback = [&] {
            unload = h.plugins.unload("org.quemusic.source.task6");
            // A provider terminal emitted synchronously by cancel cannot resurrect success.
            emit home->actionCompleted(providerA, {SourceActionV2::Download,
                mediaRefV2FromVariantMap(item().value("ref").toMap()), {{"destination", target}}});
        };
        connect(home, SIGNAL(cancelObserved()), &hook, SLOT(run()));
        QVERIFY(h.router.cancelDownload(a)); QVERIFY(!h.router.cancelDownload(a));
        QCOMPARE(home->property("cancelCount").toInt(), 0); QCOMPARE(failed.size(), 0);
        QTRY_COMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), a);
        QCOMPARE(failed[0][1].toMap().value("messageKey").toString(), QString("music.actionCancelled"));
        QCOMPARE(home->property("cancelled").toUuid(), providerA); QCOMPARE(home->property("cancelCount").toInt(), 1);
        QCOMPARE(unload, PluginOperationResult::Busy); QCOMPARE(done.size(), 0);
        QVERIFY(!h.router.cancelDownload(a)); QCOMPARE(office->property("cancelCount").toInt(), 0);
        emit office->actionCompleted(providerB, {SourceActionV2::Download,
            mediaRefV2FromVariantMap(item("office").value("ref").toMap()),
            {{"destination", QUrl::fromLocalFile(h.dir.filePath("B.bin"))}}});
        QTRY_COMPARE(done.size(), 1); QCOMPARE(done[0][0].toUuid(), b);
        QVERIFY(!h.router.cancelDownload(b));
    }
    void settledDownloadCannotBeCancelledAndCancelCallbackMayDestroyRouter()
    {
        ActionHarness h; QVERIFY(h.init()); auto *session = h.session();
        QSignalSpy done(&h.router, &MediaActionRouter::actionSucceeded);
        const auto inlineId = h.router.download(item(), QUrl::fromLocalFile(h.dir.filePath("inline.bin")));
        QVERIFY(!h.router.cancelDownload(inlineId)); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(session->property("cancelCount").toInt(), 0);
        session->setProperty("async", true);
        QPointer<MediaActionRouter> router = new MediaActionRouter(&h.registry);
        const auto id = router->download(item(), QUrl::fromLocalFile(h.dir.filePath("pending.bin")));
        const auto provider = session->property("lastRequest").toUuid();
        ActionHook hook; hook.callback = [&] { delete router.data(); };
        connect(session, SIGNAL(cancelObserved()), &hook, SLOT(run()));
        QVERIFY(router->cancelDownload(id)); QTRY_VERIFY(!router);
        QCOMPARE(session->property("cancelled").toUuid(), provider); QCOMPARE(session->property("cancelCount").toInt(), 1);
    }
    void lifecycleFailureWinsOverQueuedDownloadCancellation()
    {
        ActionHarness h; QVERIFY(h.init()); auto *session = h.session(); session->setProperty("async", true);
        QSignalSpy failed(&h.router, &MediaActionRouter::actionFailed), done(&h.router, &MediaActionRouter::actionSucceeded);
        const auto id = h.router.download(item(), QUrl::fromLocalFile(h.dir.filePath("closing.bin")));
        QVERIFY(h.router.cancelDownload(id)); QVERIFY(h.registry.closeInstance("task6/home"));
        QVERIFY(!h.router.cancelDownload(id)); QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed[0][0].toUuid(), id); QCOMPARE(done.size(), 0);
        QCOMPARE(failed[0][1].toMap().value("messageKey").toString(), QString("music.actionNotAvailable"));
    }
    void favoriteRoutesToOwningInstanceAndDefersCompletion()
    {
        ActionHarness h; QVERIFY(h.init()); auto home=h.session(),office=h.session("office");
        QSignalSpy done(&h.router,&MediaActionRouter::actionSucceeded);
        auto id=h.router.setFavorite(item(),true);
        QVERIFY(!id.isNull()); QCOMPARE(home->property("calls").toInt(),1);
        QCOMPARE(office->property("calls").toInt(),0); QCOMPARE(done.size(),0);
        QTRY_COMPARE(done.size(),1); QCOMPARE(done[0][0].toUuid(),id);
        const auto result=done[0][1].toMap();
        QCOMPARE(result.value("action").toInt(),int(SourceActionV2::Favorite));
        QCOMPARE(result.value("subject").toMap(),item().value("ref").toMap());
        QCOMPARE(result.value("favorite"),QVariant(true));
        QCOMPARE(result.size(),3);
    }
    void typedMethodsCarryValidatedArguments()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session();
        QSignalSpy done(&h.router,&MediaActionRouter::actionSucceeded),failed(&h.router,&MediaActionRouter::actionFailed);
        const auto playlist=item("home",MediaEntityTypeV2::Playlist,"p");
        h.router.setFavorite(item(),false); QCOMPARE(s->property("action").toInt(),int(SourceActionV2::Unfavorite));
        h.router.setRating(item(),4); QCOMPARE(s->property("arguments").toMap().value("rating").toInt(),4);
        const QUrl destination=QUrl::fromLocalFile(h.dir.filePath("song.mp3"));
        h.router.download(item(),destination); QCOMPARE(s->property("arguments").toMap().value("destination").toUrl(),destination);
        h.router.createPlaylist("task6/home","New"); QCOMPARE(s->property("trackCount").toInt(),0);
        h.router.updatePlaylist(playlist,{{"newName","Renamed"},{"tracksToAdd",QVariantList{item()}},{"trackIndexesToRemove",QVariantList{0,2}}});
        QCOMPARE(s->property("arguments").toMap().value("tracksToAdd").toList()[0].toMap(),item().value("ref").toMap());
        h.router.deletePlaylist(playlist); QCOMPARE(s->property("subject").toMap(),playlist.value("ref").toMap());
        h.router.setBookmark(item(),1000); QCOMPARE(s->property("arguments").toMap().value("positionMs").toLongLong(),1000);
        QTRY_COMPARE(done.size()+failed.size(),7); QCOMPARE(failed.size(),0);
        QCOMPARE(done[3][1].toMap().value("subject").toMap().value("entityId").toString(),QString("created"));
        QVERIFY(!done[4][1].toMap().contains("tracksToAdd")); // no arbitrary result passthrough
    }
    void livePermissionsAndMissingProviderNeverDispatch()
    {
        ActionHarness h; QVERIFY(h.init()); auto home=h.session(),office=h.session("office"),bare=h.session("bare");
        const auto stale=item(); QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        home->setProperty("forbidden",int(SourceActionV2::Favorite));
        h.router.setFavorite(stale,true); h.router.setFavorite(item("office"),true);
        home->setProperty("forbidden",QVariant{}); home->setProperty("missingAccount",true);
        h.router.setFavorite(stale,true);
        home->setProperty("missingAccount",false); home->setProperty("unavailable",int(SourceActionV2::Favorite));
        h.router.setFavorite(stale,true);
        h.router.download(item("bare"),QUrl::fromLocalFile(h.dir.filePath("song")));
        h.router.download(withAction(item(),SourceActionV2::Download,{}),QUrl::fromLocalFile(h.dir.filePath("song")));
        h.root()->setProperty("omit",int(SourceActionV2::Rating)); h.router.setRating(item(),3);
        QCOMPARE(failed.size(),0); QTRY_COMPARE(failed.size(),6);
        QCOMPARE(home->property("calls").toInt(),0); QCOMPARE(bare->property("calls").toInt(),0);
        QCOMPARE(office->property("calls").toInt(),1);
        QCOMPARE(failed[0][1].toMap().value("state").toInt(),int(AvailabilityV2::Forbidden));
        QCOMPARE(failed[3][1].toMap().value("state").toInt(),int(AvailabilityV2::Unsupported));
    }
    void malformedFullItemsAndInputDomainsFailBeforeProvider()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); QObject object;
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        QList<QVariantMap> invalid={item().value("ref").toMap(),{}};
        auto wrong=item(); auto ref=wrong.value("ref").toMap(); ref["accountId"]="office"; wrong["ref"]=ref; invalid<<wrong;
        wrong=item(); ref=wrong.value("ref").toMap(); ref["entityType"]="0"; wrong["ref"]=ref; invalid<<wrong;
        invalid<<withAction(item(),SourceActionV2::Favorite,QVariantMap{{"state","Available"},{"constraints",QVariantMap{}}});
        invalid<<withAction(item(),SourceActionV2::Favorite,QVariantMap{{"state",1},{"constraints",QVariantMap{{"sameSourceOnly",QVariant::fromValue(&object)}}}});
        for (auto media:invalid) h.router.setFavorite(media,true);
        h.router.setRating(item(),-1); h.router.setRating(item(),6);
        h.router.setBookmark(item(),-1);
        h.router.download(item(),QUrl("https://user:secret@example.org/file"));
        h.router.createPlaylist("task6/home","  "); h.router.deletePlaylist(item());
        QTRY_COMPARE(failed.size(),invalid.size()+6); QCOMPARE(s->property("calls").toInt(),0);
    }
    void constraintsInEveryLiveLayerFailSafely()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); QObject object;
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        for (auto property:{"serverConstraints","accountConstraints"}) {
            s->setProperty(property,QVariantMap{{"sameSourceOnly",QVariant::fromValue(&object)}});
            h.router.setFavorite(item(),true); s->setProperty(property,QVariant{});
        }
        h.root()->setProperty("constraints",QVariantMap{{"password","private"}});
        h.router.setFavorite(item(),true); h.root()->setProperty("constraints",QVariant{});
        s->setProperty("serverConstraints",QVariantMap{{"maxBitrate",128}});
        h.router.download(item(),QUrl::fromLocalFile(h.dir.filePath("song")));
        QTRY_COMPARE(failed.size(),4); QCOMPARE(s->property("calls").toInt(),0);
        for (auto failure:failed) QVERIFY(!failure[1].toMap().contains("constraints"));
    }
    void compoundPlaylistValidatesAllSubactionsAndTracks()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session();
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        auto playlist=item("home",MediaEntityTypeV2::Playlist,"p");
        h.router.updatePlaylist(playlist,{{"tracksToAdd",QVariantList{item("office")}}});
        h.router.updatePlaylist(playlist,{{"tracksToAdd",QVariantList{item("home",MediaEntityTypeV2::Album)}}});
        h.router.updatePlaylist(playlist,{{"trackIndexesToRemove",QVariantList{-1}}});
        h.router.updatePlaylist(playlist,{{"trackIndexesToRemove",QVariantList{1,1}}});
        h.router.updatePlaylist(playlist,{{"trackIndexesToRemove",QVariantList{"0"}}});
        h.router.updatePlaylist(playlist,{{"unknown",true}});
        h.router.updatePlaylist(playlist,{});
        h.router.updatePlaylist(playlist,{{"tracksToAdd",QVariantList{item().value("ref")}}});
        for (auto action:{SourceActionV2::UpdatePlaylist,SourceActionV2::AddPlaylistTracks,SourceActionV2::RemovePlaylistTracks}) {
            s->setProperty("forbidden",int(action));
            h.router.updatePlaylist(playlist,{{"newName","x"},{"tracksToAdd",QVariantList{item()}},{"trackIndexesToRemove",QVariantList{0}}});
        }
        s->setProperty("forbidden",QVariant{});
        h.router.updatePlaylist(playlist,{{"tracksToAdd",QVariantList{withAction(item(),SourceActionV2::AddPlaylistTracks,{})}}});
        QTRY_COMPARE(failed.size(),12); QCOMPARE(s->property("calls").toInt(),0);
    }
    void wrongTerminalsAreRejectedAndPayloadIsWhitelisted()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session();
        QSignalSpy done(&h.router,&MediaActionRouter::actionSucceeded),failed(&h.router,&MediaActionRouter::actionFailed);
        for (auto property:{"wrongAction","wrongSubject","wrongValue","mismatchId","failure"}) {
            s->setProperty(property,true); h.router.setFavorite(item(),true); s->setProperty(property,false);
        }
        s->setProperty("taintedPayload",true); h.router.setFavorite(item(),true);
        QTRY_COMPARE(failed.size(),5); QTRY_COMPARE(done.size(),1);
        QCOMPARE(done[0][1].toMap().size(),3);
        QVERIFY(!failed.last()[1].toMap().contains("detail"));
        QVERIFY(!failed.last()[1].toMap().values().contains("secret"));
    }
    void concurrentIdsIgnoreLateAndDuplicateTerminals()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        QSignalSpy done(&h.router,&MediaActionRouter::actionSucceeded),failed(&h.router,&MediaActionRouter::actionFailed);
        const auto one=h.router.setFavorite(item(),true); const auto pid1=s->property("lastRequest").toUuid();
        const auto two=h.router.setFavorite(item("home",MediaEntityTypeV2::Track,"two"),false); const auto pid2=s->property("lastRequest").toUuid();
        emit s->actionCompleted(QUuid::createUuid(),{});
        const auto ref2=mediaRefV2FromVariantMap(item("home",MediaEntityTypeV2::Track,"two").value("ref").toMap());
        emit s->actionCompleted(pid2,{SourceActionV2::Unfavorite,ref2,{{"favorite",false}}});
        emit s->requestFailed(pid2,{});
        emit s->actionCompleted(pid1,{SourceActionV2::Favorite,mediaRefV2FromVariantMap(item().value("ref").toMap()),{{"favorite",true}}});
        QTRY_COMPARE(done.size(),2); QCOMPARE(failed.size(),0);
        QCOMPARE(done[0][0].toUuid(),two); QCOMPARE(done[1][0].toUuid(),one);
    }
    void closeDisableAndTeardownCancelPending()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        h.router.setFavorite(item(),true); QVERIFY(h.registry.closeInstance("task6/home"));
        QTRY_COMPARE(failed.size(),1);
        s=h.session(); s->setProperty("async",true); h.router.setFavorite(item(),true);
        QVERIFY(h.registry.disableInstance("task6/home")); QTRY_COMPARE(failed.size(),2);
        h.router.setFavorite(item(),true); QTRY_COMPARE(failed.size(),3);
        QVERIFY(h.registry.enableInstance("task6/home")); s=h.session(); s->setProperty("async",true);
        auto router=new MediaActionRouter(&h.registry); router->setFavorite(item(),true);
        const auto pid=s->property("lastRequest").toUuid(); delete router;
        QCOMPARE(s->property("cancelled").toUuid(),pid);
        QCOMPARE(s->parent(),&h.registry);
    }
    void reentrantCloseDuringStartCannotPublishSuccessOrUnloadCode()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session();
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed),done(&h.router,&MediaActionRouter::actionSucceeded);
        PluginOperationResult unload=PluginOperationResult::Success;
        connect(s,&IMusicSourceSessionV2::requestStarted,&h.router,[&] {
            h.registry.closeInstance("task6/home"); unload=h.plugins.unload("org.quemusic.source.task6");
            QCoreApplication::processEvents();
        });
        const auto id=h.router.setFavorite(item(),true); QCOMPARE(failed.size(),0);
        QCOMPARE(unload,PluginOperationResult::Busy);
        QTRY_COMPARE(failed.size(),1); QCOMPARE(failed[0][0].toUuid(),id); QCOMPARE(done.size(),0);
    }
    void reentrantNestedRequestCorrelatesReturnedIds()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); bool nested=false; QUuid nestedId;
        QSignalSpy done(&h.router,&MediaActionRouter::actionSucceeded),failed(&h.router,&MediaActionRouter::actionFailed);
        connect(s,&IMusicSourceSessionV2::requestStarted,&h.router,[&] {
            if (nested) return; nested=true;
            nestedId=h.router.setFavorite(item("home",MediaEntityTypeV2::Track,"nested"),false);
            QCoreApplication::processEvents();
        });
        const auto outer=h.router.setFavorite(item(),true);
        QTRY_COMPARE(done.size()+failed.size(),2); QCOMPARE(failed.size(),0);
        QCOMPARE(done[0][0].toUuid(),nestedId); QCOMPARE(done[1][0].toUuid(),outer);
    }
    void capabilityRevocationDuringReadNeverInvokesProvider()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session();
        s->setProperty("revokeDuringRead",true);
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        h.router.setFavorite(item(),true);
        QCOMPARE(s->property("calls").toInt(),0);
        QTRY_COMPARE(failed.size(),1);
    }
    void descriptorAndCapabilityCallbacksMayCloseSession()
    {
        for (bool descriptor:{false,true}) {
            ActionHarness h; QVERIFY(h.init()); auto s=h.session();
            ActionHook hook; bool closed=false;
            hook.callback=[&] { if (!closed) { closed=true; h.registry.closeInstance("task6/home"); } };
            if (descriptor) QObject::connect(h.root(),SIGNAL(descriptorRead()),&hook,SLOT(run()));
            else QObject::connect(s,SIGNAL(capabilitiesRead()),&hook,SLOT(run()));
            QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed),done(&h.router,&MediaActionRouter::actionSucceeded);
            h.router.setFavorite(item(),true); QCOMPARE(failed.size(),0);
            QTRY_COMPARE(failed.size(),1); QCOMPARE(done.size(),0); QVERIFY(closed);
        }
    }
    void startIsMandatoryAndUnstartedReturnedIdIsNeverCancelled()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("skipStarted",true);
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        h.router.setFavorite(item(),true);
        QTRY_COMPARE(failed.size(),1); QCOMPARE(s->property("cancelCount").toInt(),0);
    }
    void unstartedReturnCannotBeCancelledAfterReentrantInvalidation_data()
    {
        QTest::addColumn<bool>("destroyRouter");
        QTest::newRow("capabilities-changed") << false;
        QTest::newRow("router-destroyed") << true;
    }
    void unstartedReturnCannotBeCancelledAfterReentrantInvalidation()
    {
        QFETCH(bool,destroyRouter);
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("skipStarted",true);
        QPointer<MediaActionRouter> router=new MediaActionRouter(&h.registry,&h.router);
        QSignalSpy failed(router,&MediaActionRouter::actionFailed),done(router,&MediaActionRouter::actionSucceeded);
        bool observed=false;
        connect(s,&IMusicSourceSessionV2::actionCompleted,&h.router,[&] {
            observed=true;
            if (destroyRouter) delete router.data();
            else emit s->capabilitiesChanged({});
        });
        const auto id=router->setFavorite(item(),true);
        QVERIFY(observed); QVERIFY(!id.isNull()); QCOMPARE(failed.size(),0);
        if (destroyRouter) QVERIFY(!router);
        else { QTRY_COMPARE(failed.size(),1); QCOMPARE(failed[0][0].toUuid(),id); }
        QCOMPARE(done.size(),0); QCOMPARE(s->property("cancelCount").toInt(),0);
        QVERIFY(s->property("cancelled").toUuid().isNull());
    }
    void ownedPendingReturnIsCancelledAfterInlineCapabilityInvalidation()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed);
        connect(s,&IMusicSourceSessionV2::requestStarted,&h.router,[&] { emit s->capabilitiesChanged({}); });
        const auto id=h.router.setFavorite(item(),true); QCOMPARE(failed.size(),0);
        QTRY_COMPARE(failed.size(),1); QCOMPARE(failed[0][0].toUuid(),id);
        QCOMPARE(s->property("cancelled").toUuid(),s->property("lastRequest").toUuid());
        QCOMPARE(s->property("cancelCount").toInt(),1);
    }
    void externalSessionDestructionFinishesPending()
    {
        ActionHarness h; QVERIFY(h.init(true)); auto s=h.session(); s->setProperty("async",true);
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed),done(&h.router,&MediaActionRouter::actionSucceeded);
        const auto id=h.router.setFavorite(item(),true); delete s;
        QTRY_COMPARE(failed.size(),1); QCOMPARE(failed[0][0].toUuid(),id); QCOMPARE(done.size(),0);
    }
    void routerDeletedInsideProviderCancelsAfterUnwind()
    {
        ActionHarness h; QVERIFY(h.init()); auto s=h.session(); s->setProperty("async",true);
        auto router=new MediaActionRouter(&h.registry);
        connect(s,&IMusicSourceSessionV2::requestStarted,&h.router,[&] { delete router; router=nullptr; });
        const auto id=router->setFavorite(item(),true);
        QVERIFY(!id.isNull()); QVERIFY(!router);
        QCOMPARE(s->property("cancelled").toUuid(),s->property("lastRequest").toUuid());
        QCOMPARE(s->property("cancelCount").toInt(),1);
    }
    void leaseReleaseReentrancyCannotPublishStaleSuccess()
    {
        ActionHarness h; QVERIFY(h.init()); h.session();
        bool closed=false;
        QSignalSpy failed(&h.router,&MediaActionRouter::actionFailed),done(&h.router,&MediaActionRouter::actionSucceeded);
        h.router.setFavorite(item(),true);
        connect(&h.plugins,&PluginManager::pluginChanged,&h.router,[&](const QString &package) {
            if (!closed && h.plugins.plugin(package).activeLeases==1) {
                closed=true; h.registry.closeInstance("task6/home");
            }
        });
        QTRY_COMPARE(failed.size()+done.size(),1);
        QVERIFY(closed); QCOMPARE(failed.size(),1); QCOMPARE(done.size(),0);
    }
};
QTEST_GUILESS_MAIN(MediaActionRouterV2Test)
#endif
#include "tst_MediaActionRouterV2.moc"
