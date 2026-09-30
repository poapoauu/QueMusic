#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include "extensions/content-events/v1/ISourceContentEventsProviderV1.h"
#include "extensions/content-events/v1/SourceContentEventsV1.h"

#include "plugins/local-source/LocalSourcePlugin.h"
#include "plugins/local-source/LocalSourceSession.h"

class LocalSourceTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void schemaAndIdentity();
    void requestLifecycleAndLazyOpen();
    void pagesAndCursorScope();
    void resourceRevalidationAndSharedRescan();
    void staleCursorAndSearch();
    void artworkLyricsAndForeignMedia();
    void unavailableRootAndUnknownAction();
    void swappedSymlinkNeverEscapesRoot();
    void unavailableRootsAndLateGeneration();
    void settingsRescanOwnedByPluginIndex();
    void opaqueReferencesAreScopedAndExpire();
    void retiredIdDoesNotReviveInAnotherLiveConfiguration();
};

namespace {
SourceConfigurationV2 config(const QString &root, const QString &account = "one")
{
    return {"org.quemusic.source.local", "local", "local/" + account, account,
            "Fixture " + account, {{"rootDirectory", root}, {"scanOnOpen", false}}, {}};
}
QByteArray wave()
{
    const auto le = [](quint32 value, int count) {
        QByteArray bytes;
        for (int i = 0; i < count; ++i) bytes.append(char((value >> (8 * i)) & 255));
        return bytes;
    };
    return QByteArray("RIFF", 4) + le(36 + 16000, 4) + "WAVEfmt "
        + le(16, 4) + le(1, 2) + le(1, 2) + le(8000, 4) + le(16000, 4)
        + le(2, 2) + le(16, 2) + "data" + le(16000, 4) + QByteArray(16000, '\0');
}
bool writeFile(const QString &path, const QByteArray &bytes = wave())
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
PageQueryV2 rootQuery(const QString &instance)
{
    PageQueryV2 query;
    query.page = MusicPageKindV2::Category;
    query.section = PageSectionKindV2::Tracks;
    query.scope.sourceInstanceId = instance;
    query.filters.insert("entityType", int(MediaEntityTypeV2::Directory));
    return query;
}
QString rootId(IMusicSourceSessionV2 *session, const QString &instance = "local/one")
{
    QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(session)->fetchPage(rootQuery(instance));
    if (ready.isEmpty() && !ready.wait(5000)) return {};
    const auto result = qvariant_cast<PageResultV2>(ready.last().at(1));
    return result.sections.first().items.first().ref.entityId;
}
}

void LocalSourceTest::schemaAndIdentity()
{
    LocalSourcePlugin plugin;
    const auto descriptor = plugin.descriptor();
    QCOMPARE(descriptor.pluginPackageId, QString("org.quemusic.source.local"));
    QCOMPARE(descriptor.sourceId, QString("local"));
    QCOMPARE(descriptor.sdkAbi, 2);
    QCOMPARE(descriptor.declaredActions.value(SourceActionV2::Play).state,
             AvailabilityV2::Available);
    QCOMPARE(descriptor.declaredActions.value(SourceActionV2::Download).state,
             AvailabilityV2::Unsupported);
    const auto schema = plugin.settingsSchema();
    QHash<QString, SettingsFieldV2> fields;
    for (const auto &section : schema) {
        for (const auto &field : section.fields) fields.insert(field.id, field);
        for (const auto &action : section.actions) {
            QCOMPARE(action.id, QString("rescan"));
            QVERIFY(!action.requiresConfirmation);
        }
    }
    QCOMPARE(fields.size(), 5);
    QCOMPARE(fields.value("rootDirectory").type, SettingsFieldTypeV2::Directory);
    QVERIFY(fields.value("rootDirectory").required);
    QCOMPARE(fields.value("recursive").defaultValue, QVariant(true));
    QCOMPARE(fields.value("scanOnOpen").defaultValue, QVariant(true));
    QCOMPARE(fields.value("watchChanges").defaultValue, QVariant(false));
    QCOMPARE(fields.value("ignoreDirectories").defaultValue, QVariant(QString()));
    for (const auto &field : fields) {
        QVERIFY(field.type != SettingsFieldTypeV2::Secret);
        QVERIFY(!field.secret);
    }
    QTemporaryDir root;
    QVERIFY(root.isValid());
    std::unique_ptr<IMusicSourceSessionV2> one(plugin.createSession(config(root.path()), nullptr));
    std::unique_ptr<IMusicSourceSessionV2> two(plugin.createSession(config(root.path(), "two"), nullptr));
    QCOMPARE(one->identity().sourcePluginId, QString("local"));
    QCOMPARE(one->identity().sourceInstanceId, QString("local/one"));
    QCOMPARE(two->identity().accountId, QString("two"));
    QVERIFY(one->identity().sourceInstanceId != two->identity().sourceInstanceId);
    QVERIFY(qobject_cast<IPageProviderV2 *>(one.get()));
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(one.get()));
    QVERIFY(qobject_cast<ISettingsActionProviderV2 *>(one.get()));
    QVERIFY(!qobject_cast<IFavoriteProviderV2 *>(one.get()));
}

