#include <QFile>
#include <QString>
#include <QTest>

class OriginalUiStructureTest final : public QObject
{
    Q_OBJECT

private:
    static QString readSource(const QString &relativePath)
    {
        QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/") + relativePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(file.readAll());
    }

private slots:
    void musicShellMatchesRequiredStructure()
    {
        const QString sidebar = readSource(QStringLiteral("layout/LeftSideBar.qml"));
        QCOMPARE(sidebar.count(QStringLiteral("ListElement {")), 6);
        QVERIFY(sidebar.contains(QStringLiteral("display: \"推荐\"")));
        QVERIFY(sidebar.contains(QStringLiteral("display: \"分类\"")));
        QVERIFY(sidebar.contains(QStringLiteral("display: \"收藏\"")));
        QVERIFY(sidebar.contains(QStringLiteral("display: \"本地\"")));
        QVERIFY(sidebar.contains(QStringLiteral("display: \"下载\"")));
        QVERIFY(sidebar.contains(QStringLiteral("display: \"\"")));
        QVERIFY(!sidebar.contains(QStringLiteral("display: \"音乐源\"")));

        const QString mainContent = readSource(QStringLiteral("layout/MainContent.qml"));
        QVERIFY(mainContent.contains(QStringLiteral("ParallelAnimation")));
        QVERIFY(mainContent.contains(QStringLiteral("id: pageAnine")));
        QVERIFY(mainContent.contains(QStringLiteral("duration: 320")));
        QVERIFY(mainContent.contains(QStringLiteral("sourceComponent: HomePage")));
        QVERIFY(mainContent.contains(QStringLiteral("sourceComponent: PlaylistPage")));
        QVERIFY(!mainContent.contains(QStringLiteral("MusicSectionView")));
        QVERIFY(!mainContent.contains(QStringLiteral("color: \"white\"")));

        const QString home = readSource(QStringLiteral("pages/HomePage.qml"));
        QVERIFY(home.contains(QStringLiteral("QScrollView")));
        QVERIFY(!home.contains(QStringLiteral("MusicSectionView")));

        for (const QString &page : {QStringLiteral("pages/PlaylistPage.qml"),
                                    QStringLiteral("pages/FavouritePage.qml"),
                                    QStringLiteral("pages/SearchPage.qml")}) {
            const QString source = readSource(page);
            QVERIFY2(source.contains(QStringLiteral("QPages")), qPrintable(page));
            QVERIFY2(source.contains(QStringLiteral("QBlurTapBar")), qPrintable(page));
            QVERIFY2(!source.contains(QStringLiteral("MusicSectionView")), qPrintable(page));
            QVERIFY2(!source.contains(QStringLiteral("MediaBridge")), qPrintable(page));
            QVERIFY2(!source.contains(QStringLiteral("SourceManager")), qPrintable(page));
        }
    }
};

QTEST_GUILESS_MAIN(OriginalUiStructureTest)

#include "tst_OriginalUiStructure.moc"
