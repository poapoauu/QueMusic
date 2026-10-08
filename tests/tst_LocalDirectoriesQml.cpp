#include "OnlineListModel.h"
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSet>
#include <QTest>

class DirectoryQmlApi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *directoryItems READ directoryItems CONSTANT)
    Q_PROPERTY(QString directoryState READ directoryState NOTIFY directoryChanged)
    Q_PROPERTY(bool directoryCanNavigateBack READ directoryCanNavigateBack NOTIFY directoryChanged)
public:
    DirectoryQmlApi() {
        rows.setItems({QVariantMap{{"title", "Music"}, {"entityType", 5},
                                    {"settingsPackageId", "org.quemusic.source.local"},
                                    {"settingsInstanceId", "local/home"},
                                    {"_adapterKey", 1ULL}}});
    }
    QObject *directoryItems() { return &rows; }
    QString directoryState() const { return state; }
    bool directoryCanNavigateBack() const { return canBack; }
    Q_INVOKABLE void activateDirectories() { ++activations; }
    Q_INVOKABLE bool browseDirectory(const QVariantMap &) {
        ++browses;
        canBack = true;
        rows.setItems({QVariantMap{{"title", "Song"}, {"entityType", 0},
                                    {"_adapterKey", 2ULL}}});
        emit directoryChanged();
        return true;
    }
    Q_INVOKABLE bool directoryBack() {
        ++backs;
        canBack = false;
        rows.setItems({QVariantMap{{"title", "Music"}, {"entityType", 5},
                                    {"settingsPackageId", "org.quemusic.source.local"},
                                    {"settingsInstanceId", "local/home"},
                                    {"_adapterKey", 1ULL}}});
        emit directoryChanged();
        return true;
    }
    Q_INVOKABLE QUuid play(const QVariantMap &) { ++plays; return QUuid::createUuid(); }
    Q_INVOKABLE QUuid enqueue(const QVariantMap &) { ++enqueues; return QUuid::createUuid(); }
    Q_INVOKABLE QVariantMap capabilities(const QVariantMap &row) const {
        return {{"canBrowse", row.value("canBrowse", row.value("entityType").toInt() == 5)},
                {"canPlay", row.value("canPlay", row.value("entityType").toInt() == 0)},
                {"canEnqueue", row.value("canEnqueue", row.value("entityType").toInt() == 0)}};
    }
    Q_INVOKABLE void refreshDirectories() { ++refreshes; }
    Q_INVOKABLE void retryDirectorySection(const QString &id) {
        retriedSections.append(id);
        // Simulate synchronous state changes while QML walks its snapshot.
        rows.setPresentationState({{"error", QVariantMap{{"failed", QVariantMap{{"failed", true}}}}}});
    }
    Q_INVOKABLE void loadMoreDirectories(const QString &sectionId) {
        if (!requestedSections.contains(sectionId)) {
            requestedSections.insert(sectionId);
            ++more;
        }
    }
    Q_INVOKABLE bool pluginAvailable(const QString &) const { return installed; }
    OnlineListModel rows;
    QString state = "ready";
    bool canBack = false;
    bool installed = true;
    QSet<QString> requestedSections;
    QStringList retriedSections;
    int activations = 0, browses = 0, backs = 0, plays = 0, enqueues = 0, refreshes = 0, more = 0;
signals:
    void directoryChanged();
};

class DirectoryQmlMusicApi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int songSource MEMBER songSource NOTIFY songSourceChanged)
    Q_PROPERTY(bool loadState MEMBER loadState)
public:
    int songSource = 0;
    bool loadState = false;
signals:
    void songSourceChanged();
};

class DirectoryQmlWindow final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int exitIndex MEMBER exitIndex)
public:
    int exitIndex = 0;
    int legacyPlays = 0;
    Q_INVOKABLE void playLocalSong(const QUrl &, const QString &) { ++legacyPlays; }
signals:
    void exit();
};

