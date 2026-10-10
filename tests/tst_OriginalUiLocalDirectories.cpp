#include "MusicHub.h"
#include "DirectoryLibraryController.h"
#include "OnlineListModel.h"
#include "OriginalUiMusicAdapter.h"
#include "SourceAccountStore.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class LocalDirectorySecrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return QByteArray{}; }
    bool remove(const QString &, QString *) override { return true; }
};

class OriginalUiLocalDirectoriesTest final : public QObject {
    Q_OBJECT
private slots:
    void directoryStateDoesNotDependOnVisibleRowsOrExposeDiagnostics()
    {
        QTemporaryDir files; QVERIFY(files.isValid());
        QSettings settings(files.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        QCOMPARE(adapter.directoryContextToken(), hub.directoryLibrary()->contextToken());
        QCOMPARE(adapter.property("directoryContextToken").toString(), adapter.directoryContextToken());
        auto *model = hub.directoryLibrary()->model();
        const auto generation = model->beginRequest();
        PageResultV2 result;
        PageSectionV2 section; section.sectionId = "exhausted"; section.kind = PageSectionKindV2::Tracks;
        MediaItemV2 directory; directory.ref = {"local", "local/home", "home", MediaEntityTypeV2::Directory, "opaque-root"};
        directory.title = "Music";
        section.items = {directory}; result.sections.append(section);
        section.sectionId = "empty-next"; section.items.clear(); section.hasMore = true; section.nextCursor = "private-cursor";
        result.sections.append(section);
        QVERIFY(model->applyResult(generation, result));
        section.sectionId = "failed"; section.hasMore = false; section.nextCursor.clear();
        QVERIFY(model->applyQueryFailure(generation, section, {SourceErrorKindV2::Network, "network", "private diagnostic"}));
        section.sectionId = "unsupported";
        QVERIFY(model->applyQueryFailure(generation, section, {SourceErrorKindV2::Unsupported}));
        QVERIFY(model->finishGeneration(generation, 3));
        auto *view = adapter.directoryItems();
        QCOMPARE(view->rowCount(), 2); // One real directory and one safe error placeholder.
        QCOMPARE(view->paginationSectionIds(), QStringList{"empty-next"});
        QCOMPARE(view->retrySectionIds(), QStringList{"failed"});
        QCOMPARE(view->error(), QVariantMap({{"failed", QVariantMap{{"failed", true}}}}));
        for (int i = 0; i < view->rowCount(); ++i) {
            const auto row = view->get(i);
            QVERIFY(!row.contains("ref")); QVERIFY(!row.contains("path"));
            const auto error = row.value("error").toMap();
            QVERIFY(error.isEmpty() || error == QVariantMap({{"failed", true}}));
        }
        QVERIFY(model->beginSectionRequest(generation, "exhausted"));
        const auto token = adapter.directoryContextToken();
        QVERIFY(model->applySectionFailure(generation, "exhausted", {SourceErrorKindV2::Network, "network", "private details"}));
        QCOMPARE(view->get(0).value("error").toMap(), QVariantMap({{"failed", true}}));
        QCOMPARE(adapter.directoryContextToken(), token);
        QVERIFY(model->beginSectionRequest(generation, "empty-next"));
        QVERIFY(view->paginationSectionIds().isEmpty());
        QVERIFY(view->loadingMore());
        QVERIFY(model->resetGeneration(generation));
        QCOMPARE(view->rowCount(), 0);
        QVERIFY(view->paginationSectionIds().isEmpty()); QVERIFY(view->retrySectionIds().isEmpty());
        const auto unsupportedGeneration = model->beginRequest();
        QVERIFY(model->applyQueryFailure(unsupportedGeneration, section, {SourceErrorKindV2::Unsupported}));
        QVERIFY(model->finishGeneration(unsupportedGeneration, 1));
        QCOMPARE(adapter.directoryState(), QString("empty"));
        QCOMPARE(view->rowCount(), 0); QVERIFY(view->error().isEmpty());
    }
    void unsupportedInstanceDoesNotAppearAsBrokenDirectory()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        QSettings settings(files.filePath("accounts.ini"), QSettings::IniFormat);
        LocalDirectorySecrets secrets;
        SourceAccountStore accounts(&settings, &secrets);
        PluginManager plugins;
        plugins.addSearchPath(QUEMUSIC_TASK7_PACKAGES);
        QCOMPARE(plugins.discover(), 1);
        QVERIFY(plugins.load("org.quemusic.source.task7"));
        QVERIFY(accounts.saveResolvedV2({"task7", "home", "Home", {}, {}}));
        QVERIFY(accounts.saveResolvedV2({"task7", "office", "Office", {}, {}}));
        SourceRegistry registry(&plugins, &accounts);
        QVERIFY(registry.sessionFor("task7/office"));
        registry.sessionFor("task7/office")->setProperty("unsupported", true);
        SourceScopeStore scope(&settings);
        MusicHub hub(&registry, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        adapter.activateDirectories();
        QTRY_COMPARE(hub.directoryLibrary()->model()->state(), PageLoadStateV2::Ready);
        QTRY_COMPARE(adapter.directoryItems()->rowCount(), 1);
        QVERIFY(!adapter.directoryItems()->get(0).value("isError").toBool());
    }
    void failingInstanceRemainsVisibleBesideHealthyRoot()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        QSettings settings(files.filePath("accounts.ini"), QSettings::IniFormat);
        LocalDirectorySecrets secrets;
        SourceAccountStore accounts(&settings, &secrets);
        PluginManager plugins;
        plugins.addSearchPath(QUEMUSIC_TASK7_PACKAGES);
        QCOMPARE(plugins.discover(), 1);
        QVERIFY(plugins.load("org.quemusic.source.task7"));
        QVERIFY(accounts.saveResolvedV2({"task7", "home", "Home", {}, {}}));
        QVERIFY(accounts.saveResolvedV2({"task7", "office", "Office", {}, {}}));
        SourceRegistry registry(&plugins, &accounts);
        QVERIFY(registry.sessionFor("task7/office"));
        registry.sessionFor("task7/office")->setProperty("fail", true);
        SourceScopeStore scope(&settings);
        MusicHub hub(&registry, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        adapter.activateDirectories();
        QTRY_COMPARE(hub.directoryLibrary()->model()->state(), PageLoadStateV2::Ready);
        QTRY_COMPARE(adapter.directoryItems()->rowCount(), 2);
        int errors = 0, roots = 0;
        for (int i = 0; i < adapter.directoryItems()->rowCount(); ++i) {
            const auto row = adapter.directoryItems()->get(i);
            if (row.value("isError").toBool()) ++errors;
            else ++roots;
        }
        QCOMPARE(errors, 1);
        QCOMPARE(roots, 1);
    }
    void opaqueRowsNavigateWithoutChangingOnlineCategory()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        QSettings settings(files.filePath("accounts.ini"), QSettings::IniFormat);
        LocalDirectorySecrets secrets;
        SourceAccountStore accounts(&settings, &secrets);
        PluginManager plugins;
        plugins.addSearchPath(QUEMUSIC_TASK7_PACKAGES);
        QCOMPARE(plugins.discover(), 1);
        QVERIFY(plugins.load("org.quemusic.source.task7"));
        QVERIFY(accounts.saveResolvedV2({"task7", "home", "Home", {}, {}}));
        QVERIFY(accounts.saveResolvedV2({"task7", "office", "Office", {}, {}}));
        SourceRegistry registry(&plugins, &accounts);
        SourceScopeStore scope(&settings);
        MusicHub hub(&registry, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        bool fixtureLoaded = false;
        QVERIFY(QMetaObject::invokeMethod(&adapter, "pluginAvailable",
                                          Q_RETURN_ARG(bool, fixtureLoaded),
                                          Q_ARG(QString, QString("org.quemusic.source.task7"))));
        QVERIFY(fixtureLoaded);
        bool localLoaded = true;
        QVERIFY(QMetaObject::invokeMethod(&adapter, "pluginAvailable",
                                          Q_RETURN_ARG(bool, localLoaded),
                                          Q_ARG(QString, QString("org.quemusic.source.local"))));
        QVERIFY(!localLoaded);
        hub.activatePage(int(MusicPageKindV2::Category));
        QTRY_COMPARE(hub.category()->state(), PageLoadStateV2::Ready);
        const int onlineRows = hub.category()->rowCount();

        QVERIFY(QMetaObject::invokeMethod(&adapter, "activateDirectories"));
        auto *rows = qvariant_cast<OnlineListModel *>(adapter.property("directoryItems"));
        QVERIFY(rows);
        QTRY_COMPARE(rows->rowCount(), 2);
        const auto first = rows->get(0);
        QVERIFY(!first.contains("ref"));
        QVERIFY(!first.contains("path"));
        QVERIFY(first.contains("_adapterKey"));
        bool browsed = false;
        QVERIFY(QMetaObject::invokeMethod(&adapter, "browseDirectory",
                                    Q_RETURN_ARG(bool, browsed), Q_ARG(QVariantMap, first)));
        QVERIFY(browsed);
        QTRY_COMPARE(rows->rowCount(), 1);
        QCOMPARE(hub.category()->rowCount(), onlineRows);
        bool staleAccepted = true;
        QVERIFY(QMetaObject::invokeMethod(&adapter, "browseDirectory",
                                    Q_RETURN_ARG(bool, staleAccepted), Q_ARG(QVariantMap, first)));
        QVERIFY(!staleAccepted);
        bool backed = false;
        QVERIFY(QMetaObject::invokeMethod(&adapter, "directoryBack", Q_RETURN_ARG(bool, backed)));
        QVERIFY(backed);
        QTRY_COMPARE(rows->rowCount(), 2);
    }
};
QTEST_MAIN(OriginalUiLocalDirectoriesTest)
#include "tst_OriginalUiLocalDirectories.moc"
