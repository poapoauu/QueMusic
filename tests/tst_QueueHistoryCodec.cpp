#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/music/QueueHistoryCodec.h"

namespace {
MediaRefV2 ref(QString source = "local", QString id = "00112233-4455-4677-8899-aabbccddeeff")
{
    return {source, source + "/home", "account", MediaEntityTypeV2::Track, id};
}

QueueOccurrence occurrence(const MediaRefV2 &media = ref())
{
    return {QUuid::createUuid(), media, QString::fromUtf8("海🌊"), {"Artist"}, "Album", 123456, true};
}

QJsonObject encoded(const QueueHistorySnapshot &snapshot)
{
    const auto bytes = QueueHistoryCodec::encode(snapshot);
    return bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject{};
}

QByteArray bytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

class QueueHistoryCodecTest : public QObject
{
    Q_OBJECT
private slots:
    void mixedQueueRoundTripsWithoutLocators()
    {
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence(), occurrence(ref("navidrome", "remote-123"))};
        RecentPlay play;
        play.occurrenceId = snapshot.queue.at(1).occurrenceId;
        play.ref = snapshot.queue.at(1).ref;
        play.title = "Remote";
        play.artists = {"One", "Two"};
        play.album = "Album";
        play.durationMs = 2000;
        play.playedAt = QDateTime::fromString("2026-09-29T10:20:30.123+08:00", Qt::ISODateWithMs);
        snapshot.history = {play};
        snapshot.legacyImportVersion = 1;

