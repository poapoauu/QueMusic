#include "PlaybackCoordinator.h"
#include "PlaybackSink.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QTest>

namespace {
const QString packageId = QStringLiteral("org.quemusic.source.local");
bool writeAudio(const QString &path)
{
    auto le = [](quint32 value, int count) {
        QByteArray bytes;
        for (int i = 0; i < count; ++i) bytes.append(char((value >> (8 * i)) & 255));
        return bytes;
    };
    const QByteArray wave = QByteArray("RIFF", 4) + le(36 + 16000, 4) + "WAVEfmt "
        + le(16, 4) + le(1, 2) + le(1, 2) + le(8000, 4) + le(16000, 4)
        + le(2, 2) + le(16, 2) + "data" + le(16000, 4) + QByteArray(16000, '\0');
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(wave) == wave.size();
}
struct Sink final : PlaybackSink {
    int prepares = 0;
    int plays = 0;
    int stops = 0;
    StreamDescriptorV2 stream;
    bool prepare(StreamDescriptorV2 next, QUuid) override
    {
        ++prepares;
        stream = std::move(next);
        return true;
    }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override { ++stops; }
};
struct Harness {
    QTemporaryDir storage;
    QSettings settings{storage.filePath("accounts.ini"), QSettings::IniFormat};
    UnavailableSecretStore secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    Sink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    bool load()
    {
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_LOCAL_PACKAGE_ROOT));
        plugins.discover();
        return plugins.load(packageId);
    }
    bool save(const QString &account, const QString &root, bool enabled = true)
    {
        auto *schema = qobject_cast<IPluginSettingsProviderV2 *>(plugins.pluginInstance(packageId));
        if (!schema) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = packageId;
        request.sourceId = QStringLiteral("local");
        request.accountId = account;
        request.displayName = QStringLiteral("Library ") + account;
        request.enabled = enabled;
        request.schema = schema->settingsSchema();
        request.draft.insert("rootDirectory", root);
        request.draft.insert("scanOnOpen", false);
        return accounts.saveValidatedV2(request);
    }
    PageQueryV2 children(const QString &account, const QString &root)
    {
        Q_UNUSED(root);
        PageQueryV2 query;
        query.page = MusicPageKindV2::Category;
        query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = QStringLiteral("local/") + account;
        auto *session = registry.sessionFor(query.scope.sourceInstanceId);
        if (!session) return query;
        QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
        PageQueryV2 rootQuery = query;
        rootQuery.filters.insert("entityType", int(MediaEntityTypeV2::Directory));
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(rootQuery);
        if (ready.isEmpty() && !ready.wait(5000)) return query;
        const auto result = qvariant_cast<PageResultV2>(ready.last().at(1));
        query.filters.insert("directoryId", result.sections.first().items.first().ref.entityId);
        return query;
    }
};
QVariantMap playbackRow(const MediaItemV2 &item)
{
    QVariantMap actions;
    actions.insert(QString::number(int(SourceActionV2::Play)),
                   QVariantMap{{"state", int(AvailabilityV2::Available)},
                               {"constraints", QVariantMap{}}});
    return {{"ref", mediaRefV2ToVariantMap(item.ref)},
            {"availableActions", actions}, {"title", item.title},
            {"durationMs", item.durationMs}};
}
}

class LocalSourceIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void dynamicInstancesUseCoordinator()
    {
        Harness h;
        QVERIFY(h.load());
        QCOMPARE(h.plugins.plugin(packageId).state, PluginState::Loaded);
        QCOMPARE(h.registry.enabledInstances().size(), 0);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString track = root.filePath("song.wav");
        QVERIFY(writeAudio(track));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));
        auto *first = h.registry.sessionFor("local/one");
        auto *second = h.registry.sessionFor("local/two");
        QVERIFY(first && second && first != second);
        QTRY_COMPARE(first->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(second->state(), SourceSessionStateV2::Ready);
        auto *firstPage = qobject_cast<IPageProviderV2 *>(first);
        auto *secondPage = qobject_cast<IPageProviderV2 *>(second);
        QVERIFY(firstPage && secondPage);
        QSignalSpy oneReady(first, &IMusicSourceSessionV2::pageReady);
        QSignalSpy twoReady(second, &IMusicSourceSessionV2::pageReady);
        firstPage->fetchPage(h.children("one", root.path()));
        secondPage->fetchPage(h.children("two", root.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!oneReady.isEmpty() && !twoReady.isEmpty(), 5000);
        const auto firstItem = qvariant_cast<PageResultV2>(oneReady.last().at(1)).sections.first().items.first();
        const auto secondItem = qvariant_cast<PageResultV2>(twoReady.last().at(1)).sections.first().items.first();
        QCOMPARE(firstItem.ref.sourcePluginId, QString("local"));
        QCOMPARE(firstItem.ref.sourceInstanceId, QString("local/one"));
        QCOMPARE(secondItem.ref.sourceInstanceId, QString("local/two"));
        QVERIFY(!QUuid(firstItem.ref.entityId).isNull());
        QVERIFY(!QUuid(secondItem.ref.entityId).isNull());
        QVERIFY(firstItem.ref.entityId != secondItem.ref.entityId);
        QVERIFY(firstItem.ref != secondItem.ref);
        QSignalSpy failed(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        QVERIFY(!h.coordinator.play(playbackRow(firstItem)).isNull());
        QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.url.toLocalFile(), QFileInfo(track).canonicalFilePath());
        QCOMPARE(h.sink.stream.media, firstItem.ref);
        QVERIFY(h.sink.stream.headers.isEmpty());
        QVERIFY(QFile::remove(track));
        h.coordinator.play(playbackRow(firstItem));
        QTRY_VERIFY(!failed.isEmpty());
        QCOMPARE(h.sink.plays, 1);
        QVERIFY(h.registry.disableInstance("local/one"));
        h.coordinator.play(playbackRow(firstItem));
        QTRY_VERIFY(failed.size() >= 2);
        QCOMPARE(h.sink.plays, 1);
    }

    void switchingInstancesAndDisablingCurrentStopsPlayback()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeAudio(root.filePath("song.wav")));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));

        auto trackFor = [&](const QString &account) {
            auto *session = h.registry.sessionFor(QStringLiteral("local/") + account);
            if (!session || session->state() != SourceSessionStateV2::Ready)
                return MediaItemV2{};
            const auto query = h.children(account, root.path());
            QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
            qobject_cast<IPageProviderV2 *>(session)->fetchPage(query);
            if (ready.isEmpty() && !ready.wait(5000))
                return MediaItemV2{};
            const auto result = qvariant_cast<PageResultV2>(ready.last().at(1));
            return result.sections.first().items.first();
        };
        auto *firstSession = h.registry.sessionFor("local/one");
        auto *secondSession = h.registry.sessionFor("local/two");
        QVERIFY(firstSession && secondSession);
        QTRY_COMPARE(firstSession->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(secondSession->state(), SourceSessionStateV2::Ready);
        const auto first = trackFor("one");
        const auto second = trackFor("two");
        QVERIFY(!first.ref.entityId.isEmpty());
        QVERIFY(!second.ref.entityId.isEmpty());

        const auto firstGeneration = h.coordinator.play(playbackRow(first));
        QVERIFY(!firstGeneration.isNull());
        QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.media, first.ref);
        const auto secondGeneration = h.coordinator.play(playbackRow(second));
        QVERIFY(!secondGeneration.isNull());
        QVERIFY(secondGeneration != firstGeneration);
        QTRY_COMPARE(h.sink.plays, 2);
        QCOMPARE(h.sink.stream.media, second.ref);
        QCOMPARE(h.sink.stream.url.scheme(), QStringLiteral("file"));
        QCOMPARE(h.sink.stops, 1);

        QVERIFY(h.registry.disableInstance("local/one"));
        QCOMPARE(h.coordinator.currentGeneration(), secondGeneration);
        QCOMPARE(h.sink.stops, 1);
        QVERIFY(h.registry.disableInstance("local/two"));
        QTRY_VERIFY(h.coordinator.currentGeneration().isNull());
        QCOMPARE(h.sink.stops, 2);
    }

    void unloadWaitsForWorkersAndLease()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeAudio(root.filePath("song.wav")));
        QVERIFY(h.save("disabled", root.path(), false));
        QCOMPARE(h.registry.enabledInstances().size(), 1);
        QVERIFY(!h.registry.enabledInstances().first().enabled);
        QVERIFY(!h.registry.sessionFor("local/disabled"));
        QVERIFY(h.save("active", root.path()));
        auto *session = h.registry.sessionFor("local/active");
        QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        QSignalSpy changed(&h.registry, &SourceRegistry::instanceContentChanged);
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(h.children("active", root.path()));
        QCoreApplication::processEvents();
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Busy);
        QVERIFY(h.registry.disableInstance("local/active"));
        QCOMPARE(h.plugins.plugin(packageId).activeLeases, 0);
        const int before = changed.size();
        QTest::qWait(100);
        QCOMPARE(changed.size(), before);
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Success);
        QVERIFY(h.plugins.load(packageId));
        QVERIFY(!h.registry.sessionFor("local/disabled"));
        const auto instances = h.registry.enabledInstances();
        QCOMPARE(instances.size(), 2);
        for (const auto &instance : instances) QVERIFY(!instance.enabled);
        QVERIFY(h.registry.enableInstance("local/active"));
        QPointer<IMusicSourceSessionV2> reopened = h.registry.sessionFor("local/active");
        QVERIFY(reopened);
        QTRY_COMPARE(reopened->state(), SourceSessionStateV2::Ready);
        QVERIFY(h.registry.closeInstance("local/active"));
        QVERIFY(!reopened);
        QCOMPARE(h.plugins.plugin(packageId).activeLeases, 0);
        QVERIFY(h.accounts.remove("local", "active"));
        QVERIFY(h.plugins.reload(packageId) == PluginOperationResult::Success);
        QVERIFY(!h.registry.sessionFor("local/active"));
        QCOMPARE(h.registry.enabledInstances().size(), 1);
        QVERIFY(!h.registry.enabledInstances().first().enabled);
    }
};

QTEST_MAIN(LocalSourceIntegrationTest)
#include "tst_LocalSourceIntegration.moc"
