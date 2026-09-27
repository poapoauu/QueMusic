#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <atomic>

#include "plugins/local-source/LocalSourceScanner.h"
#include "core/local-media/LocalMediaFiles.h"
#include <wavfile.h>
#include <tag.h>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes = "bad tags")
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
LocalScanConfig configFor(const QString &root)
{
    return *LocalSourceScanner::parseConfig({{"rootDirectory", root}}, nullptr);
}
QString fileId(const QString &path)
{
    return QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath()).toString(QUrl::FullyEncoded);
}
QByteArray png(Qt::GlobalColor color)
{
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
QByteArray frame(const QByteArray &id, const QByteArray &data)
{
    QByteArray result = id;
    for (int shift : {24, 16, 8, 0}) result.append(char((data.size() >> shift) & 255));
    result.append("\0\0", 2);
    return result + data;
}
QByteArray taggedMp3(const QByteArray &picture, const QByteArray &lyrics = "[00:02.00]embedded")
{
    const QByteArray frames = frame("USLT", QByteArray("\0eng\0", 5) + lyrics)
        + frame("APIC", QByteArray("\0image/png\0\3\0", 13) + picture);
    QByteArray result("ID3\3\0\0", 6);
    for (int shift : {21, 14, 7, 0}) result.append(char((frames.size() >> shift) & 127));
    return result + frames;
}
}

