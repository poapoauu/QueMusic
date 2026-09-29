#include "MusicHub.h"
#include "SourceAccountStore.h"
#if __has_include("DirectoryLibraryController.h")
#include "DirectoryLibraryController.h"
#define HAS_DIRECTORY_LIBRARY 1
#endif
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class DirectorySecrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return QByteArray{}; }
    bool remove(const QString &, QString *) override { return true; }
};

class DirectoryLibraryControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void directoryProjectionDoesNotReplaceCategory()
    {
#ifndef HAS_DIRECTORY_LIBRARY
        QFAIL("DirectoryLibraryController contract is not implemented");
#else
        QTemporaryDir files;
        QVERIFY(files.isValid());
        QSettings settings(files.filePath("accounts.ini"), QSettings::IniFormat);
        DirectorySecrets secrets;
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
        hub.activatePage(int(MusicPageKindV2::Category));
        QTRY_COMPARE(hub.category()->state(), PageLoadStateV2::Ready);
        const auto categoryRows = hub.category()->rowCount();

        auto *directories = hub.directoryLibrary();
        QVERIFY(directories);
        directories->activate();
        QTRY_COMPARE(directories->model()->state(), PageLoadStateV2::Ready);
        QCOMPARE(directories->model()->rowCount(), 2);
        QCOMPARE(hub.category()->rowCount(), categoryRows);
        const auto root = directories->model()->itemAt(0, 0);
        QCOMPARE(root.value("ref").toMap().value("entityType").toInt(),
                 int(MediaEntityTypeV2::Directory));
        QVERIFY(directories->browse(root));
        QTRY_COMPARE(directories->model()->state(), PageLoadStateV2::Ready);
        QCOMPARE(directories->model()->section(0).items.size(), 1);
        QVERIFY(directories->model()->section(0).hasMore);
        directories->loadMore(QStringLiteral("directory/content"));
        QTRY_COMPARE(directories->model()->section(0).items.size(), 2);
        directories->refresh();
        QTRY_COMPARE(directories->model()->state(), PageLoadStateV2::Ready);
        QCOMPARE(directories->model()->section(0).items.size(), 1);
        QVERIFY(directories->navigateBack());
        QTRY_COMPARE(directories->model()->rowCount(), 2);
        QCOMPARE(hub.category()->rowCount(), categoryRows);
#endif
    }
};
QTEST_MAIN(DirectoryLibraryControllerTest)
#include "tst_DirectoryLibraryController.moc"
