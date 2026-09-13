#include <QAbstractListModel>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

class FakeListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit FakeListModel(QVariantList rows = {}, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_rows(std::move(rows))
    {
        for (const QVariant &row : m_rows) {
            const QVariantMap values = row.toMap();
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                m_roles.insert(Qt::UserRole + m_roles.size() + 1, it.key().toLatin1());
        }
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
        return m_rows.at(index.row()).toMap().value(QString::fromLatin1(m_roles.value(role)));
    }
    QHash<int, QByteArray> roleNames() const override { return m_roles; }
    Q_INVOKABLE QVariantMap get(int index) const
    {
        return index >= 0 && index < m_rows.size() ? m_rows.at(index).toMap() : QVariantMap{};
    }
    Q_INVOKABLE void clear() {}
    void setRows(QVariantList rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        m_roles.clear();
        for (const QVariant &row : m_rows) {
            const QVariantMap values = row.toMap();
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                m_roles.insert(Qt::UserRole + m_roles.size() + 1, it.key().toLatin1());
        }
        endResetModel();
        emit countChanged();
    }

signals:
    void countChanged();

private:
    QVariantList m_rows;
    QHash<int, QByteArray> m_roles;
};

class FakeOriginalUiMusic final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *recommendSongs READ recommendSongs CONSTANT)
    Q_PROPERTY(QObject *categoryItems READ categoryItems CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions CONSTANT)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    FakeOriginalUiMusic() = default;

    QObject *recommendSongs() { return &recommend; }
    QObject *categoryItems() { return &category; }
    QVariantList sourceOptions() const
    {
        return QVariantList{QVariantMap{{QStringLiteral("sourceInstanceId"), QString{}},
                                         {QStringLiteral("displayName"), QStringLiteral("全部音源")},
                                         {QStringLiteral("available"), true}},
                            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("source-b")},
                                         {QStringLiteral("displayName"), QStringLiteral("Source B")},
                                         {QStringLiteral("available"), true}}};
    }
    QString selectedSourceInstanceId() const { return selected; }
    void setSelectedSourceInstanceId(const QString &value)
    {
        selected = value;
        emit selectedSourceInstanceIdChanged();
    }

    Q_INVOKABLE void activatePage(int page) { activatedPages << page; }
    Q_INVOKABLE void loadMore(int page, const QString &section) { moreRequests << qMakePair(page, section); }
    Q_INVOKABLE bool browse(const QVariantMap &row) { browsedRows << row; return true; }
    Q_INVOKABLE void play(const QVariantMap &row) { playedRows << row; }
    Q_INVOKABLE void enqueue(const QVariantMap &row) { enqueuedRows << row; }

    FakeListModel recommend;
    FakeListModel category;
    QList<int> activatedPages;
    QList<QPair<int, QString>> moreRequests;
    QList<QVariantMap> browsedRows;
    QList<QVariantMap> playedRows;
    QList<QVariantMap> enqueuedRows;

signals:
    void selectedSourceInstanceIdChanged();

private:
    QString selected;
};

QVariantList recommendationRows()
{
    return {QVariantMap{{QStringLiteral("title"), QStringLiteral("Mapped recommendation")},
                         {QStringLiteral("artist"), QStringLiteral("Mapped artist")},
                         {QStringLiteral("cover"), QString{}}, {QStringLiteral("duration"), 180},
                         {QStringLiteral("_adapterKey"), 41ULL}}};
}

QVariantList categoryRows()
{
    return {QVariantMap{{QStringLiteral("title"), QStringLiteral("Mapped category")},
                         {QStringLiteral("artist"), QStringLiteral("Mapped curator")},
                         {QStringLiteral("cover"), QString{}}, {QStringLiteral("duration"), 0},
                         {QStringLiteral("_adapterKey"), 73ULL}}};
}

class FakeLegacyMusicApi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int songSource MEMBER songSource NOTIFY songSourceChanged)
    Q_PROPERTY(int nowIndex MEMBER nowIndex NOTIFY nowIndexChanged)
    Q_PROPERTY(QString globalid MEMBER globalid NOTIFY globalidChanged)
    Q_PROPERTY(QString globaltagid MEMBER globaltagid NOTIFY globaltagidChanged)
    Q_PROPERTY(int loadState MEMBER loadState NOTIFY loadStateChanged)
#define FAKE_LIST_PROPERTY(name) Q_PROPERTY(QObject *name READ name CONSTANT)
    FAKE_LIST_PROPERTY(getHotlistMenu)
    FAKE_LIST_PROPERTY(hotPlayLists)
    FAKE_LIST_PROPERTY(allPlaylistMenu)
    FAKE_LIST_PROPERTY(musicPlaylists)
    FAKE_LIST_PROPERTY(recommendSongs)
    FAKE_LIST_PROPERTY(playlistSong)
    FAKE_LIST_PROPERTY(personalFm)
    FAKE_LIST_PROPERTY(personalRadar)
    FAKE_LIST_PROPERTY(newSongs)
    FAKE_LIST_PROPERTY(toplistList)
    FAKE_LIST_PROPERTY(singerList)
