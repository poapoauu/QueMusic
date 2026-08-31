#include "MediaBridge.h"
#include "SourceAccountController.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"
#include "SourceSessionRegistry.h"
#include "SourceStartup.h"

#include <QDir>
#include <QHash>
#include <QQuickItem>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>

namespace {

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        Q_UNUSED(error)
        values.insert(reference, secret);
        return true;
    }

    std::optional<QByteArray> read(const QString &reference, QString *error) const override
    {
        const auto value = values.constFind(reference);
        if (value == values.cend()) {
            if (error != nullptr) {
                *error = QStringLiteral("Secret is unavailable");
            }
            return std::nullopt;
        }
        return *value;
    }

    bool remove(const QString &reference, QString *error) override
    {
        Q_UNUSED(error)
        values.remove(reference);
        return true;
    }

private:
    QHash<QString, QByteArray> values;
};

class NamedObject final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit NamedObject(QString name) : m_name(std::move(name)) {}
    QString name() const { return m_name; }

private:
    QString m_name;
};

QQuickItem *findVisualItemByObjectName(QQuickItem *root, const QString &objectName)
{
    if (root == nullptr) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = findVisualItemByObjectName(child, objectName)) {
            return match;
        }
    }
    return nullptr;
}

class WindowDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int exitIndex MEMBER exitIndex)

public:
    int exitIndex = 0;

signals:
    void exit();
};

class MusicApiDouble final : public QObject {
    Q_OBJECT

public:
    Q_INVOKABLE void getRecommendSongs(int, int) {}
};

class PlaybackBridgeDouble final : public QObject {
    Q_OBJECT

public:
    const QVariantList &playCalls() const { return m_playCalls; }
    Q_INVOKABLE void play(const QVariant &entry) { m_playCalls.append(entry); }

private:
    QVariantList m_playCalls;
};

class AccountControllerDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QString lastError READ lastError CONSTANT)

public:
    QVariantList accounts() const { return m_accounts; }
    QString lastError() const { return {}; }
    void setAccounts(QVariantList accounts)
    {
        m_accounts = std::move(accounts);
        emit accountsChanged();
    }

signals:
    void accountsChanged();

private:
    QVariantList m_accounts;
};

class ResultModelDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int requestState READ requestState CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)
    Q_PROPERTY(bool canRetry READ canRetry CONSTANT)
    Q_PROPERTY(int count READ count CONSTANT)
    Q_PROPERTY(bool hasMore READ hasMore CONSTANT)

public:
    int requestState() const { return 0; }
    QString errorMessage() const { return {}; }
    bool canRetry() const { return false; }
    int count() const { return 0; }
    bool hasMore() const { return false; }
    Q_INVOKABLE QVariant get(int) const { return {}; }
};

class MediaBridgeDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *accountController READ accountController CONSTANT)
    Q_PROPERTY(QObject *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(QObject *browseResults READ browseResults CONSTANT)

public:
    MediaBridgeDouble(QObject *accountController, QObject *results)
        : m_accountController(accountController), m_results(results)
    {
    }

    QObject *accountController() const { return m_accountController; }
    QObject *searchResults() const { return m_results; }
    QObject *browseResults() const { return m_results; }

signals:
    void artworkReady(const QVariant &artwork);
    void mediaActionFailed(const QVariant &error);

private:
    QObject *m_accountController;
    QObject *m_results;
};

class PluginManagerDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)

public:
    QVariantList plugins() const { return m_plugins; }

    void setPlugins(QVariantList plugins)
    {
        m_plugins = std::move(plugins);
        emit pluginsChanged();
    }

    Q_INVOKABLE void discoverPlugins() {}
    Q_INVOKABLE void loadPlugin(const QString &) {}
    Q_INVOKABLE void unloadPlugin(const QString &) {}
    Q_INVOKABLE void reloadPlugin(const QString &) {}

signals:
    void pluginsChanged();

private:
    QVariantList m_plugins;
};

} // namespace

class MediaBridgeQmlTest : public QObject {
    Q_OBJECT

private slots:
    void exposesOnlyBridgeAndAcceptsNormalizedQueueEntry();
    void accountControllerHidesSecretsAndManagesAccountLifecycle();
    void productionLeftSideBarDispatchesSourceLibrary();
    void productionPlayListExposesClearOtherSongs();
    void productionSourceLibraryRejectsDisabledAccounts();
    void productionSourceLibraryOffersConfigurationWhenNoAccountExists();
    void productionPluginPanelSeparatesContentAndOffersNavidromeSetup();
};

