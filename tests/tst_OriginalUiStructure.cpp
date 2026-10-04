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

    static void verifyInOrder(const QString &source, const QStringList &needles)
    {
        qsizetype previous = -1;
        for (const QString &needle : needles) {
            const qsizetype index = source.indexOf(needle);
            QVERIFY2(index > previous, qPrintable(needle + QStringLiteral(" is missing or out of order")));
            previous = index;
        }
    }

private slots:
    void musicShellMatchesRequiredStructure()
    {
        const QString sidebar = readSource(QStringLiteral("layout/LeftSideBar.qml"));
        QCOMPARE(sidebar.count(QStringLiteral("ListElement {")), 6);
        verifyInOrder(sidebar, {QStringLiteral("display: \"推荐\""),
                                QStringLiteral("display: \"分类\""),
                                QStringLiteral("display: \"\""),
                                QStringLiteral("display: \"收藏\""),
                                QStringLiteral("display: \"本地\""),
                                QStringLiteral("display: \"下载\"")});
        QVERIFY(!sidebar.contains(QStringLiteral("display: \"音乐源\"")));

        const QString mainContent = readSource(QStringLiteral("layout/MainContent.qml"));
        QVERIFY(mainContent.contains(QStringLiteral("ParallelAnimation")));
        QVERIFY(mainContent.contains(QStringLiteral("id: pageAnine")));
        QVERIFY(mainContent.contains(QStringLiteral("duration: 320")));
        QVERIFY(mainContent.contains(QStringLiteral("sourceComponent: HomePage")));
        QVERIFY(mainContent.contains(QStringLiteral("sourceComponent: PlaylistPage")));
        verifyInOrder(mainContent, {QStringLiteral("id: homePage"),
                                    QStringLiteral("id: playlistPage"),
                                    QStringLiteral("id: favouritePage"),
                                    QStringLiteral("id: filePage"),
                                    QStringLiteral("id: downloadPage"),
                                    QStringLiteral("id: searchPage")});
        for (const QString &loader : {QStringLiteral("homePage"),
                                      QStringLiteral("playlistPage"),
                                      QStringLiteral("favouritePage"),
                                      QStringLiteral("filePage"),
                                      QStringLiteral("downloadPage"),
                                      QStringLiteral("searchPage")}) {
            const qsizetype loaderStart = mainContent.indexOf(QStringLiteral("id: ") + loader);
            QVERIFY2(loaderStart >= 0, qPrintable(loader));
            const QString loaderSource = mainContent.mid(loaderStart, 520);
            QVERIFY2(loaderSource.contains(QStringLiteral("objectName: \"") + loader
                                           + QStringLiteral("Loader\"")), qPrintable(loader));
            QVERIFY2(loaderSource.contains(QStringLiteral("active:")), qPrintable(loader));
            QVERIFY2(loaderSource.contains(QStringLiteral("visible:")), qPrintable(loader));
            QVERIFY2(loaderSource.contains(QStringLiteral("onLoaded: { visible = true;")),
                     qPrintable(loader));
        }
        QVERIFY(mainContent.contains(QStringLiteral("source: \"qrc:/QueMusic/pages/FilePage.qml\"")));
        QVERIFY(mainContent.contains(QStringLiteral("source: \"qrc:/QueMusic/pages/DownloadPage.qml\"")));
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

    void adapterSearchEntryAndTabsDoNotUseLegacyRequests()
    {
        const QString main = readSource(QStringLiteral("main.qml"));
        QCOMPARE(main.count(QStringLiteral("window.musicAdapter.search(")), 3);
        QVERIFY(!main.contains(QStringLiteral("MusicApi.searchSongs(")));

        const QString search = readSource(QStringLiteral("pages/SearchPage.qml"));
        QVERIFY(search.contains(QStringLiteral("objectName: \"searchSongsList\"")));
        QVERIFY(search.contains(QStringLiteral("objectName: \"searchListsList\"")));
        QVERIFY(search.contains(QStringLiteral("objectName: \"searchAlbumsList\"")));
        QVERIFY(search.contains(QStringLiteral("objectName: \"searchLyricsList\"")));
        QVERIFY(search.contains(QStringLiteral("musicAdapter.search(mainSearchInput.text, index)")));
        QVERIFY(!search.contains(QStringLiteral("MusicApi.")));
        QVERIFY(!search.contains(QStringLiteral("playListModel")));
        QVERIFY(!search.contains(QStringLiteral("酷狗音乐")));
        QVERIFY(!search.contains(QStringLiteral("网易云音乐")));

        const QString list = readSource(QStringLiteral("components/QListView.qml"));
        QVERIFY(list.contains(QStringLiteral("property var toolText0ForRow")));
        QVERIFY(list.contains(QStringLiteral("property var toolText1ForRow")));
        QVERIFY(list.contains(QStringLiteral("visible: parent.tool0.length > 0")));
        QVERIFY(list.contains(QStringLiteral("visible: parent.tool1.length > 0")));
    }

    void localPlaybackHasNoQmlPathBypass()
    {
        const QString main = readSource(QStringLiteral("main.qml"));
        const QString files = readSource(QStringLiteral("pages/FilePage.qml"));
        const QString downloads = readSource(QStringLiteral("pages/DownloadPage.qml"));
        const QString player = readSource(QStringLiteral("layout/PlayerControl.qml"));
        QVERIFY(!main.isEmpty() && !files.isEmpty() && !downloads.isEmpty() && !player.isEmpty());
        for (const QString &source : {main, files, downloads, player}) {
            QVERIFY(!source.contains(QStringLiteral("playLocalSong(")));
            QVERIFY(!source.contains(QStringLiteral("mainMedia.source = path")));
            QVERIFY(!source.contains(QStringLiteral("source: -1")));
        }
        QVERIFY(files.contains(QStringLiteral("return status !== \"matched\";")));
        QVERIFY(files.contains(QStringLiteral("legacyCollectionMigration.playSong(")));
        QVERIFY(files.contains(QStringLiteral("legacyCollectionMigration.enqueueSong(")));
        QVERIFY(player.contains(QStringLiteral("if(source == -1)")));
        QVERIFY(player.contains(QStringLiteral("旧本地队列歌曲已停止直播放")));
        QVERIFY(downloads.contains(QStringLiteral("请先在本地音乐插件中导入下载目录")));
    }
};

QTEST_GUILESS_MAIN(OriginalUiStructureTest)

#include "tst_OriginalUiStructure.moc"
