#include "IMusicSourceArtworkSession.h"
#include "IMusicSourcePlugin.h"
#include "SourceTypes.h"

#include <QJsonDocument>
#include <QTest>

class SourceTypesTest : public QObject {
    Q_OBJECT

private slots:
    void locksStableSourceIids();
    void roundTripsDescriptorWithCapabilities();
    void sourceAccountCarriesConnectionParametersWithoutChangingIdentity();
    void keepsProviderIdsNamespacedBySource();
    void preservesExpiringStreamHeadersAndMediaKind();
    void mapsUnknownErrorKindToUnknown();
};

void SourceTypesTest::locksStableSourceIids()
{
    QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID),
             QStringLiteral("org.quemusic.MusicSourcePlugin/1.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID),
             QStringLiteral("org.quemusic.MusicSourceArtworkSession/1.0"));
}

void SourceTypesTest::roundTripsDescriptorWithCapabilities()
{
    const SourceCapabilities capabilities =
        SourceCapability::Search |
        SourceCapability::Browse |
        SourceCapability::StreamAudio |
        SourceCapability::StreamVideo |
        SourceCapability::Artwork |
        SourceCapability::Lyrics |
        SourceCapability::PlaylistRead |
        SourceCapability::PlaylistWrite |
        SourceCapability::Favorites |
        SourceCapability::Download |
        SourceCapability::Scrobble;
    const SourceDescriptor original{
        QStringLiteral("nas.example"), QStringLiteral("NAS Example"),
        QStringLiteral("1.2.3"), QStringLiteral("quemusic-source-v1"),
        QStringLiteral("1.0"), capabilities};

    const SourceDescriptor roundTripped = sourceDescriptorFromJson(sourceDescriptorToJson(original));

    QCOMPARE(roundTripped.id, original.id);
    QCOMPARE(roundTripped.name, original.name);
    QCOMPARE(roundTripped.version, original.version);
    QCOMPARE(roundTripped.protocol, original.protocol);
    QCOMPARE(roundTripped.sdkVersion, original.sdkVersion);
    QCOMPARE(roundTripped.capabilities, original.capabilities);
    for (const SourceCapability capability : {
             SourceCapability::Search, SourceCapability::Browse,
             SourceCapability::StreamAudio, SourceCapability::StreamVideo,
             SourceCapability::Artwork, SourceCapability::Lyrics,
             SourceCapability::PlaylistRead, SourceCapability::PlaylistWrite,
             SourceCapability::Favorites, SourceCapability::Download,
             SourceCapability::Scrobble}) {
        QVERIFY(roundTripped.capabilities.testFlag(capability));
    }
}

void SourceTypesTest::sourceAccountCarriesConnectionParametersWithoutChangingIdentity()
{
    const SourceAccount account{
        QStringLiteral("navidrome"),
        QStringLiteral("admin"),
        QStringLiteral("Navidrome Admin"),
        {{QStringLiteral("serverUrl"), QStringLiteral("http://example.invalid:8533")},
         {QStringLiteral("username"), QStringLiteral("admin")}},
        QByteArrayLiteral("test-password")};

    QCOMPARE(account.sourceId, QStringLiteral("navidrome"));
    QCOMPARE(account.accountId, QStringLiteral("admin"));
    QCOMPARE(account.displayName, QStringLiteral("Navidrome Admin"));
    QCOMPARE(account.parameters.value(QStringLiteral("serverUrl")).toString(),
             QStringLiteral("http://example.invalid:8533"));
    QCOMPARE(account.parameters.value(QStringLiteral("username")).toString(),
             QStringLiteral("admin"));
    QCOMPARE(account.secret, QByteArrayLiteral("test-password"));
}

void SourceTypesTest::keepsProviderIdsNamespacedBySource()
{
    const TrackRef first{QStringLiteral("source-a"), QStringLiteral("42")};
    const TrackRef second{QStringLiteral("source-b"), QStringLiteral("42")};
    const TrackRef sameAsFirst{QStringLiteral("source-a"), QStringLiteral("42")};

    QVERIFY(first != second);
    QVERIFY(first == sameAsFirst);
}

void SourceTypesTest::preservesExpiringStreamHeadersAndMediaKind()
{
    const QDateTime expiresAt = QDateTime::fromString(
        QStringLiteral("2026-08-27T12:34:56Z"), Qt::ISODate);
    const StreamDescriptor original{
        {QStringLiteral("nas.example"), QStringLiteral("track-7")},
        QUrl(QStringLiteral("https://nas.example/track-7")),
        {{QStringLiteral("Authorization"), QStringLiteral("Bearer opaque")},
         {QStringLiteral("X-Device"), QStringLiteral("desktop")}},
        QStringLiteral("video/mp4"), expiresAt, true, false};
    const TrackRef expectedTrack{QStringLiteral("nas.example"), QStringLiteral("track-7")};

    QCOMPARE(original.track, expectedTrack);
    QCOMPARE(original.headers.value(QStringLiteral("Authorization")), QStringLiteral("Bearer opaque"));
    QCOMPARE(original.headers.value(QStringLiteral("X-Device")), QStringLiteral("desktop"));
    QCOMPARE(original.mimeType, QStringLiteral("video/mp4"));
    QCOMPARE(original.expiresAt, expiresAt);
    QVERIFY(original.video);
    QVERIFY(!original.seekable);
}

void SourceTypesTest::mapsUnknownErrorKindToUnknown()
{
    QCOMPARE(sourceErrorKindFromString(QStringLiteral("not-a-real-error")), SourceErrorKind::Unknown);
    QCOMPARE(sourceErrorKindFromString(QStringLiteral("network")), SourceErrorKind::Network);
}

QTEST_MAIN(SourceTypesTest)
#include "tst_SourceTypes.moc"