void LocalSourceTest::requestLifecycleAndLazyOpen()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath("track.wav")));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root.path()), nullptr));
    QSignalSpy started(session.get(), &IMusicSourceSessionV2::requestStarted);
    QSignalSpy pages(session.get(), &IMusicSourceSessionV2::pageReady);
    auto *events = qobject_cast<ISourceContentEventsProviderV1 *>(session.get());
    QVERIFY(events);
    QSignalSpy changes(events->contentEvents(), &SourceContentEventsV1::contentChanged);
    const QUuid opening = session->open();
    QCOMPARE(started.size(), 1);
    QCOMPARE(started.first().first().toUuid(), opening);
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    QCOMPARE(pages.size(), 0); // scanOnOpen=false does not eagerly browse.
    QCOMPARE(changes.size(), 0); // and does not build an index.
    auto *provider = qobject_cast<IPageProviderV2 *>(session.get());
    const QUuid cancelled = provider->fetchPage(rootQuery("local/one"));
    session->cancel(cancelled);
    QTest::qWait(100);
    for (const auto &call : pages) QVERIFY(call.first().toUuid() != cancelled);
    const QUuid browse = provider->fetchPage(rootQuery("local/one"));
    QTRY_VERIFY_WITH_TIMEOUT(!pages.isEmpty(), 5000);
    QCOMPARE(pages.last().first().toUuid(), browse);
    QVERIFY(started.size() >= 3);
    QCOMPARE(changes.size(), 1);
}

void LocalSourceTest::pagesAndCursorScope()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkdir("sub"));
    QVERIFY(writeFile(root.filePath("A.wav")));
    QVERIFY(writeFile(root.filePath("sub/B.wav")));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root.path()), nullptr));
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    QSignalSpy pages(session.get(), &IMusicSourceSessionV2::pageReady);
    QSignalSpy failed(session.get(), &IMusicSourceSessionV2::requestFailed);
    auto *provider = qobject_cast<IPageProviderV2 *>(session.get());
    provider->fetchPage(rootQuery("local/one"));
    QTRY_VERIFY_WITH_TIMEOUT(!pages.isEmpty(), 5000);
    const auto rootPage = qvariant_cast<PageResultV2>(pages.last().at(1));
    QCOMPARE(rootPage.sections.first().items.size(), 1);
    const auto directory = rootPage.sections.first().items.first().ref;
    QVERIFY(!QUuid(directory.entityId).isNull());
    QCOMPARE(directory.entityType, MediaEntityTypeV2::Directory);
    QCOMPARE(directory.sourcePluginId, QString("local"));
    PageQueryV2 children = rootQuery("local/one");
    children.filters.clear();
    children.filters.insert("directoryId", directory.entityId);
    children.limit = 1;
    provider->fetchPage(children);
    QTRY_COMPARE(pages.size(), 2);
    const auto first = qvariant_cast<PageResultV2>(pages.last().at(1)).sections.first();
    QCOMPARE(first.layoutHint, QString("directories"));
    QCOMPARE(first.items.size(), 1);
    QVERIFY(first.hasMore);
    children.cursor = first.nextCursor;
    provider->fetchPage(children);
    QTRY_COMPARE(pages.size(), 3);
    const auto second = qvariant_cast<PageResultV2>(pages.last().at(1)).sections.first();
    QCOMPARE(second.items.size(), 1);
    QVERIFY(second.items.first().ref.entityId != first.items.first().ref.entityId);
    children.scope.sourceInstanceId = "local/two";
    provider->fetchPage(children);
    QTRY_VERIFY(!failed.isEmpty());
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.last().at(1)).kind,
             SourceErrorKindV2::InvalidRequest);
}

