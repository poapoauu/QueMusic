#include "MusicHub.h"
#include "SourceAccountStore.h"
#if __has_include("DirectoryLibraryController.h")
#include "DirectoryLibraryController.h"
#define HAS_DIRECTORY_LIBRARY 1
#endif
#include <QSettings>
#include <QSignalSpy>
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
    void retriesOnlyFailedSectionAndRejectsDuplicateRequests()
    {
        QTemporaryDir files; QVERIFY(files.isValid());
        QSettings settings(files.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("MusicHub/cacheDirectory", files.filePath("cache"));
        DirectorySecrets secrets;
        SourceAccountStore accounts(&settings, &secrets);
        PluginManager plugins;
        plugins.addSearchPath(QUEMUSIC_TASK7_PACKAGES);
        QCOMPARE(plugins.discover(), 1); QVERIFY(plugins.load("org.quemusic.source.task7"));
        QVERIFY(accounts.saveResolvedV2({"task7", "home", "Home", {}, {}}));
        QVERIFY(accounts.saveResolvedV2({"task7", "office", "Office", {}, {}}));
        SourceRegistry registry(&plugins, &accounts);
        auto *home = registry.sessionFor("task7/home");
        auto *office = registry.sessionFor("task7/office");
        QVERIFY(home && office); office->setProperty("fail", true);
        SourceScopeStore scope(&settings); MusicHub hub(&registry, &scope, &settings);
        auto *directories = hub.directoryLibrary(); auto *model = directories->model();
        directories->activate();
        QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        const auto rowForId = [model](const QString &id) {
            for (int i = 0; i < model->rowCount(); ++i)
                if (model->section(i).sectionId == id) return i;
            return -1;
        };
        const auto officeRow = rowForId("directory/task7/office");
        QVERIFY(officeRow >= 0);
        const auto healthyRequests = home->property("requests").toList().size();
        const auto failedRequests = office->property("requests").toList().size();
        office->setProperty("hold", true);
        directories->retry("directory/task7/home"); // Healthy and unknown origins cannot be retried.
        directories->retry("unknown");
        directories->retry("directory/task7/office");
        directories->retry("directory/task7/office");
        QTRY_COMPARE(office->property("requests").toList().size(), failedRequests + 1);
        QCOMPARE(home->property("requests").toList().size(), healthyRequests);
        QCOMPARE(resets.count(), 0);
        const auto request = office->property("requests").toList().last().toMap();
        QCOMPARE(request.value("cursor").toString(), QString{});
        emit office->requestFailed(request.value("id").toUuid(), {SourceErrorKindV2::Network});
        QTRY_VERIFY(!model->data(model->index(officeRow), MusicPageModel::LoadingMoreRole).toBool());
        office->setProperty("hold", false); office->setProperty("fail", false);
        directories->retry("directory/task7/office");
        QTRY_VERIFY(model->data(model->index(officeRow), MusicPageModel::ErrorRole).toMap().isEmpty());
        QCOMPARE(model->section(officeRow).items.size(), 1);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(home->property("requests").toList().size(), healthyRequests);
        QVERIFY(directories->browse(model->itemAt(officeRow, 0)));
        QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        QVERIFY(model->section(0).hasMore);
        office->setProperty("fail", true);
        directories->loadMore("directory/content");
        QTRY_VERIFY(!model->data(model->index(0), MusicPageModel::ErrorRole).toMap().isEmpty());
        const auto afterFailure = office->property("requests").toList().size();
        directories->loadMore("directory/content"); // Failed cursor must go through retry.
        QCOMPARE(office->property("requests").toList().size(), afterFailure);
        office->setProperty("fail", false);
        directories->retry("directory/content");
        QTRY_VERIFY(model->data(model->index(0), MusicPageModel::ErrorRole).toMap().isEmpty());
        QCOMPARE(model->section(0).items.size(), 1); // Replace only this section from page one.
        QCOMPARE(office->property("requests").toList().last().toMap().value("cursor").toString(), QString{});
        office->setProperty("unsupported", true);
        directories->loadMore("directory/content");
        QTRY_VERIFY(!model->data(model->index(0), MusicPageModel::ErrorRole).toMap().isEmpty());
        const auto unsupportedRequests = office->property("requests").toList().size();
        directories->retry("directory/content"); directories->loadMore("directory/content");
        QCOMPARE(office->property("requests").toList().size(), unsupportedRequests);
        office->setProperty("unsupported", false); office->setProperty("fail", true);
        directories->refresh();
        QTRY_COMPARE(model->state(), PageLoadStateV2::Failed);
        office->setProperty("fail", false); office->setProperty("hold", true);
        const auto beforeStaleRetry = office->property("requests").toList().size();
        directories->retry("directory/content");
        QTRY_COMPARE(office->property("requests").toList().size(), beforeStaleRetry + 1);
        const auto staleId = office->property("requests").toList().last().toMap().value("id").toUuid();
        office->setProperty("hold", false);
        QVERIFY(directories->navigateBack());
        QTRY_COMPARE(model->state(), PageLoadStateV2::Ready);
        QCOMPARE(model->rowCount(), 2);
        emit office->requestFailed(staleId, {SourceErrorKindV2::Network});
        QTest::qWait(20);
        QVERIFY(rowForId("directory/content") < 0);
        QCOMPARE(model->state(), PageLoadStateV2::Ready);
        for (int i = 0; i < model->rowCount(); ++i)
            QVERIFY(model->data(model->index(i), MusicPageModel::ErrorRole).toMap().isEmpty());
    }
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
