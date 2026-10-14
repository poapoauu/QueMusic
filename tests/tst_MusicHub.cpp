#if defined(QUEMUSIC_HUB_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/artist-browse/v1/IArtistBrowseProviderV1.h"
#include "extensions/browse-facets/v1/IBrowseFacetsProviderV1.h"

class HubFixtureSession : public IMusicSourceSessionV2, public IPageProviderV2,
                                public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2)
public:
    SourceConfigurationV2 config;
    HubFixtureSession(SourceConfigurationV2 c, QObject *p) : IMusicSourceSessionV2(p), config(c) {}
    SourceIdentityV2 identity() const override { return {"task7", config.sourceInstanceId, config.accountId, config.displayName}; }
    SourceSessionStateV2 state() const override {
        return property("connecting").toBool() ? SourceSessionStateV2::Connecting : SourceSessionStateV2::Ready;
    }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override { auto id = QUuid::createUuid(); emit requestStarted(id); emit actionCompleted(id, {}); return id; }
    void close() override { emit stateChanged(SourceSessionStateV2::Closing); }
    void cancel(const QUuid &id) override { setProperty("cancelled", id); }
    QUuid fetchPage(const PageQueryV2 &query) override
    {
        const auto id = QUuid::createUuid();
        auto requests = property("requests").toList();
        requests.append(QVariantMap{{"id", id}, {"page", int(query.page)}, {"section", int(query.section)},
            {"scope", query.scope.sourceInstanceId}, {"search", query.searchText}, {"filters", query.filters},
            {"limit", query.limit}, {"cursor", query.cursor}});
        setProperty("requests", requests);
        emit requestStarted(id);
        if (property("hold").toBool()) return id;
        if (property("unsupported").toBool()) { emit requestFailed(id, {SourceErrorKindV2::Unsupported}); return id; }
        if (property("fail").toBool()) { emit requestFailed(id, {SourceErrorKindV2::Network}); return id; }
        QList<PageSectionKindV2> kinds{query.section};
        if (property("bundle").toBool() && query.cursor.isEmpty()
            && query.page == MusicPageKindV2::Search)
            kinds = {PageSectionKindV2::Albums, PageSectionKindV2::Tracks};
        PageResultV2 result;
        for (auto kind : kinds) {
            PageSectionV2 section;
            section.kind = kind; section.sectionId = QString::number(int(kind));
            MediaItemV2 item;
            const bool rootDirectories = query.filters.value("entityType").toInt() ==
                int(MediaEntityTypeV2::Directory);
            const bool insideDirectory = query.filters.contains("directoryId");
            const auto entityType = rootDirectories || insideDirectory
                ? MediaEntityTypeV2::Directory : query.filters.contains("facetValues")
                ? MediaEntityTypeV2::Artist : MediaEntityTypeV2::Track;
            item.ref = {"task7", config.sourceInstanceId, config.accountId, entityType,
                        rootDirectories ? "root-" + config.accountId
                                        : insideDirectory ? "child-" + config.accountId
                                                          : QString::number(requests.size())};
            item.title = query.searchText.isEmpty() ? config.accountId : query.searchText;
            if (property("badArtistRow").toBool()) item.ref.entityType = MediaEntityTypeV2::Album;
            if (property("foreignArtistRow").toBool()) item.ref.accountId = "foreign";
            if (property("badArtistSection").toBool()) section.kind = PageSectionKindV2::Albums;
            section.items = {item};
            section.hasMore = !rootDirectories && query.cursor.isEmpty();
            section.nextCursor = section.hasMore ? "provider-" + section.sectionId : QString{};
            result.sections.append(section);
        }
        emit pageReady(id, result);
        emit pageReady(id, result); // Deliberately duplicated provider terminal.
        return id;
    }
    QUuid resolveStream(const MediaRefV2 &) override { return {}; }
    QUuid fetchArtwork(const MediaRefV2 &ref) override { return asset(ref, SourceActionV2::Artwork); }
    QUuid fetchLyrics(const MediaRefV2 &ref) override { return asset(ref, SourceActionV2::Lyrics); }
    QUuid asset(const MediaRefV2 &ref, SourceActionV2 action)
    {
        const auto id = QUuid::createUuid();
        emit requestStarted(id);
        ActionResultV2 result{action, ref, {}};
        if (action == SourceActionV2::Artwork)
            result.payload = {{"bytes", QByteArray("fixture-image")}, {"mimeType", QString("image/png")}};
        else result.payload = {{"lyrics", QString("[00:01]Words")}};
        emit actionCompleted(id, result);
        return id;
    }
};
class ArtistHubFixtureSession : public HubFixtureSession, public IArtistBrowseProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IArtistBrowseProviderV1)
public:
    using HubFixtureSession::HubFixtureSession;
    std::optional<ArtistBrowseTargetV1> artistBrowseTarget(const MediaRefV2 &ref) const override
    {
        auto *self = const_cast<ArtistHubFixtureSession *>(this);
        self->setProperty("browseChecks", property("browseChecks").toInt() + 1);
        if (ref.sourcePluginId != "task7" || ref.sourceInstanceId != config.sourceInstanceId
            || ref.accountId != config.accountId || ref.entityType != MediaEntityTypeV2::Artist) return {};
        const auto target = property("browseTarget");
        const auto result = target.isValid() ? ArtistBrowseTargetV1(target.toInt()) : ArtistBrowseTargetV1::Tracks;
        if (property("denyBrowse").toBool()) return {};
        if (property("notifyOnBrowse").toBool()) emit self->capabilitiesChanged({});
        return result;
    }
};
class FacetHubFixtureSession final : public ArtistHubFixtureSession, public IBrowseFacetsProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IBrowseFacetsProviderV1)
public:
    using ArtistHubFixtureSession::ArtistHubFixtureSession;
    std::optional<BrowseFacetSchemaV1> browseFacets(PageSectionKindV2 section) const override
    {
        auto *self=const_cast<FacetHubFixtureSession *>(this);
        self->setProperty("facetChecks",property("facetChecks").toInt()+1);
        const auto revision=property("facetRevision").isValid() ? property("facetRevision").toString() : QStringLiteral("catalog-v1");
        BrowseFacetSchemaV1 schema{section,revision,{{QStringLiteral("origin"),QStringLiteral("Origin"),
            {{QStringLiteral("alpha"),QStringLiteral("Alpha")},{QStringLiteral("beta"),QStringLiteral("Beta")}}}}};
        const bool withdrawn=property("withdrawFacets").toBool();
        if (property("notifyOnFacets").toBool()) emit self->capabilitiesChanged({});
        return withdrawn ? std::nullopt : std::optional(schema);
    }
};
class HubFixturePlugin final : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override { return {"org.quemusic.source.task7", "task7", "Task7", "2.0.0", 2, {}}; }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &c, QObject *p) override {
        setProperty("sessionCreates", property("sessionCreates").toInt() + 1);
        auto *session = property("browseFacets").toBool() && c.accountId=="home"
            ? static_cast<HubFixtureSession *>(new FacetHubFixtureSession(c,p))
            : property("artistBrowse").toBool() && c.accountId == "home"
            ? static_cast<HubFixtureSession *>(new ArtistHubFixtureSession(c, p)) : new HubFixtureSession(c, p);
        session->setProperty("connecting", property("connecting"));
        return session;
    }
};
#else
#include "MusicHub.h"
#include "SourceAccountStore.h"
#include "PageCache.h"
#include "PageRepository.h"
#include "MediaAssetRepository.h"
#include "extensions/artist-browse/v1/IArtistBrowseProviderV1.h"
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtConcurrentRun>

class HubSecrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return QByteArray{}; }
    bool remove(const QString &, QString *) override { return true; }
};
struct HubHarness {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("settings.ini"), QSettings::IniFormat};
    HubSecrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    SourceScopeStore scope{&settings};
    std::unique_ptr<MusicHub> hub;
    bool init(bool artistBrowse = false,bool facets = false)
    {
        plugins.addSearchPath(QUEMUSIC_TASK7_PACKAGES);
        if (plugins.discover() != 1 || !plugins.load("org.quemusic.source.task7")) return false;
        plugins.pluginInstance("org.quemusic.source.task7")->setProperty("artistBrowse", artistBrowse);
        plugins.pluginInstance("org.quemusic.source.task7")->setProperty("browseFacets",facets);
        if (!accounts.saveResolvedV2({"task7", "home", "Home", {}, {}})
            || !accounts.saveResolvedV2({"task7", "office", "Office", {}, {}})) return false;
        settings.setValue("MusicHub/cacheDirectory", dir.filePath("cache"));
        hub = std::make_unique<MusicHub>(&registry, &scope, &settings);
        return true;
    }
    ~HubHarness() { hub.reset(); musicCacheIoPool()->waitForDone(); }
    IMusicSourceSessionV2 *session(const QString &id = "home") { return registry.sessionFor("task7/" + id); }
    QVariantList requests(const QString &id = "home") { return session(id)->property("requests").toList(); }
};
static QVariantMap item(MediaEntityTypeV2 type, const QString &entity = "entity")
{
    return {{"ref", mediaRefV2ToVariantMap({"task7", "task7/home", "home", type, entity})},
            {"title", "Entity"}, {"availableActions", QVariantMap{}}};
}
static int rowFor(MusicPageModel *model, PageSectionKindV2 kind)
{
    for (int i = 0; i < model->rowCount(); ++i) if (model->section(i).kind == kind) return i;
    return -1;
}
class MusicHubTest final : public QObject {
    Q_OBJECT
private slots:
    void facetChangingSelectionCancelsOldRequestAndLateRowsCannotOverwrite()
    {
        HubHarness h; QVERIFY(h.init(false,true)); h.hub->setSelectedSourceInstanceId("task7/home");
        QVERIFY(h.hub->refreshCategoryFacets()); auto *session=h.session(); session->setProperty("hold",true);
        QVERIFY(h.hub->setCategoryFacetValues(h.hub->categoryFacets()["token"].toString(),{{"origin","alpha"}}));
        QTRY_COMPARE(h.requests().size(),1); const auto oldId=h.requests()[0].toMap()["id"].toUuid();
        session->setProperty("hold",false);
        QVERIFY(h.hub->setCategoryFacetValues(h.hub->categoryFacets()["token"].toString(),{{"origin","beta"}}));
        QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready);
        QCOMPARE(session->property("cancelled").toUuid(),oldId);
        const auto rows=h.hub->category()->itemAt(0,0);
        PageSectionV2 old; old.kind=PageSectionKindV2::Artists; old.sectionId="stale";
        MediaItemV2 entry; entry.ref={"task7","task7/home","home",MediaEntityTypeV2::Artist,"stale"}; entry.title="STALE";
        old.items={entry}; emit session->pageReady(oldId,{{old},{},false,true}); QCoreApplication::processEvents();
        QCOMPARE(h.hub->category()->itemAt(0,0),rows);
        QCOMPARE(h.hub->categoryFacets()["values"].toMap(),QVariantMap({{"origin","beta"}}));
    }
    void facetSnapshotNotificationCanDestroyHubWithoutAccessingFreedState()
    {
        HubHarness h; QVERIFY(h.init(false,true)); h.hub->setSelectedSourceInstanceId("task7/home");
        connect(h.hub.get(),&MusicHub::categoryFacetsChanged,this,[&] { h.hub.reset(); });
        auto *hub=h.hub.get(); QVERIFY(!hub->refreshCategoryFacets()); QVERIFY(!h.hub);
    }
    void facetSnapshotGetterIsPureAndSelectionIsExplicitStrictAndTokenBound()
    {
        HubHarness h; QVERIFY(h.init(false,true));
        h.hub->setSelectedSourceInstanceId("task7/home");
        auto *root=h.plugins.pluginInstance("org.quemusic.source.task7");
        for (int i=0;i<5;++i) QVERIFY(h.hub->categoryFacets().isEmpty());
        QCOMPARE(root->property("sessionCreates").toInt(),0);
        QVERIFY(h.hub->refreshCategoryFacets()); auto *session=h.session();
        auto snapshot=h.hub->categoryFacets(); QVERIFY(snapshot.value("supported").toBool());
        QVERIFY(!snapshot.value("active").toBool()); const auto token=snapshot.value("token").toString();
        const auto checks=session->property("facetChecks").toInt();
        auto foreign=QtConcurrent::run([&] {
            return h.hub->refreshCategoryFacets() || h.hub->setCategoryFacetValues(token,{{"origin","alpha"}})
                || h.hub->clearCategoryFacets(token);
        });
        foreign.waitForFinished(); QVERIFY(!foreign.result());
        for (int i=0;i<5;++i) QCOMPARE(h.hub->categoryFacets(),snapshot);
        QCOMPARE(session->property("facetChecks").toInt(),checks); QVERIFY(h.requests().isEmpty());
        QVERIFY(!h.hub->setCategoryFacetValues("stale",{{"origin","alpha"}}));
        QVERIFY(!h.hub->setCategoryFacetValues(token,{{"origin",1}}));
        QVERIFY(!h.hub->setCategoryFacetValues(token,{{"origin","unknown"}}));
        QVERIFY(!h.hub->setCategoryFacetValues(token,{}));
        QCOMPARE(session->property("facetChecks").toInt(),checks); QVERIFY(h.requests().isEmpty());
        QVERIFY(h.hub->setCategoryFacetValues(token,{{"origin","alpha"}}));
        QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready); QCOMPARE(h.requests().size(),1);
        QCOMPARE(h.requests().last().toMap()["filters"].toMap().value("facetValues").toMap(),QVariantMap({{"origin","alpha"}}));
        QCOMPARE(h.hub->category()->rowCount(),1); QCOMPARE(h.hub->category()->section(0).kind,PageSectionKindV2::Artists);
        QVERIFY(!h.hub->setCategoryFacetValues(token,{{"origin","beta"}}));
        snapshot=h.hub->categoryFacets(); QVERIFY(snapshot.value("active").toBool());
        QVERIFY(h.hub->clearCategoryFacets(snapshot.value("token").toString()));
        QTRY_VERIFY(h.requests().size()>=6); QVERIFY(!h.hub->categoryFacets().value("active").toBool());
    }
    void facetDetailsHideControlsAndBackRestoresSelectionWithoutMixingFilters()
    {
        HubHarness h; QVERIFY(h.init(false,true)); h.hub->setSelectedSourceInstanceId("task7/home");
        QVERIFY(h.hub->refreshCategoryFacets());
        QVERIFY(h.hub->setCategoryFacetValues(h.hub->categoryFacets()["token"].toString(),{{"origin","alpha"}}));
        QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready);
        const auto oldToken=h.hub->categoryFacets()["token"].toString();
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Artist)));
        QVERIFY(h.hub->categoryFacets().isEmpty()); QVERIFY(!h.hub->clearCategoryFacets(oldToken));
        QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready);
        QVERIFY(!h.requests().last().toMap()["filters"].toMap().contains("facetValues"));
        QVERIFY(h.hub->navigateBack()); QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready);
        QCOMPARE(h.hub->categoryFacets()["values"].toMap(),QVariantMap({{"origin","alpha"}}));
        QVERIFY(h.hub->categoryFacets()["token"].toString()!=oldToken);
        h.hub->setSelectedSourceInstanceId("task7/office"); QTRY_VERIFY(h.hub->categoryFacets().isEmpty());
        QVERIFY(!h.hub->setCategoryFacetValues(oldToken,{{"origin","beta"}}));
        h.hub->setSelectedSourceInstanceId(""); QVERIFY(h.hub->categoryFacets().isEmpty());
    }
    void facetWithdrawalRetainsSelectionAndRequiresExplicitClear()
    {
        HubHarness h; QVERIFY(h.init(false,true)); h.hub->setSelectedSourceInstanceId("task7/home");
        QVERIFY(h.hub->refreshCategoryFacets());
        QVERIFY(h.hub->setCategoryFacetValues(h.hub->categoryFacets()["token"].toString(),{{"origin","alpha"}}));
        QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Ready);
        auto *session=h.session(); const auto calls=h.requests().size(); session->setProperty("withdrawFacets",true);
        h.hub->refresh(1); QTRY_COMPARE(h.hub->category()->state(),PageLoadStateV2::Failed);
        QCOMPARE(h.requests().size(),calls); const auto snapshot=h.hub->categoryFacets();
        QVERIFY(snapshot["active"].toBool()); QVERIFY(!snapshot["supported"].toBool()); QVERIFY(!snapshot["selectionValid"].toBool());
        QVERIFY(h.hub->clearCategoryFacets(snapshot["token"].toString())); QTRY_VERIFY(h.requests().size()>calls);
    }
    void facetSchemaChangeRejectsOldChoiceAndNotificationCannotOverwriteNewNavigation()
    {
        HubHarness h; QVERIFY(h.init(false,true)); h.hub->setSelectedSourceInstanceId("task7/home");
        QVERIFY(h.hub->refreshCategoryFacets()); auto *session=h.session();
        const auto old=h.hub->categoryFacets()["token"].toString(); session->setProperty("facetRevision","catalog-v2");
        QVERIFY(!h.hub->setCategoryFacetValues(old,{{"origin","alpha"}})); QVERIFY(h.requests().isEmpty());
        QCOMPARE(h.hub->categoryFacets()["revision"].toString(),QString("catalog-v2"));
        session->setProperty("notifyOnFacets",true);
        auto connection=connect(session,&IMusicSourceSessionV2::capabilitiesChanged,this,[&] { h.hub->setSelectedSourceInstanceId("task7/office"); });
        QVERIFY(!h.hub->refreshCategoryFacets()); QCOMPARE(h.hub->selectedSourceInstanceId(),QString("task7/office"));
        QVERIFY(h.hub->categoryFacets().isEmpty()); disconnect(connection);
    }
    void artistColdNavigationCreatesSessionOnlyForExplicitBrowse()
    {
        // Cached cards may precede every session; ordinary getters stay inert.
        for (const bool extended : {false, true}) {
            HubHarness h; QVERIFY(h.init(extended));
            auto *plugin = h.plugins.pluginInstance("org.quemusic.source.task7");
            h.hub->categoryContext(); h.hub->sourceOptions();
            QVERIFY(!h.registry.artistBrowseSection({"task7", "task7/home", "home", MediaEntityTypeV2::Artist, "entity"}));
            QCOMPARE(plugin->property("sessionCreates").toInt(), 0);
            auto artist = item(MediaEntityTypeV2::Artist); artist["metadata"] = QVariantMap{{"browseTarget", "tracks"}};
            QVERIFY(h.hub->browse(artist)); QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
            QCOMPARE(plugin->property("sessionCreates").toInt(), 1);
            QCOMPARE(h.requests().last().toMap()["section"].toInt(),
                     int(extended ? PageSectionKindV2::Tracks : PageSectionKindV2::Albums));
        }
    }
    void oldArtistConnectingSessionPreservesDeferredAlbumsBrowse()
    {
        HubHarness h; QVERIFY(h.init());
        h.plugins.pluginInstance("org.quemusic.source.task7")->setProperty("connecting", true);
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Artist)));
        QCoreApplication::processEvents(); QVERIFY(h.requests().isEmpty());
        auto *session = h.session(); session->setProperty("connecting", false);
        emit session->stateChanged(SourceSessionStateV2::Ready);
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests().last().toMap()["section"].toInt(), int(PageSectionKindV2::Albums));
    }
    void artistBrowseNegotiatesTracksAndRechecksContinuation()
    {
        HubHarness h; QVERIFY(h.init(true)); auto *session = h.session();
        QVERIFY(qobject_cast<IArtistBrowseProviderV1 *>(session));
        QVERIFY(!qobject_cast<IArtistBrowseProviderV1 *>(h.session("office")));
        auto artist = item(MediaEntityTypeV2::Artist);
        artist["metadata"] = QVariantMap{{"browseTarget", "albums"}}; // not a routing authority
        QVERIFY(h.hub->browse(artist)); QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(session->property("browseChecks").toInt(), 2); // navigation + execution
        QCOMPARE(h.requests().last().toMap()["section"].toInt(), int(PageSectionKindV2::Tracks));
        QCOMPARE(h.requests().last().toMap()["filters"].toMap(), QVariantMap({{"artistId", "entity"}}));
        const auto section = h.hub->category()->section(0).sectionId;
        h.hub->loadMore(1, section); QTRY_COMPARE(h.hub->category()->section(0).items.size(), 2);
        QCOMPARE(session->property("browseChecks").toInt(), 3);
        QCOMPARE(h.requests().last().toMap()["cursor"].toString(), "provider-8");
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Album)));
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QVERIFY(h.hub->navigateBack()); QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        const auto calls = h.requests().size(); const auto retained = h.hub->category()->itemAt(0, 0);
        session->setProperty("denyBrowse", true);
        h.hub->refresh(1); QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Failed);
        QCOMPARE(h.requests().size(), calls);
        QCOMPARE(h.hub->category()->itemAt(0, 0), retained); // preserve accepted rows under refresh failure
    }
    void artistBrowseDenialAndInvalidTargetDoNotMutateNavigation()
    {
        HubHarness h; QVERIFY(h.init(true)); auto *session = h.session();
        for (const int target : {-1, 99}) {
            session->setProperty("browseTarget", target);
            QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Artist)));
            QVERIFY(h.hub->categoryContext().isEmpty()); QVERIFY(h.requests().isEmpty());
        }
        session->setProperty("browseTarget", int(ArtistBrowseTargetV1::Albums));
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Artist)));
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests().last().toMap()["section"].toInt(), int(PageSectionKindV2::Albums));
        const auto context = h.hub->categoryContext(); const auto calls = h.requests().size();
        session->setProperty("denyBrowse", true);
        QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Artist, "other")));
        QCOMPARE(h.hub->categoryContext(), context); QCOMPARE(h.requests().size(), calls);
        auto foreign = item(MediaEntityTypeV2::Artist); auto ref = foreign["ref"].toMap();
        ref["accountId"] = "office"; foreign["ref"] = ref;
        const auto checks = session->property("browseChecks").toInt();
        QVERIFY(!h.hub->browse(foreign)); QCOMPARE(session->property("browseChecks").toInt(), checks);
    }
    void artistTracksRejectWrongSectionAndForeignOrNonTrackRows()
    {
        for (const auto *property : {"badArtistRow", "foreignArtistRow", "badArtistSection"}) {
            HubHarness h; QVERIFY(h.init(true)); h.session()->setProperty(property, true);
            QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Artist)));
            QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Failed);
            for (int row = 0; row < h.hub->category()->rowCount(); ++row) QVERIFY(h.hub->category()->section(row).items.isEmpty());
        }
    }
    void artistBrowseCallbackRetirementIsSafeAndLeaseRemainsCallable()
    {
        HubHarness h; QVERIFY(h.init(true)); QPointer<IMusicSourceSessionV2> session = h.session();
        session->setProperty("notifyOnBrowse", true);
        connect(session, &IMusicSourceSessionV2::capabilitiesChanged, h.hub.get(), [&] {
            QVERIFY(h.registry.closeInstance("task7/home"));
            QCOMPARE(h.plugins.unload("org.quemusic.source.task7"), PluginOperationResult::Busy);
        });
        QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Artist)));
        QVERIFY(!session); QVERIFY(h.hub->categoryContext().isEmpty());
        QCOMPARE(h.plugins.unload("org.quemusic.source.task7"), PluginOperationResult::Success);
    }
    void artistBrowseWithdrawnBeforeLoadMoreRetainsItemsWithoutProviderFetch()
    {
        HubHarness h; QVERIFY(h.init(true)); auto *session = h.session();
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Artist)));
        auto *model = h.hub->category(); QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        const auto first = model->itemAt(0, 0); const auto calls = h.requests().size();
        session->setProperty("browseTarget", int(ArtistBrowseTargetV1::Albums));
        h.hub->loadMore(1, model->section(0).sectionId);
        QTRY_VERIFY(!model->data(model->index(0), MusicPageModel::ErrorRole).toMap().isEmpty());
        QCOMPARE(h.requests().size(), calls); QCOMPARE(model->itemAt(0, 0), first);
        QVERIFY(!model->data(model->index(0), MusicPageModel::LoadingMoreRole).toBool());
    }
    void artistBrowseReentryCannotReplaceNewerNavigation()
    {
        HubHarness h; QVERIFY(h.init(true)); auto *session = h.session(); session->setProperty("notifyOnBrowse", true);
        connect(session, &IMusicSourceSessionV2::capabilitiesChanged, h.hub.get(), [&] {
            QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Artist))); // recursive extension call denied
            QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Album, "newer")));
        });
        QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Artist)));
        QCOMPARE(h.hub->categoryContext()["item"].toMap()["ref"].toMap()["entityId"].toString(), "newer");
    }
    void capabilitiesInvalidateAllOrdinaryPagesOnlyInTheirScope() {
        HubHarness h; QVERIFY(h.init());
        auto *home=h.session("home"); auto *office=h.session("office");
        h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(0); h.hub->activatePage(1); h.hub->activatePage(2); h.hub->search("query");
        const QList<MusicPageModel *> models{h.hub->recommendation(),h.hub->category(),h.hub->favorites(),h.hub->searchResults()};
        for (auto *model:models) QTRY_COMPARE(model->state(),PageLoadStateV2::Ready);
        const auto before=h.requests().size();
        emit office->capabilitiesChanged({}); QCoreApplication::processEvents();
        QCOMPARE(h.requests().size(),before);
        for (auto *model:models) QVERIFY(model->rowCount()>0);
        home->setProperty("hold",true);
        emit home->capabilitiesChanged({});
        for (auto *model:models) QCOMPARE(model->rowCount(),0);
        QTRY_COMPARE(h.requests().size(),before+15); // 5 recommendation, 5 category, 4 favorites, 1 search
        const auto pending=h.requests().mid(before);
        for (auto *model:models) QCOMPARE(model->state(),PageLoadStateV2::Loading);
        emit home->capabilitiesChanged({});
        for (auto *model:models) QCOMPARE(model->rowCount(),0);
        for (const auto &request:pending) emit home->pageReady(request.toMap().value("id").toUuid(),{});
        QCoreApplication::processEvents();
        for (auto *model:models) QCOMPARE(model->rowCount(),0);
        home->setProperty("hold",false); emit home->capabilitiesChanged({});
        for (auto *model:models) QTRY_COMPARE(model->state(),PageLoadStateV2::Ready);
    }
    void capabilityRefreshDoesNotReactivateCancelledPage() {
        HubHarness h; QVERIFY(h.init());
        h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(0); QTRY_COMPARE(h.hub->recommendation()->state(),PageLoadStateV2::Ready);
        const auto before=h.requests().size();
        bool cancelled=false;
        connect(h.hub->recommendation(),&QAbstractItemModel::modelReset,h.hub.get(),[&] {
            if (!cancelled) { cancelled=true; h.hub->cancel(0); }
        });
        emit h.session()->capabilitiesChanged({}); QCoreApplication::processEvents();
        QVERIFY(cancelled); QCOMPARE(h.requests().size(),before);
        QCOMPARE(h.hub->recommendation()->state(),PageLoadStateV2::Idle);
    }
    void capabilityBatchStopsAfterScopeRoundTrip() {
        HubHarness h; QVERIFY(h.init());
        h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(0); QTRY_COMPARE(h.hub->recommendation()->state(),PageLoadStateV2::Ready);
        QSignalSpy favoriteResets(h.hub->favorites(),&QAbstractItemModel::modelReset);
        bool switched=false;
        connect(h.hub->recommendation(),&QAbstractItemModel::modelReset,h.hub.get(),[&] {
            if (!switched) {
                switched=true;
                h.hub->setSelectedSourceInstanceId("task7/office");
                h.hub->setSelectedSourceInstanceId("task7/home");
            }
        });
        emit h.session()->capabilitiesChanged({}); QVERIFY(switched);
        // Only the two real scope changes reset later pages. The old capability
        // batch must not resume merely because the final string equals home.
        QCOMPARE(favoriteResets.size(),2);
        QTRY_COMPARE(h.hub->recommendation()->state(),PageLoadStateV2::Ready);
        QCOMPARE(h.hub->selectedSourceInstanceId(),QString("task7/home"));
        QCOMPARE(h.hub->favorites()->state(),PageLoadStateV2::Idle);
    }
    void directoryBrowseUsesGenericEntityFilter()
    {
        HubHarness h; QVERIFY(h.init());
        h.hub->setSelectedSourceInstanceId("task7/home");
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Directory, "root-home")));
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        const auto request = h.requests().last().toMap();
        QCOMPARE(request.value("filters").toMap().value("directoryId").toString(),
                 QString("root-home"));
        QCOMPARE(request.value("scope").toString(), QString("task7/home"));
        QVERIFY(h.hub->navigateBack());
    }
    void contentRefreshInvalidatesOnlyMatchingInstance()
    {
        HubHarness h; QVERIFY(h.init());
        auto *home = h.session("home"); auto *office = h.session("office");
        h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(int(MusicPageKindV2::Recommendation));
        QTRY_COMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Ready);
        const auto homeCalls = h.requests().size();
        const auto officeCalls = h.requests("office").size();
        const auto oldTitle = h.hub->recommendation()->section(0).items.at(0).title;
        QSignalSpy refreshFailed(h.hub.get(), SIGNAL(sourceRefreshFailed(QString,QString)));
        QVERIFY(refreshFailed.isValid());
        QVERIFY(QMetaObject::invokeMethod(&h.registry, "instanceRefreshFailed",
            Q_ARG(QString, QString("task7/home")),
            Q_ARG(SourceErrorV2, (SourceErrorV2{SourceErrorKindV2::Unavailable,
                "plugin.unknownError", "private path should not leak"}))));
        QCOMPARE(refreshFailed.count(), 1);
        QCOMPARE(refreshFailed.at(0).at(1).toString(), QString("source.instance.refreshFailed"));
        QCOMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.hub->recommendation()->section(0).items.at(0).title, oldTitle);
        QCOMPARE(h.requests().size(), homeCalls);
        QVERIFY(QMetaObject::invokeMethod(&h.registry, "instanceContentChanged",
            Q_ARG(QString, QString("task7/office")), Q_ARG(quint64, quint64(1))));
        QCoreApplication::processEvents();
        QCOMPARE(h.requests().size(), homeCalls);
        QVERIFY(QMetaObject::invokeMethod(&h.registry, "instanceContentChanged",
            Q_ARG(QString, QString("task7/home")), Q_ARG(quint64, quint64(1))));
        QTRY_VERIFY(h.requests().size() > homeCalls);
        QTRY_COMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests("office").size(), officeCalls);
        QCOMPARE(h.session(), home); QCOMPARE(h.session("office"), office);
    }
    void sharedScopeResetsStableModelsAndRefreshesActivatedOnly()
    {
        HubHarness h; QVERIFY(h.init());
        auto *recommendation = h.hub->recommendation(); auto *category = h.hub->category();
        auto *favorites = h.hub->favorites(); auto *search = h.hub->searchResults();
        h.hub->activatePage(int(MusicPageKindV2::Recommendation));
        h.hub->activatePage(int(MusicPageKindV2::Favorites));
        QTRY_COMPARE(recommendation->state(), PageLoadStateV2::Ready);
        QTRY_COMPARE(favorites->state(), PageLoadStateV2::Ready);
        // Explicit refresh does not opt into future automatic activation.
        h.hub->refresh(1); QTRY_COMPARE(category->state(), PageLoadStateV2::Ready);
        const int before = h.requests().size();
        h.hub->setSelectedSourceInstanceId("task7/home");
        QCOMPARE(recommendation->rowCount(), 0); QCOMPARE(favorites->rowCount(), 0);
        QCOMPARE(category->rowCount(), 0); QCOMPARE(search->rowCount(), 0);
        QTRY_COMPARE(recommendation->state(), PageLoadStateV2::Ready);
        QTRY_COMPARE(favorites->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.hub->recommendation(), recommendation); QCOMPARE(h.hub->category(), category);
        QCOMPARE(h.hub->favorites(), favorites); QCOMPARE(h.hub->searchResults(), search);
        QCOMPARE(category->state(), PageLoadStateV2::Idle);
        const auto after = h.requests();
        for (int i = before; i < after.size(); ++i) QVERIFY(after[i].toMap()["page"].toInt() != int(MusicPageKindV2::Category));
        QCOMPARE(recommendation->itemAt(0, 0)["ref"].toMap()["accountId"].toString(), "home");
    }
    void searchDropsOldTextAndCancellationIgnoresLateCallbacks()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->search("Miles Davis");
        auto *model = h.hub->searchResults();
        QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        h.session()->setProperty("hold", true);
        h.hub->search("Coltrane"); QCOMPARE(model->rowCount(), 0);
        QTRY_COMPARE(h.requests().size(), 2);
        const auto query = h.requests().last().toMap();
        QCOMPARE(query["scope"].toString(), "task7/home"); QCOMPARE(query["search"].toString(), "Coltrane");
        h.hub->cancel(int(MusicPageKindV2::Search)); QCOMPARE(model->state(), PageLoadStateV2::Idle);
        emit h.session()->pageReady(query["id"].toUuid(), {});
        QCoreApplication::processEvents(); QCOMPARE(model->state(), PageLoadStateV2::Idle);
        h.session()->setProperty("hold", false);
        h.hub->refresh(int(MusicPageKindV2::Search)); QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        h.hub->cancel(int(MusicPageKindV2::Search)); QCOMPARE(model->state(), PageLoadStateV2::Ready);
    }
    void independentBundleContinuationsAndTargetRetry()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.session()->setProperty("bundle", true); h.hub->search("query");
        auto *model = h.hub->searchResults(); QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        QCOMPARE(model->rowCount(), 2);
        const int a = rowFor(model, PageSectionKindV2::Albums), b = rowFor(model, PageSectionKindV2::Tracks);
        QVERIFY(a >= 0 && b >= 0);
        const auto aid = model->section(a).sectionId, bid = model->section(b).sectionId;
        const auto tokenB = model->section(b).nextCursor;
        h.session()->setProperty("hold", true);
        h.hub->loadMore(3, aid); h.hub->loadMore(3, aid); h.hub->loadMore(3, bid);
        QTRY_COMPARE(h.requests().size(), 3);
        QCOMPARE(h.requests()[1].toMap()["cursor"].toString(), "provider-7");
        QCOMPARE(h.requests()[2].toMap()["cursor"].toString(), "provider-8");
        for (int i : {1, 2}) {
            const auto request = h.requests()[i].toMap();
            QCOMPARE(request["search"].toString(), "query"); QCOMPARE(request["limit"].toInt(), 50);
            QCOMPARE(request["scope"].toString(), "task7/home");
        }
        emit h.session()->requestFailed(h.requests()[1].toMap()["id"].toUuid(), {SourceErrorKindV2::Network});
        QTRY_VERIFY(!model->data(model->index(a), MusicPageModel::LoadingMoreRole).toBool());
        QCOMPARE(model->section(a).items.size(), 1); QCOMPARE(model->section(b).nextCursor, tokenB);
        h.hub->cancel(3);
        QVERIFY(!model->data(model->index(b), MusicPageModel::LoadingMoreRole).toBool());
        h.session()->setProperty("hold", false);
        // Full refresh restores valid generation after cancellation.
        h.hub->refresh(3); QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        h.hub->loadMore(3, aid);
        QTRY_COMPARE(model->section(a).items.size(), 2);
        QCOMPARE(model->section(b).items.size(), 1);
        const auto sibling = model->itemAt(b, 0); const auto siblingToken = model->section(b).nextCursor;
        h.hub->retrySection(3, aid);
        QTRY_COMPARE(model->section(a).items.size(), 1);
        QCOMPARE(model->itemAt(b, 0), sibling); QCOMPARE(model->section(b).nextCursor, siblingToken);
    }
    void disabledAndRemovedSelectionIsPreserved()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(0); QTRY_COMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Ready);
        QVERIFY(h.registry.disableInstance("task7/home"));
        QTRY_COMPARE(h.hub->recommendation()->error().messageKey, "source.instance.disabled");
        QCOMPARE(h.hub->selectedSourceInstanceId(), "task7/home");
        QCOMPARE(h.hub->recommendation()->rowCount(), 0);
        bool found = false;
        for (const auto &option : h.hub->sourceOptions()) {
            const auto map = option.toMap();
            if (map["sourceInstanceId"].toString() == "task7/home") {
                found = true; QVERIFY(!map["available"].toBool()); QCOMPARE(map["reasonKey"].toString(), "source.instance.disabled");
            }
        }
        QVERIFY(found);
        h.hub->setSelectedSourceInstanceId("task7/missing");
        QCOMPARE(h.hub->selectedSourceInstanceId(), "task7/missing");
        QCOMPARE(h.hub->recommendation()->error().messageKey, "source.instance.removed");
        QCOMPARE(h.hub->sourceOptions().last().toMap()["sourceInstanceId"].toString(), "task7/missing");
    }
    void assetsAreCorrelatedCancellableAndLocal()
    {
        HubHarness h; QVERIFY(h.init());
        const auto ref = item(MediaEntityTypeV2::Track)["ref"].toMap();
        QSignalSpy lyrics(h.hub.get(), &MusicHub::lyricsReady), artwork(h.hub.get(), &MusicHub::artworkReady);
        QSignalSpy failed(h.hub.get(), &MusicHub::assetFailed);
        const auto a = h.hub->loadLyrics(ref), b = h.hub->loadLyrics(ref);
        QVERIFY(a != b); h.hub->cancelAsset(a);
        QTRY_COMPARE(lyrics.size(), 1); QCOMPARE(lyrics[0][0].toUuid(), b); QCOMPARE(lyrics[0][1].toMap(), ref);
        const auto c = h.hub->loadArtwork(ref); QTRY_COMPARE(artwork.size(), 1);
        QCOMPARE(artwork[0][0].toUuid(), c); QVERIFY(artwork[0][2].toUrl().isLocalFile());
        const auto invalid = h.hub->loadLyrics({}); QCOMPARE(failed.size(), 0);
        const auto cancelledInvalid = h.hub->loadArtwork({}); h.hub->cancelAsset(cancelledInvalid);
        QTRY_COMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), invalid);
        auto forged = ref; forged["sourcePluginId"] = "org.quemusic.source.task7";
        h.hub->loadLyrics(forged); QTRY_COMPARE(failed.size(), 2);
    }
    void browseIsSourceBoundAndBackRestoresQueries()
    {
        HubHarness h; QVERIFY(h.init());
        const QList<MediaEntityTypeV2> types{MediaEntityTypeV2::Album, MediaEntityTypeV2::Artist,
            MediaEntityTypeV2::Playlist, MediaEntityTypeV2::Genre};
        const QStringList keys{"albumId", "artistId", "playlistId", "genre"};
        for (int i = 0; i < types.size(); ++i) {
            QVERIFY(h.hub->browse(item(types[i])));
            QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
            QCOMPARE(h.hub->selectedSourceInstanceId(), QString{});
            QCOMPARE(h.hub->categoryContext()["sourceInstanceId"].toString(), "task7/home");
            const auto query = h.requests().last().toMap();
            QCOMPARE(query["filters"].toMap(), QVariantMap({{keys[i], "entity"}}));
            QCOMPARE(query["section"].toInt(), i == 1 ? 7 : 8);
            const auto id = h.hub->category()->section(0).sectionId;
            h.hub->loadMore(1, id);
            QTRY_COMPARE(h.hub->category()->section(0).items.size(), 2);
            QCOMPARE(h.requests().last().toMap()["filters"], query["filters"]);
        }
        QVERIFY(h.hub->navigateBack()); QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests().last().toMap()["filters"].toMap(), QVariantMap({{"playlistId", "entity"}}));
        const auto context = h.hub->categoryContext();
        QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Track)));
        QVERIFY(h.hub->browse(item(MediaEntityTypeV2::Directory)));
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests().last().toMap()["filters"].toMap(),
                 QVariantMap({{"directoryId", "entity"}}));
        QVERIFY(h.hub->navigateBack());
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        auto foreign = item(MediaEntityTypeV2::Album); auto ref = foreign["ref"].toMap(); ref["accountId"] = "office"; foreign["ref"] = ref;
        QVERIFY(!h.hub->browse(foreign)); QCOMPARE(h.hub->categoryContext(), context);
        h.hub->setSelectedSourceInstanceId("task7/office");
        QVERIFY(h.hub->categoryContext().isEmpty()); QVERIFY(!h.hub->canNavigateBack()); QVERIFY(!h.hub->navigateBack());
        QVERIFY(!h.hub->browse(item(MediaEntityTypeV2::Album))); // Late home delegate under office scope.
        QVERIFY(h.hub->categoryContext().isEmpty());
        QTRY_COMPARE(h.hub->category()->state(), PageLoadStateV2::Ready);
        for (const auto &request : h.requests("office")) QVERIFY(request.toMap()["filters"].toMap().isEmpty());
    }
    void categoryRequestsStandardPlaylists()
    {
        HubHarness h; QVERIFY(h.init());
        h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->activatePage(1);
        QTRY_COMPARE(h.requests().size(), 5);
        bool requestedPlaylists = false;
        for (const auto &value : h.requests()) {
            const auto request = value.toMap();
            if (request.value("section").toInt() == int(PageSectionKindV2::Playlists)) {
                requestedPlaylists = true;
                QVERIFY(request.value("filters").toMap().isEmpty());
                QCOMPARE(request.value("scope").toString(), QStringLiteral("task7/home"));
            }
        }
        QVERIFY(requestedPlaylists);
    }
    void hubDestructionPreservesBorrowedSession()
    {
        HubHarness h; QVERIFY(h.init()); auto *session = h.session();
        session->setProperty("hold", true); h.hub->search("pending");
        QTRY_COMPARE(h.requests().size(), 1);
        QPointer<IMusicSourceSessionV2> guard = session;
        h.hub.reset(); QVERIFY(guard); QCOMPARE(guard->parent(), &h.registry);
        emit guard->pageReady(h.requests().last().toMap()["id"].toUuid(), {});
        QCoreApplication::processEvents();
    }
    void hostTerminalDedupAndPreviewsDoNotCompleteQueries()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.session()->setProperty("hold", true);
        auto *repo = h.hub->findChild<PageRepository *>(); QVERIFY(repo);
        QSignalSpy terminals(repo, &PageRepository::pageReady);
        h.hub->activatePage(0); QTRY_COMPARE(h.requests().size(), 5);
        auto *model = h.hub->recommendation();
        auto providerRequests = h.requests();
        emit h.session()->pageReady(providerRequests[0].toMap()["id"].toUuid(), {});
        QTRY_COMPARE(terminals.size(), 1);
        const auto hostId = terminals[0][0].toUuid(); const auto generation = terminals[0][1].toULongLong();
        // Repositories normally deduplicate provider emissions. Exercise the hub's
        // independent host-ID guard using a real completed repository request ID.
        emit repo->pageReady(hostId, generation, {});
        emit repo->pageFailed(hostId, generation, {SourceErrorKindV2::Network});
        QCOMPARE(model->state(), PageLoadStateV2::Loading);
        for (int i = 1; i < 4; ++i)
            emit h.session()->pageReady(providerRequests[i].toMap()["id"].toUuid(), {});
        QTRY_COMPARE(terminals.size(), 5); // first + duplicate + three real requests
        QCOMPARE(model->state(), PageLoadStateV2::Loading);
        emit h.session()->pageReady(providerRequests[4].toMap()["id"].toUuid(), {});
        QTRY_COMPARE(model->state(), PageLoadStateV2::Empty);
        // A same-query cached preview must leave the page Loading until network.
        h.hub->refresh(0); QTRY_COMPARE(h.requests().size(), 10);
        QTRY_VERIFY(terminals.size() > 6);
        QCOMPARE(model->state(), PageLoadStateV2::Loading);
        h.hub->cancel(0); QCOMPARE(model->state(), PageLoadStateV2::Idle);
    }
    void reentrantCancelAtLoadingPreventsDispatch()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        auto *session = h.session();
        bool cancelled = false;
        connect(h.hub->searchResults(), &MusicPageModel::stateChanged, h.hub.get(), [&] {
            if (!cancelled && h.hub->searchResults()->state() == PageLoadStateV2::Loading) {
                cancelled = true; h.hub->cancel(3);
            }
        });
        h.hub->search("cancel immediately");
        QTest::qWait(80);
        QVERIFY(cancelled); QCOMPARE(h.hub->searchResults()->state(), PageLoadStateV2::Idle);
        QCOMPARE(session->property("requests").toList().size(), 0);
    }
    void reentrantScopeChangeDoesNotDispatchTheOldSearch()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        auto *home = h.session(); bool switched = false;
        connect(h.hub->searchResults(), &MusicPageModel::stateChanged, h.hub.get(), [&] {
            if (!switched && h.hub->searchResults()->state() == PageLoadStateV2::Loading) {
                switched = true; h.hub->setSelectedSourceInstanceId("task7/office");
            }
        });
        h.hub->search("current");
        QTRY_COMPARE(h.hub->searchResults()->state(), PageLoadStateV2::Ready);
        QCOMPARE(home->property("requests").toList().size(), 0);
        QCOMPARE(h.requests("office").size(), 1);
        QCOMPARE(h.hub->searchResults()->itemAt(0, 0)["ref"].toMap()["accountId"].toString(), "office");
    }
    void retainedRowsCancelDuringRefreshDataChanged()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.hub->search("accepted");
        auto *model = h.hub->searchResults();
        QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        const auto accepted = model->itemAt(0, 0);
        const int before = h.requests().size();
        bool cancelled = false;
        connect(model, &MusicPageModel::dataChanged, h.hub.get(), [&] {
            // invalidate() also notifies rows; cached identifies beginRequest's
            // retained-row notification, before any provider can be dispatched.
            if (!cancelled && model->cached()) { cancelled = true; h.hub->cancel(3); }
        });
        h.hub->refresh(3);
        QVERIFY(cancelled);
        QCOMPARE(model->state(), PageLoadStateV2::Ready);
        QCOMPARE(model->itemAt(0, 0), accepted);
        QCoreApplication::processEvents();
        QCOMPARE(h.requests().size(), before);
    }
    void queuedAssetSuccessCanStillBeCancelled()
    {
        HubHarness h; QVERIFY(h.init());
        auto *assets = h.hub->findChild<MediaAssetRepository *>(); QVERIFY(assets);
        QSignalSpy ready(h.hub.get(), &MusicHub::lyricsReady);
        QSignalSpy failed(h.hub.get(), &MusicHub::assetFailed);
        bool received = false;
        connect(assets, &MediaAssetRepository::lyricsReady, h.hub.get(), [&](QUuid id) {
            received = true; h.hub->cancelAsset(id);
        });
        h.hub->loadLyrics(item(MediaEntityTypeV2::Track)["ref"].toMap());
        QTRY_VERIFY(received); QCoreApplication::processEvents();
        QCOMPARE(ready.size(), 0); QCOMPARE(failed.size(), 0);
    }
    void browseRejectsMalformedAvailabilityWithoutChangingContext()
    {
        HubHarness h; QVERIFY(h.init());
        auto malformed = item(MediaEntityTypeV2::Album);
        malformed["availableActions"] = QVariantMap{{"4", QVariantMap{{"state", 1}, {"constraints", "not-a-map"}}}};
        QVERIFY(!h.hub->browse(malformed));
        QVERIFY(h.hub->categoryContext().isEmpty());
        QCOMPARE(h.hub->category()->state(), PageLoadStateV2::Idle);
    }
    void initialPartialFailureRetriesOnlyItsSection()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.session()->setProperty("hold", true); h.hub->activatePage(0);
        QTRY_COMPARE(h.requests().size(), 5);
        const auto requests = h.requests();
        auto *repo = h.hub->findChild<PageRepository *>(); QVERIFY(repo);
        QSignalSpy failed(repo, &PageRepository::pageFailed);
        emit h.session()->requestFailed(requests[0].toMap()["id"].toUuid(), {SourceErrorKindV2::Network});
        QTRY_COMPARE(failed.size(), 1);
        emit repo->pageFailed(failed[0][0].toUuid(), failed[0][1].toULongLong(), {SourceErrorKindV2::Network});
        PageSectionV2 section; section.sectionId = "success"; section.kind = PageSectionKindV2::FrequentlyPlayed;
        section.hasMore = true; section.nextCursor = "sibling-provider-cursor";
        MediaItemV2 song; song.ref = {"task7", "task7/home", "home", MediaEntityTypeV2::Track, "one"};
        song.title = "accepted"; section.items = {song};
        emit h.session()->pageReady(requests[1].toMap()["id"].toUuid(), {{section}, {}, false, true});
        for (int i : {2, 3}) emit h.session()->pageReady(requests[i].toMap()["id"].toUuid(), {});
        QTRY_VERIFY(rowFor(h.hub->recommendation(), PageSectionKindV2::FrequentlyPlayed) >= 0);
        QCOMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Loading);
        emit h.session()->pageReady(requests[4].toMap()["id"].toUuid(), {});
        QTRY_COMPARE(h.hub->recommendation()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.hub->recommendation()->error().kind, SourceErrorKindV2::Network);
        auto *model = h.hub->recommendation();
        const int target = rowFor(model, PageSectionKindV2::RecentlyPlayed);
        QVERIFY2(target >= 0, "First-load failed query must expose a retryable section");
        QCOMPARE(model->rowCount(), 2);
        const auto targetId = model->section(target).sectionId;
        QVERIFY(!model->data(model->index(target), MusicPageModel::ErrorRole).toMap().isEmpty());
        const int sibling = rowFor(model, PageSectionKindV2::FrequentlyPlayed);
        const auto siblingItem = model->itemAt(sibling, 0);
        const auto siblingCursor = model->section(sibling).nextCursor;
        h.hub->retrySection(0, targetId); h.hub->retrySection(0, targetId);
        QTRY_COMPARE(h.requests().size(), 6);
        const auto retry = h.requests().last().toMap();
        QCOMPARE(retry["section"].toInt(), int(PageSectionKindV2::RecentlyPlayed));
        QCOMPARE(retry["scope"].toString(), "task7/home");
        QVERIFY(retry["cursor"].toString().isEmpty());
        auto recovered = section;
        recovered.sectionId = "provider-recovered"; recovered.kind = PageSectionKindV2::RecentlyPlayed;
        recovered.nextCursor = "retry-provider-cursor";
        emit h.session()->pageReady(retry["id"].toUuid(), {{recovered}, {}, false, true});
        QTRY_COMPARE(model->section(target).items.size(), 1);
        QCOMPARE(model->section(target).sectionId, targetId);
        QVERIFY(model->data(model->index(target), MusicPageModel::ErrorRole).toMap().isEmpty());
        QVERIFY(model->errorMap().isEmpty());
        QCOMPARE(model->itemAt(sibling, 0), siblingItem);
        QCOMPARE(model->section(sibling).nextCursor, siblingCursor);
        h.hub->loadMore(0, targetId);
        QTRY_COMPARE(h.requests().size(), 7);
        QCOMPARE(h.requests().last().toMap()["cursor"].toString(), "retry-provider-cursor");
        recovered.hasMore = false; recovered.nextCursor.clear();
        emit h.session()->pageReady(h.requests().last().toMap()["id"].toUuid(), {{recovered}, {}, false, true});
        QTRY_COMPARE(model->section(target).items.size(), 2);
        QCOMPARE(model->section(sibling).nextCursor, siblingCursor);
    }
    void initialAllFailuresHaveTargetedRetry()
    {
        HubHarness h; QVERIFY(h.init()); h.hub->setSelectedSourceInstanceId("task7/home");
        h.session()->setProperty("fail", true);
        h.hub->activatePage(2); QTRY_COMPARE(h.hub->favorites()->state(), PageLoadStateV2::Failed);
        QCOMPARE(h.hub->favorites()->rowCount(), 4);
        const int favorite = rowFor(h.hub->favorites(), PageSectionKindV2::FavoriteTracks);
        QVERIFY(favorite >= 0);
        const auto favoriteId = h.hub->favorites()->section(favorite).sectionId;
        const int beforeRetry = h.requests().size();
        h.session()->setProperty("fail", false);
        h.hub->retrySection(2, favoriteId);
        QTRY_COMPARE(h.hub->favorites()->state(), PageLoadStateV2::Ready);
        QCOMPARE(h.requests().size(), beforeRetry + 1);
        QCOMPARE(h.requests().last().toMap()["section"].toInt(), int(PageSectionKindV2::FavoriteTracks));
        QCOMPARE(h.hub->favorites()->section(favorite).items.size(), 1);
        QCOMPARE(h.hub->favorites()->section(favorite).sectionId, favoriteId);
        QCOMPARE(h.hub->favorites()->rowCount(), 4);
        QVERIFY(h.hub->favorites()->data(h.hub->favorites()->index(favorite), MusicPageModel::ErrorRole).toMap().isEmpty());
        QVERIFY(!h.hub->favorites()->errorMap().isEmpty()); // Remaining failed queries retain diagnostics.
    }
};
QTEST_GUILESS_MAIN(MusicHubTest)
#endif
#include "tst_MusicHub.moc"