void LocalSourceTest::resourceRevalidationAndSharedRescan()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString track = root.filePath("valid.wav");
    QVERIFY(writeFile(track));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> live(plugin.createSession(config(root.path()), nullptr));
    std::unique_ptr<IMusicSourceSessionV2> settings(plugin.createSession(config(root.path()), nullptr));
    live->open();
    settings->open();
    QTRY_COMPARE(live->state(), SourceSessionStateV2::Ready);
    QTRY_COMPARE(settings->state(), SourceSessionStateV2::Ready);
    auto *actions = qobject_cast<ISettingsActionProviderV2 *>(settings.get());
    auto *liveEvents = qobject_cast<ISourceContentEventsProviderV1 *>(live.get());
    auto *settingsEvents = qobject_cast<ISourceContentEventsProviderV1 *>(settings.get());
    QVERIFY(liveEvents && settingsEvents);
    QVERIFY(liveEvents->contentEvents() != settingsEvents->contentEvents());
    QSignalSpy changed(liveEvents->contentEvents(), &SourceContentEventsV1::contentChanged);
    QSignalSpy completed(settings.get(), &IMusicSourceSessionV2::settingsActionCompleted);
    actions->runSettingsAction("rescan");
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 5000);
    QTRY_COMPARE(changed.size(), 1);
    QSignalSpy pages(live.get(), &IMusicSourceSessionV2::pageReady);
    auto *pageProvider = qobject_cast<IPageProviderV2 *>(live.get());
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(live.get())}};
    pages.clear();
    pageProvider->fetchPage(children);
    QTRY_VERIFY_WITH_TIMEOUT(!pages.isEmpty(), 5000);
    const auto page = qvariant_cast<PageResultV2>(pages.last().at(1));
    QCOMPARE(page.sections.first().items.size(), 1);
    const auto media = page.sections.first().items.first().ref;
    QVERIFY(!QUuid(media.entityId).isNull());
    QVERIFY(media.entityId != children.filters.value("directoryId").toString());
    auto *playback = qobject_cast<IPlaybackProviderV2 *>(live.get());
    QSignalSpy streams(live.get(), &IMusicSourceSessionV2::streamReady);
    QSignalSpy failures(live.get(), &IMusicSourceSessionV2::requestFailed);
    playback->resolveStream(media);
    QTRY_COMPARE(streams.size(), 1);
    QCOMPARE(qvariant_cast<StreamDescriptorV2>(streams.first().at(1)).url.toLocalFile(),
             QFileInfo(track).canonicalFilePath());
    QVERIFY(QFile::remove(track));
    playback->resolveStream(media);
    QTRY_VERIFY(!failures.isEmpty());
    QCOMPARE(qvariant_cast<SourceErrorV2>(failures.last().at(1)).kind,
             SourceErrorKindV2::NotFound);
}