void MediaBridgeQmlTest::exposesOnlyBridgeAndAcceptsNormalizedQueueEntry()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QQmlApplicationEngine engine;
    SourceManager *sourceManager =
        initializeSourceStartupBoundary(*QCoreApplication::instance(), engine);
    QVERIFY(sourceManager != nullptr);

    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")), QSettings::IniFormat);
    UnavailableSecretStore secretStore;
    SourceAccountStore accountStore(&settings, &secretStore);
    SourceSessionRegistry registry(sourceManager, &accountStore, &engine);
    SourceAccountController accountController(&accountStore, &registry, sourceManager, &engine);
    MediaBridge mediaBridge(&registry, &engine);
    mediaBridge.setAccountController(&accountController);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaBridge"), &mediaBridge);

    QVERIFY(engine.rootContext()->contextProperty(QStringLiteral("mediaBridge")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceManager")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceSession")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("accountStore")).isValid());

    QQmlComponent queueAdapter(&engine);
    queueAdapter.setData(R"(
        import QtQml
        QtObject {
            property var entries: []
            function accept(entry) {
                entries.push({
                    mediaId: entry.mediaId,
                    title: entry.title,
                    artist: entry.artist,
                    url: entry.url
                })
            }
        }
    )", QUrl());
    QObject *adapter = queueAdapter.create();
    QVERIFY2(adapter != nullptr, qPrintable(queueAdapter.errorString()));

    const QVariantMap entry{{QStringLiteral("mediaId"),
                             QVariantMap{{QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                         {QStringLiteral("accountId"), QStringLiteral("home")},
                                         {QStringLiteral("nativeId"), QStringLiteral("song-1")},
                                         {QStringLiteral("kind"), 0}}},
                            {QStringLiteral("title"), QStringLiteral("Song")},
                            {QStringLiteral("artist"), QStringLiteral("Artist")},
                            {QStringLiteral("url"), QStringLiteral("https://stream.example/song-1")},
                            {QStringLiteral("providerSecret"), QStringLiteral("must-not-be-queued")}};
    QVERIFY(QMetaObject::invokeMethod(adapter, "accept", Q_ARG(QVariant, entry)));

    const QVariantList entries = adapter->property("entries").toList();
    QCOMPARE(entries.size(), 1);
    const QVariantMap queued = entries.constFirst().toMap();
    QCOMPARE(queued.value(QStringLiteral("mediaId")).toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QVERIFY(!queued.contains(QStringLiteral("providerSecret")));

    delete adapter;
}

void MediaBridgeQmlTest::accountControllerHidesSecretsAndManagesAccountLifecycle()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")), QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore accountStore(&settings, &secretStore);
    SourceSessionRegistry registry(nullptr, &accountStore);
    SourceAccountController accountController(&accountStore, &registry, nullptr);

    QVERIFY(accountController.createNavidromeAccount(QStringLiteral("Home"),
                                                     QStringLiteral("http://navidrome.test:4533"),
                                                     QStringLiteral("alice"),
                                                     QStringLiteral("initial-password")));
    const QVariantList accounts = accountController.accounts();
    QCOMPARE(accounts.size(), 1);
    const QVariantMap account = accounts.constFirst().toMap();
    QCOMPARE(account.value(QStringLiteral("sourceId")).toString(), QStringLiteral("navidrome"));
    QCOMPARE(account.value(QStringLiteral("displayName")).toString(), QStringLiteral("Home"));
    QVERIFY(!account.contains(QStringLiteral("secret")));
    QVERIFY(!account.contains(QStringLiteral("secretReference")));

    const QString accountId = account.value(QStringLiteral("accountId")).toString();
    QVERIFY(accountController.updateNavidromeAccount(accountId, QStringLiteral("Home server"),
                                                     QStringLiteral("http://navidrome.test:4533"),
                                                     QStringLiteral("alice"), QString()));
    const std::optional<SourceAccount> stored = accountStore.sourceAccount(QStringLiteral("navidrome"),
                                                                            accountId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->secret, QByteArrayLiteral("initial-password"));

    QVERIFY(accountController.setAccountEnabled(accountId, false));
    QCOMPARE(accountController.accounts().constFirst().toMap().value(QStringLiteral("enabled")).toBool(),
             false);
    QVERIFY(accountController.setAccountEnabled(accountId, true));
    QVERIFY(accountController.removeAccount(accountId));
    QVERIFY(accountController.accounts().isEmpty());
}