class LocalSourceScannerTest : public QObject
{
    Q_OBJECT
private slots:
    void rootBoundaryAndEncodedIdentity()
    {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("music"));
        QVERIFY(QDir(temp.path()).mkdir("music-other"));
        const QString root = temp.filePath("music");
        const QString song = root + QStringLiteral("/中文 space.mp3");
        const QString outside = temp.filePath("music-other/out.mp3");
        QVERIFY(writeFile(song));
        QVERIFY(writeFile(outside));
        QVERIFY(QFile::link(song, root + "/alias.mp3"));
        QVERIFY(QFile::link(outside, root + "/outside.mp3"));
        QVERIFY(QFile::link(temp.filePath("music-other"), root + "/linked-directory"));
        QVERIFY(QFile::link(root, temp.filePath("root-link")));
        const auto config = configFor(temp.filePath("root-link"));
        QCOMPARE(config.canonicalRoot, QFileInfo(root).canonicalFilePath());
        LocalSourceScanner scanner;
        const auto result = scanner.scan(config, std::atomic_bool{false});
        QVERIFY(!result.error);
        QCOMPARE(result.entries.size(), 1);
        const QString id = result.entries.first().entityId;
        QVERIFY(id.contains("%E4%B8%AD%E6%96%87%20space.mp3"));
        QCOMPARE(scanner.scan(config, std::atomic_bool{false}).entries.first().entityId, id);
        QVERIFY(LocalSourceScanner::validatedPath(config, id, false));
        QVERIFY(!LocalSourceScanner::validatedPath(config, fileId(outside), false));
        QVERIFY(!LocalSourceScanner::validatedPath(config, QUrl::fromLocalFile(root + "/outside.mp3").toString(), false));
        QVERIFY(!LocalSourceScanner::validatedPath(config, "file://user@localhost/tmp/x", false));
        QVERIFY(!LocalSourceScanner::validatedPath(config, id, true));
        QString nonCanonicalId = id;
        nonCanonicalId.replace("%20", " ");
        QVERIFY(!LocalSourceScanner::validatedPath(config, nonCanonicalId, false));
    }
    void schemaAndScanFailures()
    {
        QTemporaryDir temp;
        SourceErrorV2 error;
        for (const QString &root : {QString("relative"), QString("https://host/music"),
                                   QString("file://user@host/music"), QString("//server/share")}) {
            QVERIFY(!LocalSourceScanner::parseConfig({{"rootDirectory", root}}, &error));
            QCOMPARE(error.kind, SourceErrorKindV2::InvalidRequest);
            QVERIFY(error.detail.isEmpty());
        }
        for (const QString &ignore : {QString("../x"), QString("a//b"), QString("/absolute"), QString("a/../b")})
            QVERIFY(!LocalSourceScanner::parseConfig({{"rootDirectory", temp.path()}, {"ignoreDirectories", ignore}}, &error));
        LocalSourceScanner scanner;
        auto config = configFor(temp.path());
        QVERIFY(scanner.scan(config, std::atomic_bool{false}).entries.isEmpty());
        QVERIFY(QDir(temp.path()).mkpath("skip/deep"));
        QVERIFY(QDir(temp.path()).mkdir("child"));
        QVERIFY(writeFile(temp.filePath("z.mp3")));
        QVERIFY(writeFile(temp.filePath("a.FLAC")));
        QVERIFY(writeFile(temp.filePath("skip/deep/hidden.mp3")));
        QVERIFY(writeFile(temp.filePath("child/inside.wav")));
        QVERIFY(writeFile(temp.filePath("not-audio.txt")));
        config.ignoreDirectories = {"skip"};
        auto result = scanner.scan(config, std::atomic_bool{false});
        QVERIFY(!result.error);
        QCOMPARE(result.entries.size(), 3);
        QStringList ids;
        for (const auto &entry : result.entries) {
            ids.append(entry.entityId);
            QVERIFY(!entry.item.title.isEmpty());
        }
        auto sorted = ids;
        std::sort(sorted.begin(), sorted.end());
        QCOMPARE(ids, sorted);
        config.recursive = false;
        QCOMPARE(scanner.scan(config, std::atomic_bool{false}).entries.size(), 2);
        QVERIFY(QFile::rename(temp.filePath("z.mp3"), temp.filePath("new.mp3")));
        QVERIFY(!LocalSourceScanner::validatedPath(config, QUrl::fromLocalFile(temp.filePath("z.mp3")).toString(), false));
        const auto moved = scanner.scan(config, std::atomic_bool{false});
        QCOMPARE(moved.entries.size(), 2);
        QVERIFY(moved.entries.at(1).entityId.endsWith("/new.mp3"));
        const QString removedId = moved.entries.at(1).entityId;
        QVERIFY(QFile::remove(temp.filePath("new.mp3")));
        QVERIFY(!LocalSourceScanner::validatedPath(config, removedId, false));
        QCOMPARE(scanner.scan(config, std::atomic_bool{false}).entries.size(), 1);
        QVERIFY(writeFile(temp.filePath("b.mp3")));
        const auto added = scanner.scan(config, std::atomic_bool{false});
        QCOMPARE(added.entries.size(), 2);
        QVERIFY(added.entries.at(0).entityId.endsWith("/a.FLAC"));
        QVERIFY(added.entries.at(1).entityId.endsWith("/b.mp3"));
        const auto cancelled = scanner.scan(config, std::atomic_bool{true});
        QVERIFY(cancelled.cancelled);
        QVERIFY(cancelled.entries.isEmpty());
        config.canonicalRoot = temp.filePath("deleted");
        QVERIFY(scanner.scan(config, std::atomic_bool{false}).error);
    }
    void metadataFromRealAudio()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath("fallback.wav");
        QByteArray wave("RIFF", 4);
        auto little = [](quint32 n, int count) { QByteArray b; for(int i=0;i<count;++i) b.append(char(n >> (i*8))); return b; };
        wave += little(36 + 16000, 4) + "WAVEfmt " + little(16,4) + little(1,2) + little(1,2)
            + little(8000,4) + little(16000,4) + little(2,2) + little(16,2) + "data" + little(16000,4)
            + QByteArray(16000, '\0');
        QVERIFY(writeFile(path, wave));
        {
            TagLib::RIFF::WAV::File file(QFile::encodeName(path).constData());
            file.tag()->setTitle("Fixture title");
            file.tag()->setArtist("Fixture artist");
            file.tag()->setAlbum("Fixture album");
            QVERIFY(file.save());
        }
        const auto result = LocalSourceScanner().scan(configFor(temp.path()), std::atomic_bool{false});
        QCOMPARE(result.entries.size(), 1);
        const auto &item = result.entries.first().item;
        QCOMPARE(item.title, QString("Fixture title"));
        QCOMPARE(item.artists, QStringList{"Fixture artist"});
        QCOMPARE(item.album, QString("Fixture album"));
        QCOMPARE(item.durationMs, 1000);
    }
    void accessFailuresDoNotPublishSuccessfulEmptyRoot()
    {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("child"));
        QVERIFY(writeFile(temp.filePath("readable.mp3")));
        QVERIFY(writeFile(temp.filePath("blocked.mp3")));
        QVERIFY(writeFile(temp.filePath("child/inside.mp3")));
        const auto config = configFor(temp.path());
        LocalSourceScanner deniedRoot([&](const QString &path, bool) { return path != config.canonicalRoot; });
        const auto failure = deniedRoot.scan(config, std::atomic_bool{false});
        QVERIFY(failure.error);
        QCOMPARE(failure.error->kind, SourceErrorKindV2::Unavailable);
        QVERIFY(failure.error->detail.isEmpty());
        LocalSourceScanner partial([](const QString &path, bool directory) {
            return directory ? !path.endsWith("/child") : !path.endsWith("/blocked.mp3");
        });
        const auto result = partial.scan(config, std::atomic_bool{false});
        QVERIFY(!result.error);
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries.first().item.title, QString("readable"));
        QVERIFY(result.warningKeys.contains("local.scan.directoryUnavailable"));
        QVERIFY(result.warningKeys.contains("local.scan.fileUnavailable"));
        std::atomic_bool cancel{false};
        int reads = 0;
        LocalSourceScanner interrupted([&](const QString &, bool directory) {
            if (!directory && ++reads == 2) cancel.store(true);
            return true;
        });
        const auto stopped = interrupted.scan(config, cancel);
        QVERIFY(stopped.cancelled);
        QVERIFY(stopped.entries.isEmpty());
        QVERIFY(stopped.watchedDirectories.isEmpty());
        int directoryChecks = 0;
        LocalSourceScanner disappearingRoot([&](const QString &path, bool directory) {
            return !(directory && path == config.canonicalRoot && ++directoryChecks == 2);
        });
        QVERIFY(disappearingRoot.scan(config, std::atomic_bool{false}).error);
    }
    void replacedRootDoesNotGrantNewAuthority()
    {
        QTemporaryDir temp;
        QTemporaryDir outside;
        QVERIFY(QDir(temp.path()).mkdir("music"));
        const QString root = temp.filePath("music");
        const auto config = configFor(root);
        QVERIFY(writeFile(outside.filePath("secret.mp3")));
        QVERIFY(writeFile(outside.filePath("secret.lrc"), "[00:01.00]secret"));
        QVERIFY(QDir(temp.path()).rename("music", "original"));
        QVERIFY(QFile::link(outside.path(), root));
        QVERIFY(!LocalSourceScanner::validatedPath(config, fileId(outside.filePath("secret.mp3")), false));
        QVERIFY(LocalMediaFiles::lyricsText(outside.filePath("secret.mp3"), config.canonicalRoot).isEmpty());
    }
    void compatibilityCandidatesRemainDeterministic()
    {
        QTemporaryDir temp;
        const QStringList suffixes{"mp3", "wav", "aac", "flac", "ogg", "eac3", "wma", "ac3", "alac", "mkv", "wmv", "avi", "mpeg4"};
        for (const auto &suffix : suffixes) QVERIFY(writeFile(temp.filePath("fixture." + suffix)));
        QVERIFY(writeFile(temp.filePath("fixture.txt")));
        const auto result = LocalSourceScanner().scan(configFor(temp.path()), std::atomic_bool{false});
        QCOMPARE(result.entries.size(), 13);
        for (const auto &entry : result.entries) QCOMPARE(entry.item.title, QString("fixture"));
    }
    void sidecarPayloads()
    {
        QTemporaryDir temp;
        QTemporaryDir outside;
        const QString path = temp.filePath("song.mp3");
        const QString root = QFileInfo(temp.path()).canonicalFilePath();
        const QByteArray embedded = png(Qt::red);
        QVERIFY(writeFile(path, taggedMp3(embedded)));
        QVERIFY(writeFile(temp.filePath("song.lrc"), "[00:01.00]sidecar\n"));
        QVERIFY(writeFile(temp.filePath("cover.png"), png(Qt::blue)));
        QCOMPARE(LocalMediaFiles::lyricsText(path, root), QString("[00:01.00]sidecar\n"));
        auto payload = LocalMediaFiles::artworkPayload(path, root);
        QCOMPARE(payload.value("bytes").toByteArray(), embedded);
        QCOMPARE(payload.value("mimeType").toString(), QString("image/png"));
        QVERIFY(QFile::remove(temp.filePath("song.lrc")));
        QVERIFY(LocalMediaFiles::lyricsText(path, root).contains("embedded"));
        const QString plain = temp.filePath("plain.mp3");
        QVERIFY(writeFile(plain));
        QCOMPARE(LocalMediaFiles::artworkPayload(plain, root).value("bytes").toByteArray(), png(Qt::blue));
        QVERIFY(LocalMediaFiles::lyricsText(plain, root).isEmpty());
        QVERIFY(QFile::remove(temp.filePath("cover.png")));
        QVERIFY(writeFile(outside.filePath("secret.png"), png(Qt::green)));
        QVERIFY(writeFile(outside.filePath("secret.lrc"), "[00:01.00]secret"));
        QVERIFY(QFile::link(outside.filePath("secret.png"), temp.filePath("cover.png")));
        QVERIFY(QFile::link(outside.filePath("secret.lrc"), temp.filePath("plain.lrc")));
        QVERIFY(LocalMediaFiles::artworkPayload(plain, root).isEmpty());
        QVERIFY(LocalMediaFiles::lyricsText(plain, root).isEmpty());
        QVERIFY(LocalMediaFiles::artworkPayload(outside.filePath("secret.png"), root).isEmpty());
    }
    void unsynchronizedLyricsRemainComplete()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath("plain.mp3");
        QVERIFY(writeFile(path, taggedMp3({}, "first line\nsecond line")));
        const QString root = QFileInfo(temp.path()).canonicalFilePath();
        const auto lines = LocalMediaFiles::parseLrc(LocalMediaFiles::lyricsText(path, root));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).toMap().value("text").toString(), QString("first line"));
        QCOMPARE(lines.at(1).toMap().value("text").toString(), QString("second line"));
    }
};
QTEST_GUILESS_MAIN(LocalSourceScannerTest)
#include "tst_LocalSourceScanner.moc"
