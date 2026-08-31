#include "MediaBridge.h"
#include "SourceAccountController.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"
#include "SourceSessionRegistry.h"
#include "SourceStartup.h"

#include <QDir>
#include <QFile>
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
    const auto source = [](const QString &relativePath) {
        QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR) + QLatin1Char('/') + relativePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString{};
        }
        return QString::fromUtf8(file.readAll());
    };

    const QString sidebar = source(QStringLiteral("layout/LeftSideBar.qml"));
    QVERIFY(sidebar.contains(QStringLiteral("function contentIndexForNav(choice)")));
    QVERIFY(sidebar.contains(QStringLiteral("return choice >= 6 ? choice + 1 : choice;")));
    QVERIFY(sidebar.contains(QStringLiteral("mainContent.contentIndexed(sidebar.contentIndexForNav(index));")));

    const QString playlist = source(QStringLiteral("components/PlayList.qml"));
    QVERIFY(playlist.contains(QStringLiteral("window.copyQueueEntry(")));
    QVERIFY(playlist.contains(QStringLiteral("playListModel.append(currentEntry);")));
    QVERIFY(playlist.contains(QStringLiteral("window.playQueueEntry(index);")));

    const QString mainQml = source(QStringLiteral("main.qml"));
    QVERIFY(mainQml.contains(QStringLiteral("function copyQueueEntry(entry)")));
    QVERIFY(mainQml.contains(QStringLiteral("bridge: entry.bridge === true")));
    QVERIFY(mainQml.contains(QStringLiteral("mediaId: entry.mediaId")));

    const QString settings = source(QStringLiteral("SettingsView.qml"));
    QVERIFY(settings.contains(QStringLiteral("sourceAccounts.availableSources")));
    QVERIFY(settings.contains(QStringLiteral("（已禁用）")));

    const QString library = source(QStringLiteral("pages/SourceLibraryPage.qml"));
    QVERIFY(library.contains(QStringLiteral("function enabledAccounts()")));
    QVERIFY(library.contains(QStringLiteral("account.enabled === true")));
    QVERIFY(library.contains(QStringLiteral("ensureSelectedAccount")));
}

QTEST_MAIN(MediaBridgeQmlTest)
#include "tst_MediaBridgeQml.moc"
