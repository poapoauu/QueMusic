#include "MediaBridge.h"
#include "SourceAccountController.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"
#include "SourceSessionRegistry.h"
#include "SourceStartup.h"

#include <QDir>
#include <QHash>
#include <QSettings>
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

} // namespace

class MediaBridgeQmlTest : public QObject {
    Q_OBJECT

private slots:
    void exposesOnlyBridgeAndAcceptsNormalizedQueueEntry();
    void accountControllerHidesSecretsAndManagesAccountLifecycle();
    void qmlNavigationQueueAndAccountSelectionContracts();
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

void MediaBridgeQmlTest::qmlNavigationQueueAndAccountSelectionContracts()
{
    QQmlApplicationEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQml.Models

        Item {
            property int sourceLibraryPage: 7
            property int searchPage: 6
            property var contentCalls: []
            property var bridgePlayCalls: []
            property int legacyPlayCalls: 0
            property int queueIndex: -1
            property var accounts: []
            property var selectedAccount: null
            property string selectedAccountId: selectedAccount ? selectedAccount.accountId : ""
            property int selectorIndex: -1

            ListModel { id: queueModel }

            function contentIndexed(index) {
                contentCalls = contentCalls.concat([index])
            }

            function navigateFromSidebar(choice) {
                contentIndexed(choice >= 6 ? choice + 1 : choice)
            }

            function openSearch() {
                contentIndexed(searchPage)
            }

            function appendQueueEntry(entry) {
                queueModel.append(entry)
                queueIndex = queueModel.count - 1
            }

            function copyQueueEntry(entry) {
                return {
                    name: entry.name,
                    path: entry.path,
                    songer: entry.songer,
                    source: entry.source,
                    bridge: entry.bridge === true,
                    mediaId: entry.mediaId,
                    albumTitle: entry.albumTitle,
                    artworkUrl: entry.artworkUrl,
                    durationMs: entry.durationMs
                }
            }

            function clearOtherSongs() {
                var currentEntry = copyQueueEntry(queueModel.get(queueIndex))
                queueModel.clear()
                queueModel.append(currentEntry)
                queueIndex = 0
            }

            function playQueueEntry(index) {
                var entry = queueModel.get(index)
                if (entry.bridge === true && entry.mediaId) {
                    bridgePlayCalls = bridgePlayCalls.concat([{
                        sourceId: entry.mediaId.sourceId,
                        accountId: entry.mediaId.accountId,
                        nativeId: entry.mediaId.nativeId,
                        kind: entry.mediaId.kind,
                        title: entry.name,
                        subtitle: entry.songer,
                        albumTitle: entry.albumTitle,
                        artworkUrl: entry.artworkUrl,
                        durationMs: entry.durationMs
                    }])
                    return
                }
                legacyPlayCalls += 1
            }

            function enabledAccounts() {
                var enabled = []
                for (var i = 0; i < accounts.length; ++i) {
                    if (accounts[i].enabled === true)
                        enabled.push(accounts[i])
                }
                return enabled
            }

            function chooseAccount(index) {
                var enabled = enabledAccounts()
                selectedAccount = index >= 0 && index < enabled.length && enabled[index].enabled === true
                    ? enabled[index] : null
            }

            function ensureSelectedAccount() {
                var enabled = enabledAccounts()
                var selectedIndex = -1
                if (selectedAccount) {
                    for (var i = 0; i < enabled.length; ++i) {
                        if (enabled[i].accountId === selectedAccount.accountId) {
                            selectedIndex = i
                            break
                        }
                    }
                }
                if (selectedIndex < 0 && enabled.length > 0)
                    selectedIndex = 0
                selectorIndex = selectedIndex
                chooseAccount(selectedIndex)
            }

        }
    )", QUrl());
    QObject *harness = component.create();
    QVERIFY2(harness != nullptr, qPrintable(component.errorString()));

    QVERIFY(QMetaObject::invokeMethod(harness, "navigateFromSidebar", Q_ARG(QVariant, 6)));
    QVERIFY(QMetaObject::invokeMethod(harness, "openSearch"));
    const QVariantList contentCalls = harness->property("contentCalls").toList();
    const QVariantList expectedContentCalls{7, 6};
    QCOMPARE(contentCalls, expectedContentCalls);
    QCOMPARE(harness->property("sourceLibraryPage").toInt(), 7);
    QCOMPARE(harness->property("searchPage").toInt(), 6);

    const QVariantMap mediaId{{QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                              {QStringLiteral("accountId"), QStringLiteral("home")},
                              {QStringLiteral("nativeId"), QStringLiteral("song-1")},
                              {QStringLiteral("kind"), 0}};
    const QVariantMap bridgeEntry{{QStringLiteral("name"), QStringLiteral("Bridge Song")},
                                  {QStringLiteral("path"), QStringLiteral("bridge:navidrome/home/song-1/0")},
                                  {QStringLiteral("songer"), QStringLiteral("Bridge Artist")},
                                  {QStringLiteral("source"), -2},
                                  {QStringLiteral("bridge"), true},
                                  {QStringLiteral("mediaId"), mediaId},
                                  {QStringLiteral("albumTitle"), QStringLiteral("Bridge Album")},
                                  {QStringLiteral("artworkUrl"), QStringLiteral("image://bridge/song-1")},
                                  {QStringLiteral("durationMs"), 1234}};
    QVERIFY(QMetaObject::invokeMethod(harness, "appendQueueEntry", Q_ARG(QVariant, bridgeEntry)));
    QVERIFY(QMetaObject::invokeMethod(harness, "clearOtherSongs"));
    QVERIFY(QMetaObject::invokeMethod(harness, "playQueueEntry", Q_ARG(QVariant, 0)));

    const QVariantList bridgePlayCalls = harness->property("bridgePlayCalls").toList();
    QCOMPARE(bridgePlayCalls.size(), 1);
    const QVariantMap resolvedBridgeEntry = bridgePlayCalls.constFirst().toMap();
    QCOMPARE(resolvedBridgeEntry.value(QStringLiteral("sourceId")).toString(), QStringLiteral("navidrome"));
    QCOMPARE(resolvedBridgeEntry.value(QStringLiteral("accountId")).toString(), QStringLiteral("home"));
    QCOMPARE(resolvedBridgeEntry.value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QCOMPARE(harness->property("legacyPlayCalls").toInt(), 0);

    const QVariantMap disabledAccount{{QStringLiteral("accountId"), QStringLiteral("offline")},
                                      {QStringLiteral("enabled"), false}};
    const QVariantMap enabledAccount{{QStringLiteral("accountId"), QStringLiteral("home")},
                                     {QStringLiteral("enabled"), true}};
    harness->setProperty("accounts", QVariantList{disabledAccount, enabledAccount});
    harness->setProperty("selectedAccount", disabledAccount);
    QVERIFY(QMetaObject::invokeMethod(harness, "ensureSelectedAccount"));
    QCOMPARE(harness->property("selectorIndex").toInt(), 0);
    QCOMPARE(harness->property("selectedAccountId").toString(), QStringLiteral("home"));

    harness->setProperty("accounts", QVariantList{disabledAccount});
    QVERIFY(QMetaObject::invokeMethod(harness, "ensureSelectedAccount"));
    QCOMPARE(harness->property("selectorIndex").toInt(), -1);
    QVERIFY(harness->property("selectedAccountId").toString().isEmpty());

    delete harness;
}

QTEST_MAIN(MediaBridgeQmlTest)
#include "tst_MediaBridgeQml.moc"