class FakeLegacyCollectionMigration final : public QObject {
    Q_OBJECT
public:
    int runs = 0;
    Q_INVOKABLE QVariantMap preview() const { return {{"folderCount", 1}, {"songCount", 3}}; }
    Q_INVOKABLE QVariantList candidates() const {
        return {QVariantMap{{"instanceId", "local/one"}, {"displayName", "One"}},
                QVariantMap{{"instanceId", "local/two"}, {"displayName", "Two"}}};
    }
    Q_INVOKABLE QVariantMap run(const QString &) { ++runs; return {{"committed", true}}; }
};

class LocalDirectoriesQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void localBranchUsesAdapterOnly()
    {
        QQmlEngine engine;
        DirectoryQmlApi adapter;
        DirectoryQmlMusicApi musicApi;
        DirectoryQmlWindow window;
        FakeLegacyCollectionMigration migration;
        QQuickItem mainLayout;
        OnlineListModel myFolders;
        auto *context = engine.rootContext();
        context->setContextProperty("MusicApi", &musicApi);
        context->setContextProperty("window", &window);
        context->setContextProperty("mainLayout", &mainLayout);
        context->setContextProperty("myFolderModel", &myFolders);
        context->setContextProperty("legacyCollectionMigration", &migration);
        context->setContextProperty("Style", QVariantMap{
            {"themes", QVariantMap{{"fontColor", "#202020"}, {"containColor", "#eeeeee"},
                                     {"fullColor", "#ffffff"}, {"sideColor", "#eeeeee"},
                                     {"hoverColor", "#dddddd"}, {"textColor", "#333333"},
                                     {"primaryColor", "#ffffff"}, {"themeColor", "#4488ff"}}},
            {"settings", QVariantMap{{"pageTitle", 20}, {"labelRadius", 10},
                                      {"textmain", 14}, {"texticon", 16}}}});
        context->setContextProperty("iconFont", QVariantMap{{"name", QString{}}});
        QQuickWindow preview;
        preview.resize(900, 600);
        QQmlComponent component(&engine, QUrl("qrc:/QueMusic/tests/qml/tst_LocalDirectories.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> page(component.createWithInitialProperties({{"directoryAdapter", QVariant::fromValue(&adapter)}}));
        QVERIFY2(page, qPrintable(component.errorString()));
        qobject_cast<QQuickItem *>(page.get())->setParentItem(preview.contentItem());
        preview.show();
        QTRY_VERIFY(preview.isVisible());
        auto *filePage = page->findChild<QObject *>("filePageUnderTest");
        QVERIFY(filePage);
        QVariant blocked;
        QVERIFY(QMetaObject::invokeMethod(filePage, "legacySongBlocked",
            Q_RETURN_ARG(QVariant, blocked), Q_ARG(QVariant, QVariant(QStringLiteral("noMatch")))));
        QVERIFY(blocked.toBool());
        QVERIFY(QMetaObject::invokeMethod(filePage, "legacySongBlocked",
            Q_RETURN_ARG(QVariant, blocked), Q_ARG(QVariant, QVariant(QStringLiteral("notMigrated")))));
        QVERIFY(blocked.toBool());
        QVERIFY(QMetaObject::invokeMethod(filePage, "legacySongBlocked",
            Q_RETURN_ARG(QVariant, blocked), Q_ARG(QVariant, QVariant(QStringLiteral("matched")))));
        QVERIFY(!blocked.toBool());
        auto *migrationButton = filePage->findChild<QObject *>("legacyCollectionMigrationOpen");
        auto *migrationDialog = filePage->findChild<QObject *>("legacyCollectionMigrationConfirm");
        auto *migrationChoice = filePage->findChild<QObject *>("legacyCollectionMigrationInstance");
        QVERIFY(migrationButton && migrationDialog && migrationChoice);
        QVERIFY(QMetaObject::invokeMethod(migrationButton, "clicked"));
        QTRY_VERIFY(migrationDialog->property("visible").toBool());
        QCOMPARE(migrationChoice->property("currentIndex").toInt(), -1);
        QCOMPARE(migration.runs, 0);
        QVERIFY(QMetaObject::invokeMethod(migrationDialog, "close"));
        auto *tabs = filePage->findChild<QObject *>("localDirectoryTabs");
        QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabChange", Q_ARG(int, 1)));
        QCOMPARE(adapter.activations, 1);
        auto *view = filePage->findChild<QObject *>("pluginDirectoryRoots");
        QVERIFY(view);
        QCOMPARE(view->property("model").value<QObject *>(), adapter.directoryItems());
        QSignalSpy requested(filePage, SIGNAL(requestPluginSettings(QString,QString)));
        QVERIFY(requested.isValid());
        auto *importButton = filePage->findChild<QObject *>("importPluginDirectory");
        QVERIFY(importButton);
        QVERIFY(QMetaObject::invokeMethod(importButton, "clicked"));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toString(), QString("org.quemusic.source.local"));
        QCOMPARE(requested.first().at(1).toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(view, "manageRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(1).toString(), QString("local/home"));
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.browses, 1);
        auto *contents = filePage->findChild<QObject *>("pluginDirectoryContents");
        QVERIFY(contents);
        QCOMPARE(contents->property("model").value<QObject *>(), adapter.directoryItems());
        QVERIFY(QMetaObject::invokeMethod(contents, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.plays, 1);
        QVERIFY(QMetaObject::invokeMethod(contents, "enqueueRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.enqueues, 1);
        auto *back = filePage->findChild<QObject *>("pluginDirectoryBack");
        QVERIFY(back);
        QVERIFY(QMetaObject::invokeMethod(back, "clicked"));
        QCOMPARE(adapter.backs, 1);
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(adapter.canBack);
        emit window.exit();
        QCOMPARE(adapter.backs, 2);
        QVERIFY(!adapter.canBack);
        adapter.rows.setItems({QVariantMap{{"title", "目录加载失败"}, {"isError", true},
                                           {"error", QVariantMap{{"code", "network"}}}}});
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.browses, 2);
        QVERIFY(!adapter.canBack);
        adapter.rows.setItems({QVariantMap{{"title", "A"}, {"entityType", 5},
                                           {"sectionId", "directory/a"}, {"hasMore", true}},
                               QVariantMap{{"title", "B"}, {"entityType", 5},
                                           {"sectionId", "directory/b"}, {"hasMore", true}}});
        adapter.rows.setPresentationState({{"hasMore", true},
            {"paginationSectionIds", QStringList{"directory/a", "directory/b"}}});
        QVERIFY(QMetaObject::invokeMethod(view, "loadMoreVisibleSections"));
        QCOMPARE(adapter.more, 2);
        adapter.more = 0;
        adapter.requestedSections.clear();
        musicApi.loadState = true; // The legacy list gate must not block Directory pagination.
        emit adapter.directoryChanged();
        QTRY_COMPARE(adapter.more, 2);
        musicApi.loadState = false;
        adapter.rows.setItems({QVariantMap{{"title", "目录加载失败"}, {"isError", true}}});
        adapter.rows.setPresentationState({{"error", QVariantMap{{"directory/failed", QVariantMap{{"failed", true}}}}},
            {"retrySectionIds", QStringList{"directory/failed", "directory/second"}}});
        emit adapter.directoryChanged();
        auto *retry = filePage->findChild<QObject *>("directoryRetry");
        QVERIFY(retry);
        QTRY_VERIFY(retry->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(adapter.retriedSections, QStringList({"directory/failed", "directory/second"}));
        QCOMPARE(adapter.refreshes, 0);
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(adapter.retriedSections.size(), 2);
        // Paging/retry state survives even when the provider returns no rows.
        adapter.rows.setItems({});
        adapter.rows.setPresentationState({{"error", QVariantMap{{"directory/empty", QVariantMap{{"failed", true}}}}},
            {"retrySectionIds", QStringList{"directory/empty"}}});
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(adapter.retriedSections.last(), QString("directory/empty"));
        adapter.state = "loading"; emit adapter.directoryChanged();
        adapter.rows.setPresentationState({{"hasMore", true},
            {"paginationSectionIds", QStringList{"directory/loading"}},
            {"retrySectionIds", QStringList{"directory/loading"}}});
        QVERIFY(QMetaObject::invokeMethod(view, "loadMoreVisibleSections"));
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(adapter.more, 2);
        QCOMPARE(adapter.retriedSections.size(), 3);
        adapter.state = "failed"; adapter.rows.setPresentationState({}); emit adapter.directoryChanged();
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(adapter.refreshes, 1); // No origin: retain whole-page recovery.
        adapter.state = "ready"; emit adapter.directoryChanged();
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(QMetaObject::invokeMethod(view, "manageRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.browses, 2); QCOMPARE(requested.count(), 2);
        adapter.rows.setItems({QVariantMap{{"title", "Unavailable directory"}, {"entityType", 5}, {"canBrowse", false}}});
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.browses, 2);
        adapter.rows.setItems({QVariantMap{{"title", "Music"}, {"entityType", 5}}});
        QVERIFY(QMetaObject::invokeMethod(view, "activateRow", Q_ARG(QVariant, QVariant(0))));
        contents = filePage->findChild<QObject *>("pluginDirectoryContents");
        QVERIFY(contents);
        adapter.rows.setItems({QVariantMap{{"title", "Unavailable track"}, {"entityType", 0},
            {"canPlay", false}, {"canEnqueue", false}}});
        QVERIFY(QMetaObject::invokeMethod(contents, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(QMetaObject::invokeMethod(contents, "enqueueRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.plays, 1); QCOMPARE(adapter.enqueues, 1);
        adapter.rows.setItems({});
        QVERIFY(QMetaObject::invokeMethod(contents, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(QMetaObject::invokeMethod(contents, "enqueueRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(filePage->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
        QVERIFY(QMetaObject::invokeMethod(contents, "activateRow", Q_ARG(QVariant, QVariant(0))));
        QVERIFY(QMetaObject::invokeMethod(contents, "enqueueRow", Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.plays, 1); QCOMPARE(adapter.enqueues, 1);
        QVERIFY(filePage->setProperty("musicAdapter", QVariant::fromValue<QObject *>(&adapter)));
        QCOMPARE(window.legacyPlays, 0);
        myFolders.setItems({QVariantMap{{"name", "Personal collection"}}});
        QCOMPARE(myFolders.rowCount(), 1);
        adapter.rows.setItems({});
        auto *status = filePage->findChild<QObject *>("directoryStatus");
        QVERIFY(status);
        adapter.state = "loading";
        emit adapter.directoryChanged();
        QCOMPARE(status->property("text").toString(), QString("正在加载目录…"));
        adapter.state = "failed";
        emit adapter.directoryChanged();
        QCOMPARE(status->property("text").toString(), QString("目录加载失败，请重试"));
        adapter.state = "empty";
        emit adapter.directoryChanged();
        QCOMPARE(status->property("text").toString(), QString("暂无目录，点击导入目录进行配置"));
        QCOMPARE(myFolders.rowCount(), 1);
        adapter.installed = false;
        emit adapter.directoryChanged();
        QCOMPARE(status->property("text").toString(), QString("本地音乐插件不可用"));
        if (qEnvironmentVariableIsSet("QUEMUSIC_TASK8_SCREENSHOT_PATH")) {
            adapter.installed = true;
            adapter.state = "ready";
            adapter.rows.setItems({QVariantMap{{"title", "Music"}, {"entityType", 5},
                                                {"settingsPackageId", "org.quemusic.source.local"},
                                                {"settingsInstanceId", "local/home"},
                                                {"_adapterKey", 1ULL}}});
            emit adapter.directoryChanged();
            const QImage image = preview.grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.save(qEnvironmentVariable("QUEMUSIC_TASK8_SCREENSHOT_PATH")));
        }
    }
};
QTEST_MAIN(LocalDirectoriesQmlTest)
#include "tst_LocalDirectoriesQml.moc"