void LocalSourceTest::staleCursorAndSearch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath("Alpha.wav")));
    QVERIFY(writeFile(root.filePath("Beta.wav")));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root.path()), nullptr));
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    auto *pages = qobject_cast<IPageProviderV2 *>(session.get());
    auto *settings = qobject_cast<ISettingsActionProviderV2 *>(session.get());
    QSignalSpy ready(session.get(), &IMusicSourceSessionV2::pageReady);
    QSignalSpy errors(session.get(), &IMusicSourceSessionV2::requestFailed);
    QSignalSpy rescans(session.get(), &IMusicSourceSessionV2::settingsActionCompleted);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(session.get())}};
    ready.clear();
    children.limit = 1;
    pages->fetchPage(children);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 5000);
    const auto first = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first();
    QVERIFY(first.hasMore);
    QVERIFY(!first.nextCursor.isEmpty());
    settings->runSettingsAction("rescan");
    QTRY_VERIFY_WITH_TIMEOUT(!rescans.isEmpty(), 5000);
    children.cursor = first.nextCursor;
    pages->fetchPage(children);
    QTRY_VERIFY(!errors.isEmpty());
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).messageKey,
             QString("local.cursor.stale"));
    children.cursor.clear();
    children.page = MusicPageKindV2::Search;
    children.filters.clear();
    children.searchText = "beta";
    pages->fetchPage(children);
    QTRY_COMPARE(ready.size(), 2);
    const auto result = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first();
    QCOMPARE(result.kind, PageSectionKindV2::SearchResults);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().title, QString("Beta"));
    children.limit = 201;
    pages->fetchPage(children);
    QTRY_COMPARE(errors.size(), 2);
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
             SourceErrorKindV2::InvalidRequest);
    // A cursor belongs to the full instance configuration, not just its ID.
    auto changedConfig = config(root.path());
    changedConfig.parameters.insert("recursive", false);
    std::unique_ptr<IMusicSourceSessionV2> other(plugin.createSession(changedConfig, nullptr));
    other->open();
    QTRY_COMPARE(other->state(), SourceSessionStateV2::Ready);
    QSignalSpy otherErrors(other.get(), &IMusicSourceSessionV2::requestFailed);
    PageQueryV2 foreignCursor = rootQuery("local/one");
    foreignCursor.filters = {{"directoryId", rootId(other.get())}};
    foreignCursor.limit = 1;
    foreignCursor.cursor = first.nextCursor;
    qobject_cast<IPageProviderV2 *>(other.get())->fetchPage(foreignCursor);
    QTRY_VERIFY_WITH_TIMEOUT(!otherErrors.isEmpty(), 5000);
    QCOMPARE(qvariant_cast<SourceErrorV2>(otherErrors.last().at(1)).messageKey,
             QString("local.cursor.stale"));
}

void LocalSourceTest::artworkLyricsAndForeignMedia()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString track = root.filePath("art.wav");
    QVERIFY(writeFile(track));
    QVERIFY(writeFile(root.filePath("art.lrc"), "[00:01.00]hello"));
    QImage picture(2, 2, QImage::Format_RGB32);
    picture.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(picture.save(&buffer, "PNG"));
    QVERIFY(writeFile(root.filePath("art.png"), png));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root.path()), nullptr));
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    auto *page = qobject_cast<IPageProviderV2 *>(session.get());
    auto *playback = qobject_cast<IPlaybackProviderV2 *>(session.get());
    QSignalSpy ready(session.get(), &IMusicSourceSessionV2::pageReady);
    QSignalSpy actions(session.get(), &IMusicSourceSessionV2::actionCompleted);
    QSignalSpy errors(session.get(), &IMusicSourceSessionV2::requestFailed);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(session.get())}};
    ready.clear();
    page->fetchPage(children);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 5000);
    const auto media = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first().items.first().ref;
    const QUuid artwork = playback->fetchArtwork(media);
    QTRY_VERIFY_WITH_TIMEOUT(!actions.isEmpty(), 5000);
    const auto image = qvariant_cast<ActionResultV2>(actions.last().at(1));
    QCOMPARE(actions.last().first().toUuid(), artwork);
    QCOMPARE(image.action, SourceActionV2::Artwork);
    QCOMPARE(image.subject, media);
    QCOMPARE(image.payload.value("mimeType").toString(), QString("image/png"));
    QVERIFY(!image.payload.value("bytes").toByteArray().isEmpty());
    playback->fetchLyrics(media);
    QTRY_COMPARE_WITH_TIMEOUT(actions.size(), 2, 5000);
    const auto lyrics = qvariant_cast<ActionResultV2>(actions.last().at(1));
    QCOMPARE(lyrics.action, SourceActionV2::Lyrics);
    QVERIFY(lyrics.payload.value("lyrics").metaType().id() == QMetaType::QString);
    QVERIFY(lyrics.payload.value("lyrics").toString().contains("hello"));
    auto forged = media;
    forged.sourceInstanceId = "local/other";
    playback->resolveStream(forged);
    QTRY_VERIFY(!errors.isEmpty());
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
             SourceErrorKindV2::InvalidRequest);
    QVERIFY(QFile::remove(root.filePath("art.png")));
    playback->fetchArtwork(media);
    QTRY_COMPARE(errors.size(), 2);
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
             SourceErrorKindV2::NotFound);
    QSignalSpy streams(session.get(), &IMusicSourceSessionV2::streamReady);
    playback->resolveStream(media);
    QTRY_COMPARE(streams.size(), 1);
    playback->resolveStream({"local", "local/one", "one", MediaEntityTypeV2::Directory,
                             QUrl::fromLocalFile(root.path()).toString(QUrl::FullyEncoded)});
    QTRY_COMPARE(errors.size(), 3);
}