void MediaBridgeQmlTest::productionLeftSideBarDispatchesSourceLibrary()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    qmlRegisterType<QObject>("QueMusic", 1, 0, "DownloadedMusicModel");

    WindowDouble window;
    MusicApiDouble musicApi;
    QQuickItem mainLayout;
    NamedObject iconFont(QStringLiteral("Arial"));
    NamedObject textFont(QStringLiteral("Arial"));
    engine.rootContext()->setContextProperty("window", &window);
    engine.rootContext()->setContextProperty("MusicApi", &musicApi);
    engine.rootContext()->setContextProperty("mainLayout", &mainLayout);
    engine.rootContext()->setContextProperty("iconFont", &iconFont);
    engine.rootContext()->setContextProperty("textFont", &textFont);

    QQmlComponent mainContentComponent(&engine, QUrl(QStringLiteral("qrc:/QueMusic/layout/MainContent.qml")));
    QObject *mainContent = mainContentComponent.createWithInitialProperties(
        {{QStringLiteral("pageLoadingEnabled"), false}});
    QVERIFY2(mainContent != nullptr, qPrintable(mainContentComponent.errorString()));
    engine.rootContext()->setContextProperty("mainContent", mainContent);

    QQmlComponent sidebarComponent(&engine, QUrl(QStringLiteral("qrc:/QueMusic/layout/LeftSideBar.qml")));
    QObject *sidebar = sidebarComponent.create();
    QVERIFY2(sidebar != nullptr, qPrintable(sidebarComponent.errorString()));

    QVERIFY(QMetaObject::invokeMethod(sidebar, "navigate", Q_ARG(QVariant, 6)));
    QCOMPARE(mainContent->property("pageIndex").toInt(), 7);
    QVERIFY(QMetaObject::invokeMethod(mainContent, "contentIndexed", Q_ARG(QVariant, 6)));
    QCOMPARE(mainContent->property("pageIndex").toInt(), 6);

    delete sidebar;
    delete mainContent;
}

void MediaBridgeQmlTest::productionPlayListExposesClearOtherSongs()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));

    QQmlComponent modelComponent(&engine);
    modelComponent.setData(R"(
        import QtQuick
        import QtQml.Models
        Item {
            property alias queueModel: queue
            function appendQueueEntry(entry) { queue.append(entry) }
            function clearQueue() { queue.clear() }
            ListModel { id: queue; property int playListIndex: 0 }
        }
    )", QUrl());
    QObject *modelOwner = modelComponent.create();
    QVERIFY2(modelOwner != nullptr, qPrintable(modelComponent.errorString()));
    QObject *queueModel = modelOwner->property("queueModel").value<QObject *>();
    QVERIFY(queueModel != nullptr);
    const QVariantMap mediaId{{QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                              {QStringLiteral("accountId"), QStringLiteral("home")},
                              {QStringLiteral("nativeId"), QStringLiteral("song-1")},
                              {QStringLiteral("kind"), 0}};
    const QVariantMap entry{{QStringLiteral("name"), QStringLiteral("Bridge Song")},
                            {QStringLiteral("songer"), QStringLiteral("Bridge Artist")},
                            {QStringLiteral("bridge"), true},
                            {QStringLiteral("mediaId"), mediaId}};
    QVERIFY(QMetaObject::invokeMethod(modelOwner, "appendQueueEntry", Q_ARG(QVariant, entry)));

    PlaybackBridgeDouble mediaBridge;
    QQuickItem mainLayout;
    NamedObject iconFont(QStringLiteral("Arial"));
    engine.rootContext()->setContextProperty("playListModel", queueModel);
    engine.rootContext()->setContextProperty("mainLayout", &mainLayout);
    engine.rootContext()->setContextProperty("iconFont", &iconFont);

    QQmlComponent controllerComponent(&engine,
                                      QUrl(QStringLiteral("qrc:/QueMusic/components/QueueWiring.qml")));
    QObject *queueController = controllerComponent.create();
    QVERIFY2(queueController != nullptr, qPrintable(controllerComponent.errorString()));
    queueController->setProperty("queueModel", QVariant::fromValue(queueModel));
    queueController->setProperty("bridge", QVariant::fromValue(static_cast<QObject *>(&mediaBridge)));
    engine.rootContext()->setContextProperty("window", queueController);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls.Basic
        import QueMusic 1.0
        ApplicationWindow { width: 400; height: 300; PlayList { objectName: "productionPlayList" } }
    )", QUrl());
    QObject *host = component.create();
    QVERIFY2(host != nullptr, qPrintable(component.errorString()));
    QObject *playList = host->findChild<QObject *>(QStringLiteral("productionPlayList"));
    QVERIFY(playList != nullptr);

    QVERIFY(QMetaObject::invokeMethod(playList, "clearOtherSongs"));
    QCOMPARE(queueModel->property("count").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(queueController, "playQueueEntry", Q_ARG(QVariant, 0)));
    QCOMPARE(mediaBridge.playCalls().size(), 1);
    QCOMPARE(mediaBridge.playCalls().constFirst().toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));

    QVERIFY(QMetaObject::invokeMethod(modelOwner, "clearQueue"));
    queueModel->setProperty("playListIndex", -1);
    QVERIFY(QMetaObject::invokeMethod(playList, "clearOtherSongs"));
    QCOMPARE(queueModel->property("count").toInt(), 0);

    delete host;
    delete queueController;
    delete modelOwner;
}