#undef FAKE_LIST_PROPERTY
public:
    QObject *getHotlistMenu() { return &lists; } QObject *hotPlayLists() { return &lists; }
    QObject *allPlaylistMenu() { return &lists; } QObject *musicPlaylists() { return &lists; }
    QObject *recommendSongs() { return &lists; } QObject *playlistSong() { return &lists; }
    QObject *personalFm() { return &lists; } QObject *personalRadar() { return &lists; }
    QObject *newSongs() { return &lists; } QObject *toplistList() { return &lists; }
    QObject *singerList() { return &lists; }
    Q_INVOKABLE void getHotPlaylistMenu(int) {} Q_INVOKABLE void getHotPlaylists(int) {}
    Q_INVOKABLE void getPlaylistMenu(int) {} Q_INVOKABLE void getNewSongs(int, int, int) {}
    Q_INVOKABLE void getAllToplist() {} Q_INVOKABLE void getRecommendSongs(int, int, int = 0) {}
    Q_INVOKABLE void getMusicPlaylists(int, int, int) {} Q_INVOKABLE void getMenuInfo(int) {}
    Q_INVOKABLE void getMusicInfo(const QString &, int = 0, int = 0) {}
    Q_INVOKABLE void getPersonalFm(int, int, int) {} Q_INVOKABLE void getPersonalRadar(int, int, int) {}
    Q_INVOKABLE void getPlaylistSongs(const QString &, int, int) {}
    Q_INVOKABLE void getHotSingers(int, int, int) {} Q_INVOKABLE void getSingerCategory(int, int, int, int) {}
    Q_INVOKABLE void getSingerSongs(const QString &, int, int, int) {}
    Q_INVOKABLE void getMusicToplist(int, int, int, int) {}

    int songSource = 0;
    int nowIndex = 0;
    QString globalid;
    QString globaltagid;
    int loadState = 0;
signals:
    void songSourceChanged(); void nowIndexChanged(); void globalidChanged(); void globaltagidChanged(); void loadStateChanged();
private:
    FakeListModel lists;
};

class FakeCompletedStart final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool homeLoaded MEMBER homeLoaded NOTIFY homeLoadedChanged)
    Q_PROPERTY(bool playlistLoaded MEMBER playlistLoaded NOTIFY playlistLoadedChanged)
public:
    bool homeLoaded = false;
    bool playlistLoaded = false;
signals:
    void homeLoadedChanged();
    void playlistLoadedChanged();
};

class FakeWindow final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *completedStart READ completedStart CONSTANT)
    Q_PROPERTY(int exitIndex MEMBER exitIndex NOTIFY exitIndexChanged)
public:
    FakeCompletedStart completed;
    QObject *completedStart() { return &completed; }
    int exitIndex = 0;
signals:
    void exitIndexChanged();
};

class PageContext final {
public:
    explicit PageContext(QQmlEngine &engine)
    {
        const QVariantMap themes{{QStringLiteral("containColor"), QStringLiteral("#fff")}, {QStringLiteral("fontColor"), QStringLiteral("#222")}, {QStringLiteral("fullColor"), QStringLiteral("#fff")}, {QStringLiteral("hoverColor"), QStringLiteral("#10000000")}, {QStringLiteral("primaryBlurColor"), QStringLiteral("#eee")}, {QStringLiteral("primaryColor"), QStringLiteral("#fff")}, {QStringLiteral("secondaryColor"), QStringLiteral("#eee")}, {QStringLiteral("shadowColor"), QStringLiteral("#20000000")}, {QStringLiteral("sideColor"), QStringLiteral("#ddd")}, {QStringLiteral("textColor"), QStringLiteral("#555")}, {QStringLiteral("themeColor"), QStringLiteral("#3481fa")}, {QStringLiteral("borderColor"), QStringLiteral("#ddd")}, {QStringLiteral("blurSecondaryColor"), QStringLiteral("#eee")}};
        const QVariantMap settings{{QStringLiteral("cubeRadius"), 8}, {QStringLiteral("labelRadius"), 12}, {QStringLiteral("pageTitle"), 20}, {QStringLiteral("text"), 14}, {QStringLiteral("textH1"), 18}, {QStringLiteral("textH2"), 16}, {QStringLiteral("textTip"), 12}, {QStringLiteral("texticon"), 16}, {QStringLiteral("textmain"), 14}, {QStringLiteral("noControlRadius"), false}};
        QQmlContext *context = engine.rootContext();
        context->setContextProperty(QStringLiteral("MusicApi"), &musicApi);
        context->setContextProperty(QStringLiteral("window"), &window);
        context->setContextProperty(QStringLiteral("Style"), QVariantMap{{QStringLiteral("themes"), themes}, {QStringLiteral("settings"), settings}});
        const QVariantMap lastSong{{QStringLiteral("hash"), QString{}}, {QStringLiteral("name"), QString{}}, {QStringLiteral("artist"), QString{}}, {QStringLiteral("cover"), QString{}}, {QStringLiteral("source"), 0}};
        context->setContextProperty(QStringLiteral("Options"), QVariantMap{{QStringLiteral("lastSongs"), lastSong}, {QStringLiteral("settings"), QVariantMap{{QStringLiteral("soundQuality"), 0}}}});
        context->setContextProperty(QStringLiteral("iconFont"), QVariantMap{{QStringLiteral("name"), QString{}}});
        context->setContextProperty(QStringLiteral("favoritesList"), &lists); context->setContextProperty(QStringLiteral("favoritesSong"), &lists);
        context->setContextProperty(QStringLiteral("favoritesArtist"), &lists); context->setContextProperty(QStringLiteral("playListModel"), &lists);
        context->setContextProperty(QStringLiteral("mainWarn"), &lists); context->setContextProperty(QStringLiteral("mainLayout"), QVariantMap{{QStringLiteral("state"), QString{}}});
        context->setContextProperty(QStringLiteral("mainSearchInput"), QVariantMap{{QStringLiteral("text"), QString{}}});
    }
private:
    FakeLegacyMusicApi musicApi;
    FakeWindow window;
    FakeListModel lists;
};