void LocalSourceTest::unavailableRootAndUnknownAction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    LocalSourcePlugin plugin;
    auto invalid = config(root.filePath("missing"));
    std::unique_ptr<IMusicSourceSessionV2> missing(plugin.createSession(invalid, nullptr));
    QSignalSpy failed(missing.get(), &IMusicSourceSessionV2::requestFailed);
    missing->open();
    QTRY_COMPARE(missing->state(), SourceSessionStateV2::Failed);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.first().at(1)).messageKey,
             QString("local.root.unavailable"));
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root.path()), nullptr));
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    auto *actions = qobject_cast<ISettingsActionProviderV2 *>(session.get());
    QSignalSpy errors(session.get(), &IMusicSourceSessionV2::requestFailed);
    actions->runSettingsAction("deleteFiles");
    QTRY_VERIFY(!errors.isEmpty());
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
             SourceErrorKindV2::Unsupported);
}

void LocalSourceTest::swappedSymlinkNeverEscapesRoot()
{
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    QVERIFY(QDir(parent.path()).mkdir("library"));
    const QString root = parent.filePath("library");
    const QString track = root + "/safe.wav";
    const QString outside = parent.filePath("outside.wav");
    QVERIFY(writeFile(track));
    QVERIFY(writeFile(outside));
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> session(plugin.createSession(config(root), nullptr));
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    auto *page = qobject_cast<IPageProviderV2 *>(session.get());
    QSignalSpy ready(session.get(), &IMusicSourceSessionV2::pageReady);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(session.get())}};
    ready.clear();
    page->fetchPage(children);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 5000);
    const auto media = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first().items.first().ref;
    QVERIFY(QFile::remove(track));
    QVERIFY(QFile::link(outside, track));
    QSignalSpy errors(session.get(), &IMusicSourceSessionV2::requestFailed);
    QSignalSpy streams(session.get(), &IMusicSourceSessionV2::streamReady);
    qobject_cast<IPlaybackProviderV2 *>(session.get())->resolveStream(media);
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
             SourceErrorKindV2::NotFound);
    QCOMPARE(streams.size(), 0);
}

void LocalSourceTest::unavailableRootsAndLateGeneration()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath("late.wav")));
    LocalLibraryIndex::Hooks hooks;
    hooks.scan = [](const LocalScanConfig &config, const std::atomic_bool &cancel) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return LocalSourceScanner().scan(config, cancel);
    };
    LocalLibraryIndexPool pool(hooks);
    LocalSourceSession session(config(root.path()), &pool);
    session.open();
    QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
    QSignalSpy pages(&session, &IMusicSourceSessionV2::pageReady);
    QSignalSpy failures(&session, &IMusicSourceSessionV2::requestFailed);
    session.fetchPage(rootQuery("local/one"));
    QCoreApplication::processEvents(); // starts the worker before close
    session.close();
    QTest::qWait(150);
    QCOMPARE(pages.size(), 0);
    QCOMPARE(failures.size(), 0);
    QCOMPARE(session.state(), SourceSessionStateV2::Closed);
}

