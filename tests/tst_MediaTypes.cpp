#include "MediaTypes.h"

#include <QTest>

class MediaTypesTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsCompleteMediaIdentityThroughVariantMap();
    void treatsDifferentAccountsAsDifferentMediaIdentities();
};

void MediaTypesTest::roundTripsCompleteMediaIdentityThroughVariantMap()
{
    const MediaId original{QStringLiteral("navidrome"), QStringLiteral("home"),
                           QStringLiteral("song-1"), MediaKind::Track};

    const QVariantMap serialized = mediaIdToVariantMap(original);

    QCOMPARE(serialized.value(QStringLiteral("sourceId")), QStringLiteral("navidrome"));
    QCOMPARE(serialized.value(QStringLiteral("accountId")), QStringLiteral("home"));
    QCOMPARE(serialized.value(QStringLiteral("nativeId")), QStringLiteral("song-1"));
    QCOMPARE(serialized.value(QStringLiteral("kind")), static_cast<int>(MediaKind::Track));
    QCOMPARE(mediaIdFromVariantMap(serialized), original);
}

void MediaTypesTest::treatsDifferentAccountsAsDifferentMediaIdentities()
{
    const MediaId home{QStringLiteral("navidrome"), QStringLiteral("home"),
                       QStringLiteral("song-1"), MediaKind::Track};
    const MediaId work{QStringLiteral("navidrome"), QStringLiteral("work"),
                       QStringLiteral("song-1"), MediaKind::Track};

    QVERIFY(home != work);
}

QTEST_MAIN(MediaTypesTest)
#include "tst_MediaTypes.moc"