void MediaBridgeQmlTest::productionSourceLibraryRejectsDisabledAccounts()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));

    AccountControllerDouble accountController;
    const QVariantMap disabled{{QStringLiteral("accountId"), QStringLiteral("offline")},
                               {QStringLiteral("displayName"), QStringLiteral("Offline")},
                               {QStringLiteral("enabled"), false}};
    const QVariantMap enabled{{QStringLiteral("accountId"), QStringLiteral("home")},
                              {QStringLiteral("displayName"), QStringLiteral("Home")},
                              {QStringLiteral("enabled"), true}};
    accountController.setAccounts(QVariantList{disabled, enabled});
    ResultModelDouble results;
    MediaBridgeDouble mediaBridge(&accountController, &results);
    engine.rootContext()->setContextProperty("mediaBridge", &mediaBridge);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/QueMusic/pages/SourceLibraryPage.qml")));
    QObject *page = component.create();
    QVERIFY2(page != nullptr, qPrintable(component.errorString()));
    QCOMPARE(page->property("selectedAccount").toMap().value(QStringLiteral("accountId")).toString(),
             QStringLiteral("home"));

    accountController.setAccounts(QVariantList{disabled});
    QCoreApplication::processEvents();
    QVERIFY(!page->property("selectedAccount").isValid() || page->property("selectedAccount").isNull());

    delete page;
}

void MediaBridgeQmlTest::productionSourceLibraryOffersConfigurationWhenNoAccountExists()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));

    AccountControllerDouble accountController;
    ResultModelDouble results;
    MediaBridgeDouble mediaBridge(&accountController, &results);
    engine.rootContext()->setContextProperty("mediaBridge", &mediaBridge);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/QueMusic/pages/SourceLibraryPage.qml")));
    QObject *page = component.create();
    QVERIFY2(page != nullptr, qPrintable(component.errorString()));

    auto *pageItem = qobject_cast<QQuickItem *>(page);
    QVERIFY(pageItem != nullptr);
    QQuickItem *configureAction =
        findVisualItemByObjectName(pageItem, QStringLiteral("sourceLibraryConfigureAction"));
    QVERIFY(configureAction != nullptr);
    QSignalSpy requested(page, SIGNAL(configureSourceRequested()));
    QVERIFY(QMetaObject::invokeMethod(configureAction, "click"));
    QCOMPARE(requested.count(), 1);

    delete page;
}