void LocalSourceTest::settingsRescanOwnedByPluginIndex()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    LocalSourcePlugin plugin;
    std::unique_ptr<IMusicSourceSessionV2> live(plugin.createSession(config(root.path()), nullptr));
    live->open();
    QTRY_COMPARE(live->state(), SourceSessionStateV2::Ready);
    auto *events = qobject_cast<ISourceContentEventsProviderV1 *>(live.get());
    QVERIFY(events);
    QSignalSpy changed(events->contentEvents(), &SourceContentEventsV1::contentChanged);
    {
        std::unique_ptr<IMusicSourceSessionV2> settings(plugin.createSession(config(root.path()), nullptr));
        settings->open();
        QTRY_COMPARE(settings->state(), SourceSessionStateV2::Ready);
        auto *actions = qobject_cast<ISettingsActionProviderV2 *>(settings.get());
        QSignalSpy done(settings.get(), &IMusicSourceSessionV2::settingsActionCompleted);
        actions->runSettingsAction("rescan");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
    }
    QTRY_COMPARE(changed.size(), 1);
    QVERIFY(writeFile(root.filePath("after.wav")));
    auto *actions = qobject_cast<ISettingsActionProviderV2 *>(live.get());
    QSignalSpy done(live.get(), &IMusicSourceSessionV2::settingsActionCompleted);
    actions->runSettingsAction("rescan");
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
    QTRY_COMPARE(changed.size(), 2);
    QVERIFY(changed.at(1).first().toULongLong() > changed.at(0).first().toULongLong());
    QSignalSpy pages(live.get(), &IMusicSourceSessionV2::pageReady);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(live.get())}};
    pages.clear();
    qobject_cast<IPageProviderV2 *>(live.get())->fetchPage(children);
    QTRY_COMPARE(pages.size(), 1);
    QCOMPARE(qvariant_cast<PageResultV2>(pages.first().at(1)).sections.first().items.size(), 1);
}
void LocalSourceTest::opaqueReferencesAreScopedAndExpire()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir("music"));
    const QString root = temp.filePath("music");
    const QString firstPath = root + "/old.wav";
    QVERIFY(writeFile(firstPath));
    QVERIFY(QDir(root).mkdir("child"));
    LocalLibraryIndexPool pool({}, temp.filePath("identities"));
    LocalSourceSession first(config(root), &pool);
    LocalSourceSession second(config(root, "two"), &pool);
    first.open();
    second.open();
    QTRY_COMPARE(first.state(), SourceSessionStateV2::Ready);
    QTRY_COMPARE(second.state(), SourceSessionStateV2::Ready);
    const QString firstRoot = rootId(&first);
    const QString secondRoot = rootId(&second, "local/two");
    QVERIFY(!QUuid(firstRoot).isNull());
    QVERIFY(!QUuid(secondRoot).isNull());
    QVERIFY(firstRoot != secondRoot);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", firstRoot}};
    QSignalSpy ready(&first, &IMusicSourceSessionV2::pageReady);
    QSignalSpy errors(&first, &IMusicSourceSessionV2::requestFailed);
    auto *page = qobject_cast<IPageProviderV2 *>(&first);
    page->fetchPage(children);
    QTRY_COMPARE(ready.size(), 1);
    const auto items = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first().items;
    QCOMPARE(items.size(), 2);
    const auto directory = items.first().ref;
    const auto track = items.last().ref;
    QCOMPARE(directory.entityType, MediaEntityTypeV2::Directory);
    QCOMPARE(track.entityType, MediaEntityTypeV2::Track);
    QVERIFY(!QUuid(directory.entityId).isNull());
    QVERIFY(!QUuid(track.entityId).isNull());
    auto *playback = qobject_cast<IPlaybackProviderV2 *>(&first);
    QSignalSpy streams(&first, &IMusicSourceSessionV2::streamReady);
    const auto expectPageFailure = [&](const QString &id) {
        const int before = errors.size();
        children.filters = {{"directoryId", id}};
        page->fetchPage(children);
        QTRY_COMPARE(errors.size(), before + 1);
        QCOMPARE(qvariant_cast<SourceErrorV2>(errors.last().at(1)).kind,
                 SourceErrorKindV2::InvalidRequest);
    };
    expectPageFailure(track.entityId);
    expectPageFailure(secondRoot);
    expectPageFailure(QUrl::fromLocalFile(root).toString(QUrl::FullyEncoded));
    expectPageFailure(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto expectStreamFailure = [&](MediaRefV2 ref) {
        const int before = errors.size();
        playback->resolveStream(ref);
        QTRY_COMPARE(errors.size(), before + 1);
        QCOMPARE(streams.size(), 0);
    };
    auto wrongKind = track;
    wrongKind.entityId = directory.entityId;
    expectStreamFailure(wrongKind);
    auto legacy = track;
    legacy.entityId = QUrl::fromLocalFile(firstPath).toString(QUrl::FullyEncoded);
    expectStreamFailure(legacy);
    auto forged = track;
    forged.entityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    expectStreamFailure(forged);
    PageQueryV2 secondChildren = rootQuery("local/two");
    secondChildren.filters = {{"directoryId", secondRoot}};
    QSignalSpy secondReady(&second, &IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&second)->fetchPage(secondChildren);
    QTRY_COMPARE(secondReady.size(), 1);
    const auto foreignTrack = qvariant_cast<PageResultV2>(secondReady.last().at(1))
                                  .sections.first().items.last().ref;
    QVERIFY(foreignTrack.entityId != track.entityId);
    auto foreign = track;
    foreign.entityId = foreignTrack.entityId;
    expectStreamFailure(foreign);
    QVERIFY(QFile::rename(firstPath, root + "/moved.wav"));
    QSignalSpy rescanned(&first, &IMusicSourceSessionV2::settingsActionCompleted);
    qobject_cast<ISettingsActionProviderV2 *>(&first)->runSettingsAction("rescan");
    QTRY_COMPARE(rescanned.size(), 1);
    expectStreamFailure(track);
    children.filters = {{"directoryId", firstRoot}};
    page->fetchPage(children);
    QTRY_COMPARE(ready.size(), 2);
    const auto moved = qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first().items.last().ref;
    QVERIFY(!QUuid(moved.entityId).isNull());
    QVERIFY(moved.entityId != track.entityId);
    const QString movedPath = root + "/moved.wav";
    QVERIFY(QFile::remove(movedPath));
    qobject_cast<ISettingsActionProviderV2 *>(&first)->runSettingsAction("rescan");
    QTRY_COMPARE(rescanned.size(), 2);
    expectStreamFailure(moved);
    QVERIFY(writeFile(movedPath));
    qobject_cast<ISettingsActionProviderV2 *>(&first)->runSettingsAction("rescan");
    QTRY_COMPARE(rescanned.size(), 3);
    page->fetchPage(children);
    QTRY_COMPARE(ready.size(), 3);
    const auto recreated = qvariant_cast<PageResultV2>(ready.last().at(1))
                               .sections.first().items.last().ref;
    QVERIFY(!QUuid(recreated.entityId).isNull());
    QVERIFY(recreated.entityId != moved.entityId);
    expectStreamFailure(moved);
    playback->resolveStream(recreated);
    QTRY_COMPARE(streams.size(), 1);
}