        const auto data = QueueHistoryCodec::encode(snapshot);
        QVERIFY(data.has_value());
        const auto root = QJsonDocument::fromJson(*data).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 1);
        QCOMPARE(root.keys().size(), 4);
        QCOMPARE(root.value("queue").toArray().size(), 2);
        const auto first = root.value("queue").toArray().at(0).toObject();
        QCOMPARE(first.keys().size(), 7);
        const auto identity = first.value("ref").toObject();
        QCOMPARE(identity.keys().size(), 5);
        QCOMPARE(identity.value("sourcePluginId").toString(), QString("local"));
        QCOMPARE(identity.value("sourceInstanceId").toString(), QString("local/home"));
        QCOMPARE(identity.value("accountId").toString(), QString("account"));
        QCOMPARE(identity.value("entityType").toInt(), static_cast<int>(MediaEntityTypeV2::Track));
        QCOMPARE(identity.value("entityId").toString(), snapshot.queue.at(0).ref.entityId);
        QVERIFY(!data->contains("availableActions"));
        QVERIFY(!data->contains("metadata"));
        QVERIFY(!data->contains("token"));
        QVERIFY(!data->contains("file://"));
        QVERIFY(!data->contains("http://"));
        QVERIFY(!data->contains("https://"));
        const auto decoded = QueueHistoryCodec::decode(*data);
        QCOMPARE(decoded.status, QueueHistoryDecodeStatus::Ok);
        QCOMPARE(decoded.snapshot.queue.size(), 2);
        QCOMPARE(decoded.snapshot.queue.at(0).ref, snapshot.queue.at(0).ref);
        QCOMPARE(decoded.snapshot.queue.at(1).ref, snapshot.queue.at(1).ref);
        QCOMPARE(decoded.snapshot.queue.at(0).title, snapshot.queue.at(0).title);
        QCOMPARE(decoded.snapshot.history.at(0).playedAt.timeSpec(), Qt::UTC);
        QCOMPARE(decoded.snapshot.history.at(0).playedAt.toString(Qt::ISODateWithMs),
                 QString("2026-09-29T02:20:30.123Z"));
        QCOMPARE(decoded.snapshot.legacyImportVersion, 1);
    }

    void rejectsMalformedAndFutureVersion()
    {
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence()};
        auto root = encoded(snapshot);
        QCOMPARE(QueueHistoryCodec::decode("{").status, QueueHistoryDecodeStatus::Corrupt);
        root.insert("schemaVersion", 2);
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::UnsupportedVersion);
        root.insert("schemaVersion", 1);
        root.insert("token", "secret");
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        root.remove("token");
        auto item = root.value("queue").toArray().at(0).toObject();
        item.insert("availableActions", QJsonArray{});
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        item.remove("availableActions");
        auto identity = item.value("ref").toObject();
        identity.insert("entityId", "file:///private/song.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "https://host/stream?token=secret");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "/Users/person/song.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "./song.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "~/Music/song.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "C:song.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "C:song:alternate.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        identity.insert("entityId", "C:/song:alternate.mp3");
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
        snapshot.queue[0].ref.entityId = "file:///private/song.mp3";
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].ref.entityId = "https://host/stream?token=secret";
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].ref.entityId = "C:song:alternate.mp3";
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].ref = ref();
        snapshot.queue[0].ref.entityType = MediaEntityTypeV2::Directory;
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].ref = ref();
        root = encoded(snapshot);
        item = root.value("queue").toArray().at(0).toObject();
        identity = item.value("ref").toObject();
        identity.insert("entityType", int(MediaEntityTypeV2::Directory));
        item.insert("ref", identity);
        root.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
    }

    void opaqueColonIdRoundTrips()
    {
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence(ref("navidrome", "song:https://suffix")),
                          occurrence(ref("navidrome", "track:https://suffix"))};
        const auto data = QueueHistoryCodec::encode(snapshot);
        QVERIFY(data.has_value());
        const auto decoded = QueueHistoryCodec::decode(*data);
        QCOMPARE(decoded.status, QueueHistoryDecodeStatus::Ok);
        QCOMPARE(decoded.snapshot.queue.at(0).ref.entityId, QString("song:https://suffix"));
        QCOMPARE(decoded.snapshot.queue.at(1).ref.entityId, QString("track:https://suffix"));
    }

    void versionedInputCannotImportAsLegacy()
    {
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence()};
        auto future = encoded(snapshot);
        future.insert("schemaVersion", 2);
        const auto rejectedFuture = QueueHistoryCodec::importLegacy(bytes(future));
        QVERIFY(!rejectedFuture.parsed);
        QVERIFY(rejectedFuture.accepted.isEmpty());
        auto current = encoded(snapshot);
        const auto rejectedCurrent = QueueHistoryCodec::importLegacy(bytes(current));
        QVERIFY(!rejectedCurrent.parsed);
        QVERIFY(rejectedCurrent.accepted.isEmpty());
    }

    void enforcesAllSizeLimits()
    {
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence()};
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].title = QString(513, 'x');
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].title = QString(512, 'x');
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].title = QString::fromUtf8("🌊").repeated(512);
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].title += QString::fromUtf8("🌊");
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].title = "Title";
        snapshot.queue[0].artists = QStringList(17, "x");
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].artists = QStringList(16, "x");
        snapshot.queue[0].ref.entityId = QString(1025, 'x');
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue[0].ref.entityId = QString(1024, 'x');
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        const auto valid = encoded(snapshot);
        auto item = valid.value("queue").toArray().at(0).toObject();
        item.insert("album", QString(513, 'a'));
        auto invalid = valid;
        invalid.insert("queue", QJsonArray{item});
        QCOMPARE(QueueHistoryCodec::decode(bytes(invalid)).status, QueueHistoryDecodeStatus::TooLarge);

        snapshot.queue.clear();
        for (int i = 0; i < 1001; ++i) snapshot.queue.append(occurrence());
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue.resize(1000);
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.queue.clear();
        RecentPlay play;
        play.occurrenceId = QUuid::createUuid();
        play.ref = ref();
        play.playedAt = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 201; ++i) snapshot.history.append(play);
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        snapshot.history.resize(200);
        QVERIFY(QueueHistoryCodec::encode(snapshot).has_value());
        auto tooMany = encoded(snapshot);
        QJsonArray oversizedHistory;
        for (int i = 0; i < 201; ++i) oversizedHistory.append(QJsonObject{});
        tooMany.insert("history", oversizedHistory);
        QCOMPARE(QueueHistoryCodec::decode(bytes(tooMany)).status, QueueHistoryDecodeStatus::TooLarge);
        QCOMPARE(QueueHistoryCodec::decode(QByteArray(16 * 1024 * 1024 + 1, 'x')).status,
                 QueueHistoryDecodeStatus::TooLarge);

        snapshot.history.clear();
        const QString largeArtist(512, QChar(0x6d77));
        for (int i = 0; i < 1000; ++i) {
            auto item = occurrence();
            item.artists = QStringList(16, largeArtist);
            snapshot.queue.append(item);
        }
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
    }

    void duplicateOccurrencesStayDistinct()
    {
        QueueHistorySnapshot snapshot;
        auto first = occurrence();
        auto second = occurrence(first.ref);
        snapshot.queue = {first, second};
        const auto decoded = QueueHistoryCodec::decode(*QueueHistoryCodec::encode(snapshot));
        QCOMPARE(decoded.status, QueueHistoryDecodeStatus::Ok);
        QCOMPARE(decoded.snapshot.queue.at(0).ref, decoded.snapshot.queue.at(1).ref);
        QVERIFY(decoded.snapshot.queue.at(0).occurrenceId != decoded.snapshot.queue.at(1).occurrenceId);
        snapshot.queue[1].occurrenceId = first.occurrenceId;
        QVERIFY(!QueueHistoryCodec::encode(snapshot).has_value());
        auto root = encoded(QueueHistorySnapshot{{first, second}, {}, 0});
        auto queue = root.value("queue").toArray();
        auto item = queue.at(1).toObject();
        item.insert("occurrenceId", queue.at(0).toObject().value("occurrenceId"));
        queue[1] = item;
        root.insert("queue", queue);
        QCOMPARE(QueueHistoryCodec::decode(bytes(root)).status, QueueHistoryDecodeStatus::Corrupt);
    }

    void legacyInputOnlyAcceptsCompleteOpaqueRefs()
    {
        const auto valid = occurrence();
        auto good = encoded(QueueHistorySnapshot{{valid}, {}, 0}).value("queue").toArray().at(0).toObject();
        auto pathOnly = QJsonObject{{"path", "/private/song.mp3"}, {"hash", "abc"}, {"source", 1}};
        auto pathRef = good;
        auto identity = pathRef.value("ref").toObject();
        identity.insert("entityId", "file:///private/song.mp3");
        pathRef.insert("ref", identity);
        auto incomplete = good;
        identity = incomplete.value("ref").toObject();
        identity.remove("sourceInstanceId");
        incomplete.insert("ref", identity);
        const auto completeRefOnly = QJsonObject{{"ref", good.value("ref")},
                                                 {"title", "Imported"},
                                                 {"path", "/ignored/legacy/path"},
                                                 {"metadata", QJsonObject{{"token", "discard"}}}};
        const auto legacy = bytes(QJsonObject{{"queue", QJsonArray{good, completeRefOnly,
                                                                    pathOnly, pathRef, incomplete}}});
        const auto result = QueueHistoryCodec::importLegacy(legacy);
        QVERIFY(result.parsed);
        QCOMPARE(result.accepted.size(), 2);
        QCOMPARE(result.accepted.at(0).ref, valid.ref);
        QCOMPARE(result.accepted.at(1).ref, valid.ref);
        QVERIFY(!result.accepted.at(1).occurrenceId.isNull());
        QVERIFY(result.accepted.at(0).occurrenceId != result.accepted.at(1).occurrenceId);
        const auto importedBytes = QueueHistoryCodec::encode({result.accepted, {}, 1});
        QVERIFY(importedBytes.has_value());
        QVERIFY(!importedBytes->contains("token"));
        QCOMPARE(result.rejected, 3);
        QVERIFY(!QueueHistoryCodec::importLegacy("not json").parsed);
        QVERIFY(!QueueHistoryCodec::importLegacy(QByteArray(16 * 1024 * 1024 + 1, 'x')).parsed);
    }
};

QTEST_MAIN(QueueHistoryCodecTest)
#include "tst_QueueHistoryCodec.moc"