void MediaBridgeQmlTest::productionPluginPanelSeparatesContentAndOffersNavidromeSetup()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    NamedObject iconFont(QStringLiteral("Arial"));
    engine.rootContext()->setContextProperty("iconFont", &iconFont);

    PluginManagerDouble pluginManager;
    pluginManager.setPlugins({
        QVariantMap{{QStringLiteral("id"), QStringLiteral("navidrome")},
                    {QStringLiteral("name"), QStringLiteral("Navidrome")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")},
                    {QStringLiteral("state"), QStringLiteral("loaded")},
                    {QStringLiteral("loadable"), false},
                    {QStringLiteral("unloadable"), true},
                    {QStringLiteral("reloadable"), true},
                    {QStringLiteral("activeLeases"), 0}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("unrelated")},
                    {QStringLiteral("name"), QStringLiteral("Unrelated")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")},
                    {QStringLiteral("state"), QStringLiteral("loaded")},
                    {QStringLiteral("loadable"), false},
                    {QStringLiteral("unloadable"), true},
                    {QStringLiteral("reloadable"), true},
                    {QStringLiteral("activeLeases"), 0}}
    });

    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/QueMusic/components/PluginSettingsPanel.qml")));
    QObject *panel = component.createWithInitialProperties({
        {QStringLiteral("width"), 1200},
        {QStringLiteral("height"), 760},
        {QStringLiteral("containX"), 72},
        {QStringLiteral("standWidth"), 900},
        {QStringLiteral("pluginManager"), QVariant::fromValue(static_cast<QObject *>(&pluginManager))},
        {QStringLiteral("selectedTab"), 2}
    });
    QVERIFY2(panel != nullptr, qPrintable(component.errorString()));
    QCoreApplication::processEvents();
    QCOMPARE(panel->property("selectedTab").toInt(), 2);
    QCOMPARE(panel->property("pluginManager").value<QObject *>(),
             static_cast<QObject *>(&pluginManager));
    QCOMPARE(panel->property("plugins").toList().size(), 2);

    auto *panelItem = qobject_cast<QQuickItem *>(panel);
    QVERIFY(panelItem != nullptr);
    auto *header = findVisualItemByObjectName(panelItem, QStringLiteral("pluginPanelHeader"));
    auto *tabs = findVisualItemByObjectName(panelItem, QStringLiteral("pluginPanelTabs"));
    auto *notice = findVisualItemByObjectName(panelItem, QStringLiteral("pluginPanelNotice"));
    auto *list = findVisualItemByObjectName(panelItem, QStringLiteral("pluginPanelList"));
    QVERIFY(header != nullptr);
    QVERIFY(tabs != nullptr);
    QVERIFY(notice != nullptr);
    QVERIFY(list != nullptr);
    auto xInPanel = [panel](QQuickItem *item) {
        return item->mapToItem(qobject_cast<QQuickItem *>(panel), QPointF{}).x();
    };
    auto yInPanel = [panel](QQuickItem *item) {
        return item->mapToItem(qobject_cast<QQuickItem *>(panel), QPointF{}).y();
    };
    QCOMPARE(xInPanel(header), xInPanel(tabs));
    QCOMPARE(xInPanel(tabs), xInPanel(notice));
    QVERIFY(yInPanel(header) + header->height() <= yInPanel(tabs));
    QVERIFY(yInPanel(tabs) + tabs->height() <= yInPanel(notice));
    QVERIFY(yInPanel(notice) + notice->height() <= yInPanel(list));

    QVERIFY(findVisualItemByObjectName(panelItem, QStringLiteral("pluginCard_navidrome")) != nullptr);
    QQuickItem *navidromeLoader = findVisualItemByObjectName(panelItem,
                                                              QStringLiteral("navidromeConfigLoader"));
    QVERIFY(navidromeLoader != nullptr);
    QCOMPARE(navidromeLoader->property("status").toInt(), 1);
    QQuickItem *navidromeAction = findVisualItemByObjectName(panelItem,
                                                              QStringLiteral("navidromeConfigAction"));
    QVERIFY(navidromeAction != nullptr);
    QSignalSpy configured(panel, SIGNAL(configureNavidromeRequested()));
    QVERIFY(QMetaObject::invokeMethod(navidromeAction, "click"));
    QCOMPARE(configured.count(), 1);

    pluginManager.setPlugins({QVariantMap{{QStringLiteral("id"), QStringLiteral("unrelated")},
                                          {QStringLiteral("name"), QStringLiteral("Unrelated")},
                                          {QStringLiteral("version"), QStringLiteral("1.0.0")},
                                          {QStringLiteral("state"), QStringLiteral("loaded")},
                                          {QStringLiteral("loadable"), false},
                                          {QStringLiteral("unloadable"), true},
                                          {QStringLiteral("reloadable"), true},
                                          {QStringLiteral("activeLeases"), 0}}});
    QCoreApplication::processEvents();
    QVERIFY(findVisualItemByObjectName(panelItem, QStringLiteral("navidromeConfigAction")) == nullptr);

    delete panel;
}

QTEST_MAIN(MediaBridgeQmlTest)
#include "tst_MediaBridgeQml.moc"