void LocalSourceTest::retiredIdDoesNotReviveInAnotherLiveConfiguration()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath("song.wav");
    QVERIFY(writeFile(path));
    LocalLibraryIndexPool pool({}, temp.filePath("identities"));
    const auto firstConfig = config(temp.path());
    auto secondConfig = firstConfig;
    secondConfig.displayName = "Other view";
    LocalSourceSession first(firstConfig, &pool);
    LocalSourceSession second(secondConfig, &pool);
    first.open();
    second.open();
    QTRY_COMPARE(first.state(), SourceSessionStateV2::Ready);
    QTRY_COMPARE(second.state(), SourceSessionStateV2::Ready);
    PageQueryV2 children = rootQuery("local/one");
    children.filters = {{"directoryId", rootId(&second)}};
    QSignalSpy ready(&second, &IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&second)->fetchPage(children);
    QTRY_COMPARE(ready.size(), 1);
    const auto oldRef = qvariant_cast<PageResultV2>(ready.last().at(1))
                            .sections.first().items.first().ref;
    QVERIFY(!QUuid(oldRef.entityId).isNull());
    QVERIFY(QFile::remove(path));
    QSignalSpy rescanned(&first, &IMusicSourceSessionV2::settingsActionCompleted);
    qobject_cast<ISettingsActionProviderV2 *>(&first)->runSettingsAction("rescan");
    QTRY_COMPARE(rescanned.size(), 1);
    QVERIFY(writeFile(path));
    QSignalSpy streams(&second, &IMusicSourceSessionV2::streamReady);
    QSignalSpy errors(&second, &IMusicSourceSessionV2::requestFailed);
    qobject_cast<IPlaybackProviderV2 *>(&second)->resolveStream(oldRef);
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(streams.size(), 0);
    qobject_cast<IPageProviderV2 *>(&second)->fetchPage(children);
    QTRY_COMPARE(ready.size(), 2);
    QVERIFY(qvariant_cast<PageResultV2>(ready.last().at(1)).sections.first().items.isEmpty());
}
QTEST_MAIN(LocalSourceTest)
#include "tst_LocalSource.moc"
