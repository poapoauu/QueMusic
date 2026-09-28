#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <thread>

#include "plugins/local-source/LocalLibraryIndex.h"

namespace {
bool writeFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("not tagged") == 10;
}
LocalScanConfig configFor(const QString &root)
{
    const auto config = LocalSourceScanner::parseConfig({{"rootDirectory", root}}, nullptr);
    return *config;
}
SourceConfigurationV2 sourceConfig(const QString &root, const QString &account)
{
    SourceConfigurationV2 config;
    config.pluginPackageId = "org.quemusic.source.local";
    config.sourceId = "local";
    config.sourceInstanceId = "local/" + account;
    config.accountId = account;
    config.displayName = "Music";
    config.parameters = {{"rootDirectory", root}, {"recursive", true},
                         {"scanOnOpen", true}, {"watchChanges", false},
                         {"ignoreDirectories", ""}};
    return config;
}
}

class LocalLibraryIndexTest : public QObject
{
    Q_OBJECT
private slots:
    void sameInstanceSharesDifferentInstancesIsolate()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(writeFile(temp.filePath("one.mp3")));
        const auto scanConfig = configFor(temp.path());
        const auto home = sourceConfig(temp.path(), "home");
        auto office = sourceConfig(temp.path(), "office");
        LocalLibraryIndexPool pool;
        auto playback = pool.acquire(home, scanConfig);
        auto settings = pool.acquire(home, scanConfig);
        QVERIFY(playback == settings);
        auto separate = pool.acquire(office, scanConfig);
        QVERIFY(playback != separate);
        office.sourceInstanceId = home.sourceInstanceId;
        QVERIFY(pool.acquire(office, scanConfig) != playback);
        auto changedSetting = home;
        changedSetting.parameters.insert("scanOnOpen", false);
        QVERIFY(pool.acquire(changedSetting, scanConfig) != playback);
        auto sameCanonicalRoot = home;
        QVERIFY(QFile::link(temp.path(), temp.filePath("root-alias")));
        sameCanonicalRoot.parameters.insert("rootDirectory", temp.filePath("root-alias"));
        QVERIFY(pool.acquire(sameCanonicalRoot, scanConfig) == playback);
        QSignalSpy changed(playback.get(), &LocalLibraryIndex::snapshotChanged);
        playback->requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        QVERIFY(settings->snapshot());
        QCOMPARE(settings->snapshot()->revision, quint64(1));
        QCOMPARE(settings->snapshot()->scan.entries.size(), 1);
        playback.reset();
        settings->requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 5000);
        QCOMPARE(settings->snapshot()->revision, quint64(2));
        QVERIFY(!separate->snapshot());
    }
    void oneScanOnePendingAndShutdown()
    {
        QTemporaryDir temp;
        const auto config = configFor(temp.path());
        std::atomic_int starts{0};
        std::atomic_int active{0};
        std::atomic_int maximum{0};
        std::atomic_bool hold{true};
        LocalLibraryIndex::Hooks hooks;
        hooks.scan = [&](const LocalScanConfig &value, const std::atomic_bool &cancel) {
            starts.fetch_add(1);
            const int now = active.fetch_add(1) + 1;
            int old = maximum.load();
            while (old < now && !maximum.compare_exchange_weak(old, now)) {}
            while (hold.load() && !cancel.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto result = LocalSourceScanner().scan(value, cancel);
            active.fetch_sub(1);
            return result;
        };
        LocalLibraryIndex index("local/home", "fingerprint", config, false, nullptr, hooks);
        QSignalSpy changed(&index, &LocalLibraryIndex::snapshotChanged);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(starts.load(), 1, 5000);
        index.requestScan();
        index.requestScan();
        hold.store(false);
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        QCOMPARE(starts.load(), 2);
        QCOMPARE(maximum.load(), 1);
        QVERIFY(index.snapshot());
        QCOMPARE(index.snapshot()->generation, quint64(3));
        hold.store(true);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(starts.load(), 3, 5000);
        index.stop();
        QCOMPARE(active.load(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(index.snapshot()->revision, quint64(1));
        index.requestScan();
        QCoreApplication::processEvents();
        QCOMPARE(starts.load(), 3);
    }
    void watchCoverageAndRootRecreation()
    {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("root"));
        const QString root = temp.filePath("root");
        QVERIFY(writeFile(root + "/first.mp3"));
        for (int n = 0; n < 48; ++n)
            QVERIFY(QDir(root).mkdir(QString("sub-%1").arg(n, 3, 10, QChar('0'))));
        const auto config = configFor(root);
        QSet<QString> attempted;
        std::atomic_bool failRegistration{false};
        LocalLibraryIndex::Hooks hooks;
        hooks.registerWatch = [&](QFileSystemWatcher &watcher, const QString &path) {
            attempted.insert(path);
            if (failRegistration.load() && path.endsWith("sub-037")) return false;
            return watcher.addPath(path);
        };
        LocalLibraryIndex index("local/home", "fingerprint", config, true, nullptr, hooks);
        QSignalSpy changed(&index, &LocalLibraryIndex::snapshotChanged);
        QSignalSpy failed(&index, &LocalLibraryIndex::refreshFailed);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        const auto accepted = index.snapshot();
        QVERIFY(accepted);
        QCOMPARE(accepted->scan.watchedDirectories.size(), 49);
        QCOMPARE(attempted.size(), 51); // root parent, discovered directories, and track
        for (const auto &directory : accepted->scan.watchedDirectories)
            QVERIFY(attempted.contains(directory));
        QVERIFY(attempted.contains(QFileInfo(config.canonicalRoot).absolutePath()));
        QVERIFY(attempted.contains(QFileInfo(root + "/first.mp3").canonicalFilePath()));
        failRegistration.store(true);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed.takeFirst().at(0)).messageKey,
                 QString("local.watch.incomplete"));
        QCOMPARE(index.snapshot(), accepted);
        QCOMPARE(changed.count(), 1);
        failRegistration.store(false);
        QVERIFY(QDir(root).removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 5000);
        QCOMPARE(index.snapshot(), accepted);
        QVERIFY(QDir(temp.path()).mkdir("root"));
        QVERIFY(writeFile(root + "/recreated.mp3"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 5000);
        QCOMPARE(index.snapshot()->revision, quint64(2));
        QCOMPARE(index.snapshot()->scan.entries.size(), 1);
        QVERIFY(index.snapshot()->scan.entries.first().entityId.endsWith("recreated.mp3"));
    }
    void fileModificationRefreshesSnapshot()
    {
        QTemporaryDir temp;
        const QString song = temp.filePath("one.mp3");
        QVERIFY(writeFile(song));
        const auto config = configFor(temp.path());
        LocalLibraryIndex index("local/home", "fingerprint", config, true);
        QSignalSpy changed(&index, &LocalLibraryIndex::snapshotChanged);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        QFile file(song);
        QVERIFY(file.open(QIODevice::Append));
        QVERIFY(file.write("new data") == 8);
        file.close();
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 5000);
        QCOMPARE(index.snapshot()->revision, quint64(2));
    }
    void missingRootBeforeFirstScanStillWatchesParent()
    {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("root"));
        const QString root = temp.filePath("root");
        const auto config = configFor(root);
        QVERIFY(QDir(root).rmdir(root));
        LocalLibraryIndex index("local/home", "fingerprint", config, true);
        QSignalSpy changed(&index, &LocalLibraryIndex::snapshotChanged);
        QSignalSpy failed(&index, &LocalLibraryIndex::refreshFailed);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(!index.snapshot());
        QVERIFY(QDir(temp.path()).mkdir("root"));
        QVERIFY(writeFile(root + "/new.mp3"));
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        QCOMPARE(index.snapshot()->scan.entries.size(), 1);
    }
    void finalLeaseStopsAndJoinsWorker()
    {
        QTemporaryDir temp;
        const auto config = configFor(temp.path());
        std::atomic_bool entered{false};
        std::atomic_bool exited{false};
        LocalLibraryIndex::Hooks hooks;
        hooks.scan = [&](const LocalScanConfig &value, const std::atomic_bool &cancel) {
            entered.store(true);
            while (!cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto result = LocalSourceScanner().scan(value, cancel);
            exited.store(true);
            return result;
        };
        LocalLibraryIndexPool pool(hooks);
        auto playback = pool.acquire(sourceConfig(temp.path(), "home"), config);
        auto settings = pool.acquire(sourceConfig(temp.path(), "home"), config);
        QPointer<LocalLibraryIndex> guard(playback.get());
        playback->requestScan();
        QTRY_VERIFY_WITH_TIMEOUT(entered.load(), 5000);
        settings.reset();
        QVERIFY(guard);
        playback.reset();
        QVERIFY(exited.load());
        QVERIFY(guard.isNull());
        QCoreApplication::processEvents();
    }
    void lastLeaseReleasedOffThreadDeletesOnOwnerThread()
    {
        QTemporaryDir temp;
        const auto config = configFor(temp.path());
        LocalLibraryIndexPool pool;
        auto index = pool.acquire(sourceConfig(temp.path(), "home"), config);
        QPointer<LocalLibraryIndex> guard(index.get());
        std::atomic_bool released{false};
        bool deletedOnOwner = false;
        QObject::connect(index.get(), &QObject::destroyed, this, [&] {
            deletedOnOwner = QThread::currentThread() == thread();
        });
        std::thread releaser([owned = std::move(index), &released]() mutable {
            owned.reset();
            released.store(true);
        });
        QTRY_VERIFY_WITH_TIMEOUT(released.load(), 5000);
        releaser.join();
        QVERIFY(guard.isNull());
        QVERIFY(deletedOnOwner);
    }
    void failedParentRearmInvalidatesRunningGeneration()
    {
        QTemporaryDir temp;
        const auto config = configFor(temp.path());
        const QString parent = QFileInfo(config.canonicalRoot).absolutePath();
        std::atomic_bool failParent{false};
        std::atomic_bool workerEntered{false};
        std::atomic_bool releaseWorker{false};
        std::atomic_int calls{0};
        LocalLibraryIndex::Hooks hooks;
        hooks.registerWatch = [&](QFileSystemWatcher &, const QString &path) {
            // Simulate an OS watch that was acknowledged but then lost; real
            // QFileSystemWatcher delivery is covered by the recreation test.
            return !(failParent.load() && path == parent);
        };
        hooks.scan = [&](const LocalScanConfig &value, const std::atomic_bool &cancel) {
            if (++calls == 2) {
                workerEntered.store(true);
                while (!releaseWorker.load() && !cancel.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return LocalSourceScanner().scan(value, cancel);
        };
        LocalLibraryIndex index("local/home", "fingerprint", config, true, nullptr, hooks);
        QSignalSpy changed(&index, &LocalLibraryIndex::snapshotChanged);
        QSignalSpy failed(&index, &LocalLibraryIndex::refreshFailed);
        index.requestScan();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
        const auto accepted = index.snapshot();
        index.requestScan();
        QTRY_VERIFY_WITH_TIMEOUT(workerEntered.load(), 5000);
        failParent.store(true);
        index.requestScan();
        failParent.store(false); // registration recovered; the older result must still be fenced
        releaseWorker.store(true);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QTest::qWait(200);
        QCOMPARE(index.snapshot(), accepted);
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(LocalLibraryIndexTest)
#include "tst_LocalLibraryIndex.moc"