namespace {
std::unique_ptr<QObject> loadPage(QQmlEngine &engine, const QString &page, FakeOriginalUiMusic *adapter, QString *error)
{
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/QueMusic/") + page));
    if (component.status() != QQmlComponent::Ready) { *error = component.errorString(); return {}; }
    auto object = std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("musicAdapter"), QVariant::fromValue(adapter)}}));
    if (!object) *error = component.errorString();
    return object;
}
}

class OriginalUiRecommendationQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void pagesActivateAndScopeWritesSelectedSource()
    {
        QQmlEngine engine; PageContext context(engine); FakeOriginalUiMusic adapter; QString error;
        auto home = loadPage(engine, QStringLiteral("pages/HomePage.qml"), &adapter, &error);
        QVERIFY2(home, qPrintable(error)); QTRY_COMPARE(adapter.activatedPages, QList<int>{0});
        QObject *scope = home->findChild<QObject *>(QStringLiteral("recommendationSourceScope"));
        QVERIFY(scope); QVERIFY(QMetaObject::invokeMethod(scope, "transformed", Q_ARG(int, 1)));
        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("source-b"));
        auto playlist = loadPage(engine, QStringLiteral("pages/PlaylistPage.qml"), &adapter, &error);
        QVERIFY2(playlist, qPrintable(error)); QTRY_VERIFY(adapter.activatedPages.contains(1));
    }

    void recommendationRowsPaginateAndPlayThroughAdapter()
    {
        QQmlEngine engine; PageContext context(engine); FakeOriginalUiMusic adapter; QString error;
        auto home = loadPage(engine, QStringLiteral("pages/HomePage.qml"), &adapter, &error);
        QVERIFY2(home, qPrintable(error));
        QObject *dailyWindow = home->findChild<QObject *>(QStringLiteral("dailyRecommendationWindow"));
        QVERIFY(dailyWindow);
        QVERIFY(QMetaObject::invokeMethod(dailyWindow, "opened",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Daily"))),
                                          Q_ARG(QVariant, QVariant(QString{}))));
        QTRY_VERIFY(home->findChild<QObject *>(QStringLiteral("recommendationList")));
        QObject *list = home->findChild<QObject *>(QStringLiteral("recommendationList"));
        QCOMPARE(list->property("model").value<QObject *>(), adapter.recommendSongs());
        adapter.recommend.setRows(recommendationRows());
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(list, "ended"));
        const QList<QPair<int, QString>> expectedMore{{0, QString{}}};
        QCOMPARE(adapter.moreRequests, expectedMore);
        QVERIFY(QMetaObject::invokeMethod(list, "clicked", Q_ARG(int, 0)));
        const QList<QVariantMap> expectedPlayed{QVariantMap{{QStringLiteral("_adapterKey"), 41ULL},
                                                             {QStringLiteral("title"), QStringLiteral("Mapped recommendation")},
                                                             {QStringLiteral("artist"), QStringLiteral("Mapped artist")},
                                                             {QStringLiteral("cover"), QString{}},
                                                             {QStringLiteral("duration"), 180}}};
        QCOMPARE(adapter.playedRows, expectedPlayed);
    }

    void categoryRowsUsePresentationIdentityForBrowse()
    {
        QQmlEngine engine; PageContext context(engine); FakeOriginalUiMusic adapter; QString error;
        auto home = loadPage(engine, QStringLiteral("pages/HomePage.qml"), &adapter, &error);
        QVERIFY2(home, qPrintable(error));
        QObject *list = home->findChild<QObject *>(QStringLiteral("recommendationCategoryList"));
        QVERIFY(list); QCOMPARE(list->property("model").value<QObject *>(), adapter.categoryItems());
        adapter.category.setRows(categoryRows());
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(home.get(), "browseCategory",
                                          Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(adapter.browsedRows.size(), 1);
        QCOMPARE(adapter.browsedRows.constFirst().value(QStringLiteral("_adapterKey")).toULongLong(), 73ULL);
        QVERIFY(!adapter.browsedRows.constFirst().contains(QStringLiteral("ref")));
    }
};

QTEST_MAIN(OriginalUiRecommendationQmlTest)
#include "tst_OriginalUiRecommendationQml.moc"
