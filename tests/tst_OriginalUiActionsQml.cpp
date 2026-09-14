#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QQuickItem>
#include <QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

class FakeListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString sectionId MEMBER sectionId)
    Q_PROPERTY(bool hasMore MEMBER hasMore)
    Q_PROPERTY(bool loadingMore MEMBER loadingMore)
    Q_PROPERTY(QVariantMap error MEMBER error)
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
    { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override
    { return index.isValid() && index.row() >= 0 && index.row() < m_rows.size()
        ? m_rows.at(index.row()).toMap().value(QString::fromLatin1(m_roles.value(role))) : QVariant{}; }
    QHash<int, QByteArray> roleNames() const override { return m_roles; }
    Q_INVOKABLE QVariantMap get(int index) const
    { return index >= 0 && index < m_rows.size() ? m_rows.at(index).toMap() : QVariantMap{}; }
    Q_INVOKABLE void clear() { setRows({}); }
    Q_INVOKABLE bool isFavorite(const QString &, const QString &) { ++legacyFavoriteCalls; return false; }
    Q_INVOKABLE void addFavorite(const QString &, const QString &, const QString &, const QString &, int, int, const QString &) { ++legacyFavoriteCalls; }
    Q_INVOKABLE void removeFavorite(const QString &, const QString &) { ++legacyFavoriteCalls; }
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
    int legacyFavoriteCalls = 0;
    QString sectionId = QStringLiteral("section-song");
    bool hasMore = true;
    bool loadingMore = false;
    QVariantMap error;
signals:
    void countChanged();
private:
    QVariantList m_rows; QHash<int, QByteArray> m_roles;
};

class FakeAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *favoriteSongs READ favoriteSongs CONSTANT)
    Q_PROPERTY(QObject *favoriteLists READ favoriteLists CONSTANT)
    Q_PROPERTY(QObject *categoryItems READ categoryItems CONSTANT)
    Q_PROPERTY(QObject *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(QObject *searchLists READ searchLists CONSTANT)
    Q_PROPERTY(QObject *searchAlbums READ searchAlbums CONSTANT)
    Q_PROPERTY(QObject *searchLyrics READ searchLyrics CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions CONSTANT)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    FakeAdapter()
        : songs(QVariantList{row(QStringLiteral("Song"), QStringLiteral("favorite-songs")),
                             row(QStringLiteral("Unavailable"), QStringLiteral("favorite-songs"),
                                 true, false, false, false, false)}),
          lists(QVariantList{row(QStringLiteral("List"), QStringLiteral("favorite-lists"),
                                 true, false, true, true, true)}),
          categoryRows(QVariantList{row(QStringLiteral("Detail song"),
                                        QStringLiteral("detail-tracks"))}),
          searchRows(QVariantList{row(QStringLiteral("Search song"),
                                      QStringLiteral("search-songs"))}),
          searchListRows(QVariantList{row(QStringLiteral("Search list"),
                                          QStringLiteral("search-lists"),
                                          true, false, true, false, true)}),
          searchAlbumRows(QVariantList{row(QStringLiteral("Search album"),
                                           QStringLiteral("search-albums"),
                                           true, false, false, false, true)}),
          searchLyricRows(QVariantList{row(QStringLiteral("Search lyric"),
                                           QStringLiteral("search-lyrics"))})
    {
        lists.sectionId = QStringLiteral("favorite-lists");
        categoryRows.sectionId = QStringLiteral("detail-tracks");
        searchRows.sectionId = QStringLiteral("search-songs");
        searchListRows.sectionId = QStringLiteral("search-lists");
        searchAlbumRows.sectionId = QStringLiteral("search-albums");
        searchLyricRows.sectionId = QStringLiteral("search-lyrics");
    }
    QObject *favoriteSongs() { return &songs; }
    QObject *favoriteLists() { return &lists; }
    QObject *categoryItems() { return &categoryRows; }
    QObject *searchSongs() { return &searchRows; }
    QObject *searchLists() { return &searchListRows; }
    QObject *searchAlbums() { return &searchAlbumRows; }
    QObject *searchLyrics() { return &searchLyricRows; }
    QVariantList sourceOptions() const
    {
        return {QVariantMap{{QStringLiteral("sourceInstanceId"), QString{}},
                            {QStringLiteral("displayName"), QStringLiteral("All")},
                            {QStringLiteral("available"), true}},
                QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("adapter/home")},
                            {QStringLiteral("displayName"), QStringLiteral("Home")},
                            {QStringLiteral("available"), true}}};
    }
    QString selectedSourceInstanceId() const { return selectedSource; }
    void setSelectedSourceInstanceId(const QString &value)
    {
        if (selectedSource == value) return;
        selectedSource = value;
        emit selectedSourceInstanceIdChanged();
    }
    Q_INVOKABLE QVariantMap capabilities(const QVariant &value) const
    {
        QVariantList rows;
        if (value.metaType().id() == QMetaType::QVariantMap) rows.append(value);
        else rows = value.toList();
        QVariantMap result{{QStringLiteral("canPlay"), !rows.isEmpty()},
                           {QStringLiteral("canEnqueue"), !rows.isEmpty()},
                           {QStringLiteral("canFavorite"), !rows.isEmpty()},
                           {QStringLiteral("canUnfavorite"), !rows.isEmpty()},
                           {QStringLiteral("canBrowse"), !rows.isEmpty()}};
        for (const QVariant &rowValue : rows) {
            const QVariantMap row = rowValue.toMap();
            for (const QString &name : {QStringLiteral("Play"), QStringLiteral("Enqueue"),
                                        QStringLiteral("Favorite"), QStringLiteral("Unfavorite"),
                                        QStringLiteral("Browse")}) {
                const QString key = QStringLiteral("can") + name;
                if (!row.value(key).toBool()) result.insert(key, false);
            }
        }
        return result;
    }
    Q_INVOKABLE void activatePage(int page) { activated << page; }
    Q_INVOKABLE void search(const QString &text, int tab = 0) { searches << qMakePair(text, tab); }
    Q_INVOKABLE void loadMore(int page, const QString &section) { more << qMakePair(page, section); }
    Q_INVOKABLE void retry(int page, const QString &section) { retries << qMakePair(page, section); }
    Q_INVOKABLE bool browse(const QVariantMap &value) { browsed << value; return true; }
    Q_INVOKABLE QUuid play(const QVariantMap &value) { played << value; return QUuid::createUuid(); }
    Q_INVOKABLE QUuid enqueue(const QVariantMap &value) { enqueued << value; return QUuid::createUuid(); }
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &value, bool favorite) { favorites << qMakePair(value, favorite); return QUuid::createUuid(); }
    static QVariantMap row(const QString &title, const QString &sectionId,
                           bool canPlay = true, bool canEnqueue = true,
                           bool canFavorite = true, bool canUnfavorite = true,
                           bool canBrowse = false)
    {
        return {{QStringLiteral("title"), title},
                {QStringLiteral("artist"), QStringLiteral("Artist")},
                {QStringLiteral("cover"), QString{}},
                {QStringLiteral("duration"), 180},
                {QStringLiteral("sectionId"), sectionId},
                {QStringLiteral("hasMore"), true},
                {QStringLiteral("loadingMore"), false},
                {QStringLiteral("error"), QVariantMap{}},
                {QStringLiteral("canPlay"), canPlay},
                {QStringLiteral("canEnqueue"), canEnqueue},
                {QStringLiteral("canFavorite"), canFavorite},
                {QStringLiteral("canUnfavorite"), canUnfavorite},
                {QStringLiteral("canBrowse"), canBrowse},
                {QStringLiteral("_adapterKey"), 42ULL}};
    }
    FakeListModel songs, lists, categoryRows, searchRows, searchListRows,
        searchAlbumRows, searchLyricRows;
    QList<int> activated;
    QList<QPair<QString, int>> searches;
    QList<QPair<int, QString>> more, retries;
    QList<QVariantMap> played, enqueued, browsed;
    QList<QPair<QVariantMap, bool>> favorites;
    QString selectedSource;
signals:
    void selectedSourceInstanceIdChanged();
};

class FakeDownloader final : public QObject { Q_OBJECT Q_PROPERTY(int completedCount MEMBER completedCount NOTIFY completedCountChanged) Q_PROPERTY(int taskCount MEMBER taskCount NOTIFY taskCountChanged) Q_PROPERTY(bool hasActiveTasks MEMBER hasActiveTasks NOTIFY taskCountChanged) public: Q_INVOKABLE QString effectiveDownloadDir() const { return QDir::tempPath(); } Q_INVOKABLE void removeTask(const QString &) {} Q_INVOKABLE void retryTask(const QString &) {} int completedCount = 0; int taskCount = 0; bool hasActiveTasks = false; signals: void completedCountChanged(); void taskCountChanged(); };
class FakeDownloadedMusicModel : public QAbstractListModel { Q_OBJECT Q_PROPERTY(QString downloadDir MEMBER downloadDir NOTIFY downloadDirChanged) Q_PROPERTY(int count READ rowCount NOTIFY countChanged) public: int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 0; } QVariant data(const QModelIndex &, int) const override { return {}; } Q_INVOKABLE void reload() {} Q_INVOKABLE QVariantMap get(int) const { return {}; } QString downloadDir; signals: void downloadDirChanged(); void countChanged(); };
class FakeMusicApi final : public QObject { Q_OBJECT Q_PROPERTY(QObject *searchSongsResults READ searchSongsResults CONSTANT) Q_PROPERTY(QObject *playlistSong READ playlistSong CONSTANT) Q_PROPERTY(QObject *downloader READ downloader CONSTANT) Q_PROPERTY(int songSource MEMBER songSource NOTIFY songSourceChanged) Q_PROPERTY(int nowIndex MEMBER nowIndex NOTIFY nowIndexChanged) public: QObject *searchSongsResults() { return &rows; } QObject *playlistSong() { return &rows; } QObject *downloader() { return &downloaderValue; } Q_INVOKABLE void searchSongs(const QString &, int, int, int) { ++legacySearches; } Q_INVOKABLE void getMusicInfo(const QString &, int = 0, int = 0) { ++legacyPlays; } Q_INVOKABLE void getPlaylistSongs(const QString &, int, int, int = 0) { ++legacyPlaylistRequests; } FakeListModel rows; FakeDownloader downloaderValue; int songSource = 0, nowIndex = 0, legacySearches = 0, legacyPlays = 0, legacyPlaylistRequests = 0; signals: void songSourceChanged(); void nowIndexChanged(); void downloadPathChanged(); };
class FakeWindow final : public QObject { Q_OBJECT Q_PROPERTY(int exitIndex MEMBER exitIndex NOTIFY exitIndexChanged) public: int exitIndex = 0; Q_INVOKABLE void playLocalSong(const QUrl &, const QString &) {} signals: void exitIndexChanged(); };
class FakeMainContent final : public QObject { Q_OBJECT public: Q_INVOKABLE bool contentIndexed(int page) { pages << page; return true; } QList<int> pages; };

class PageContext final {
public:
    explicit PageContext(QQmlEngine &engine) {
        const QVariantMap themes{{QStringLiteral("containColor"), QStringLiteral("#fff")}, {QStringLiteral("fontColor"), QStringLiteral("#222")}, {QStringLiteral("fullColor"), QStringLiteral("#fff")}, {QStringLiteral("hoverColor"), QStringLiteral("#10000000")}, {QStringLiteral("primaryBlurColor"), QStringLiteral("#eee")}, {QStringLiteral("primaryColor"), QStringLiteral("#fff")}, {QStringLiteral("secondaryColor"), QStringLiteral("#eee")}, {QStringLiteral("shadowColor"), QStringLiteral("#20000000")}, {QStringLiteral("sideColor"), QStringLiteral("#ddd")}, {QStringLiteral("textColor"), QStringLiteral("#555")}, {QStringLiteral("themeColor"), QStringLiteral("#3481fa")}, {QStringLiteral("borderColor"), QStringLiteral("#ddd")}, {QStringLiteral("blurSecondaryColor"), QStringLiteral("#eee")}};
        const QVariantMap settings{{QStringLiteral("labelRadius"), 12}, {QStringLiteral("pageTitle"), 20}, {QStringLiteral("text"), 14}, {QStringLiteral("textmain"), 14}, {QStringLiteral("texticon"), 16}, {QStringLiteral("cubeRadius"), 8}, {QStringLiteral("textH1"), 18}, {QStringLiteral("textH2"), 16}, {QStringLiteral("textTip"), 12}, {QStringLiteral("blurSize"), 48}, {QStringLiteral("noControlRadius"), false}};
        QVariantMap completeThemes = themes;
        completeThemes.insert(QStringLiteral("sideBlurColor"), QStringLiteral("#88eaeaea"));
        auto *context = engine.rootContext(); context->setContextProperty(QStringLiteral("MusicApi"), &musicApi); context->setContextProperty(QStringLiteral("window"), &window); context->setContextProperty(QStringLiteral("mainContent"), &mainContent); context->setContextProperty(QStringLiteral("Style"), QVariantMap{{QStringLiteral("themes"), completeThemes}, {QStringLiteral("settings"), settings}}); context->setContextProperty(QStringLiteral("Options"), QVariantMap{{QStringLiteral("settings"), QVariantMap{{QStringLiteral("soundQuality"), 0}}}}); context->setContextProperty(QStringLiteral("iconFont"), QVariantMap{{QStringLiteral("name"), QString{}}}); context->setContextProperty(QStringLiteral("favoritesSong"), &legacy); context->setContextProperty(QStringLiteral("favoritesList"), &legacy); context->setContextProperty(QStringLiteral("favoritesArtist"), &legacy); context->setContextProperty(QStringLiteral("playListModel"), &legacy); context->setContextProperty(QStringLiteral("mainWarn"), &legacy); context->setContextProperty(QStringLiteral("mainLayout"), QVariantMap{{QStringLiteral("state"), QString{}}}); context->setContextProperty(QStringLiteral("mainSearchInput"), QVariantMap{{QStringLiteral("text"), QStringLiteral("needle")}});
    }
    FakeMusicApi musicApi; FakeWindow window; FakeMainContent mainContent; FakeListModel legacy;
};

class QmlDiagnosticCapture final {
public:
    QmlDiagnosticCapture()
    {
        Q_ASSERT(!active);
        active = this;
        previous = qInstallMessageHandler(messageHandler);
    }
    ~QmlDiagnosticCapture()
    {
        qInstallMessageHandler(previous);
        active = nullptr;
    }
    QString runtimeErrors() const
    {
        QStringList errors;
        for (const QString &message : messages) {
            if (message.contains(QStringLiteral("ReferenceError"))
                || message.contains(QStringLiteral("TypeError"))
                || message.contains(QStringLiteral("is not defined")))
                errors << message;
        }
        return errors.join(QLatin1Char('\n'));
    }
private:
    static void messageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
    { if (active) active->messages << message; }
    inline static QmlDiagnosticCapture *active = nullptr;
    QtMessageHandler previous = nullptr;
    QStringList messages;
};

static std::unique_ptr<QObject> load(QQmlEngine &engine, const QString &page, FakeAdapter *adapter, QString *error)
{ QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/QueMusic/") + page)); if (component.status() != QQmlComponent::Ready) { *error = component.errorString(); return {}; } auto result = std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("musicAdapter"), QVariant::fromValue(adapter)}, {QStringLiteral("width"), 900}, {QStringLiteral("height"), 600}})); if (!result) *error = component.errorString(); return result; }

class OriginalUiActionsQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterType<FakeDownloadedMusicModel>("QueMusic", 1, 0, "DownloadedMusicModel");
    }

    void favoritesUseAdapterModelsAndActions()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page = load(engine, QStringLiteral("pages/FavouritePage.qml"), &adapter, &error);
        QVERIFY2(page, qPrintable(error));
        const auto views = page->findChildren<QObject *>(); QObject *songView = nullptr;
        for (QObject *candidate : views) if (candidate->property("model").value<QObject *>() == adapter.favoriteSongs()) { songView = candidate; break; }
        QVERIFY(songView); QVERIFY(QMetaObject::invokeMethod(songView, "clicked", Q_ARG(int, 0)));
        QCOMPARE(adapter.played.size(), 1); QCOMPARE(context.musicApi.legacyPlays, 0);
        QVERIFY(QMetaObject::invokeMethod(songView, "toolClicked", Q_ARG(int, 0), Q_ARG(int, 1)));
        QCOMPARE(adapter.favorites.size(), 1); QCOMPARE(context.legacy.legacyFavoriteCalls, 0);
        QObject *listView = nullptr;
        for (QObject *candidate : views) if (candidate->property("model").value<QObject *>() == adapter.favoriteLists()) { listView = candidate; break; }
        QVERIFY(listView); QVERIFY(QMetaObject::invokeMethod(listView, "clicked", Q_ARG(int, 0)));
        QCOMPARE(adapter.browsed.size(), 1);
        QObject *detailWindow = page->findChild<QObject *>(QStringLiteral("favoriteAdapterDetailWindow"));
        QVERIFY(detailWindow);
        QTRY_VERIFY2(detailWindow->property("visible").toBool(), qPrintable(diagnostics.runtimeErrors()));
        QObject *detailList = page->findChild<QObject *>(QStringLiteral("favoriteAdapterDetailList"));
        QTRY_VERIFY2(detailList, qPrintable(diagnostics.runtimeErrors()));
        QCOMPARE(detailList->property("model").value<QObject *>(), adapter.categoryItems());
        QCOMPARE(context.musicApi.legacyPlaylistRequests, 0);
        QCOMPARE(context.musicApi.legacyPlays, 0);
        QVERIFY(QMetaObject::invokeMethod(songView, "ended"));
        QCOMPARE(adapter.more.constLast(), qMakePair(2, QStringLiteral("favorite-songs")));
        const qsizetype requestsAfterFirstPage = adapter.more.size();
        QVERIFY(songView->setProperty("loadingMore", true));
        QVERIFY(QMetaObject::invokeMethod(songView, "ended"));
        QCOMPARE(adapter.more.size(), requestsAfterFirstPage);
        QVERIFY(songView->setProperty("loadingMore", false));
        QVERIFY(songView->setProperty("hasMore", false));
        QVERIFY(QMetaObject::invokeMethod(songView, "ended"));
        QCOMPARE(adapter.more.size(), requestsAfterFirstPage);
        QVERIFY(songView->setProperty("sectionError", QVariantMap{{QStringLiteral("adapter/home"),
            QVariantMap{{QStringLiteral("messageKey"), QStringLiteral("source.network")}}}}));
        QObject *retryArea = songView->findChild<QObject *>(QStringLiteral("sectionRetryArea"));
        QVERIFY(retryArea);
        QVERIFY(retryArea->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(songView, "retrySection"));
        QCOMPARE(adapter.retries.constLast(), qMakePair(2, QStringLiteral("favorite-songs")));
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }

    void mixedCapabilityRowsBindEachToolButtonIndependently()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page = load(engine, QStringLiteral("pages/FavouritePage.qml"), &adapter, &error);
        QVERIFY2(page, qPrintable(error));
        QObject *songView = nullptr;
        for (QObject *candidate : page->findChildren<QObject *>()) {
            if (candidate->property("model").value<QObject *>() == adapter.favoriteSongs()) {
                songView = candidate;
                break;
            }
        }
        QVERIFY(songView);
        QVariant supportedTool0;
        QVariant unsupportedTool0;
        QVariant supportedTool1;
        QVariant unsupportedTool1;
        QVERIFY(QMetaObject::invokeMethod(songView, "rowToolText",
                                          Q_RETURN_ARG(QVariant, supportedTool0),
                                          Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(songView, "rowToolText",
                                          Q_RETURN_ARG(QVariant, unsupportedTool0),
                                          Q_ARG(QVariant, 1), Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(songView, "rowToolText",
                                          Q_RETURN_ARG(QVariant, supportedTool1),
                                          Q_ARG(QVariant, 0), Q_ARG(QVariant, 1)));
        QVERIFY(QMetaObject::invokeMethod(songView, "rowToolText",
                                          Q_RETURN_ARG(QVariant, unsupportedTool1),
                                          Q_ARG(QVariant, 1), Q_ARG(QVariant, 1)));
        QCOMPARE(supportedTool0.toString(), QStringLiteral("\uf095"));
        QCOMPARE(supportedTool1.toString(), QStringLiteral("\uf0c8"));
        QVERIFY(unsupportedTool0.toString().isEmpty());
        QVERIFY(unsupportedTool1.toString().isEmpty());
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }

    void searchUsesAdapterScopeAndNeverFallsBackToLegacy()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page = load(engine, QStringLiteral("pages/SearchPage.qml"), &adapter, &error);
        QVERIFY2(page, qPrintable(error));
        QVERIFY(adapter.activated.contains(3));
        QVERIFY(adapter.searches.contains(qMakePair(QStringLiteral("needle"), 0)));
        QObject *songs = page->findChild<QObject *>(QStringLiteral("searchSongsList"));
        QObject *lists = page->findChild<QObject *>(QStringLiteral("searchListsList"));
        QObject *albums = page->findChild<QObject *>(QStringLiteral("searchAlbumsList"));
        QObject *lyrics = page->findChild<QObject *>(QStringLiteral("searchLyricsList"));
        QObject *tabs = page->findChild<QObject *>(QStringLiteral("searchTabs"));
        QVERIFY(songs); QVERIFY(lists); QVERIFY(albums); QVERIFY(lyrics); QVERIFY(tabs);
        QCOMPARE(songs->property("model").value<QObject *>(), adapter.searchSongs());
        QCOMPARE(lists->property("model").value<QObject *>(), adapter.searchLists());
        QCOMPARE(albums->property("model").value<QObject *>(), adapter.searchAlbums());
        QCOMPARE(lyrics->property("model").value<QObject *>(), adapter.searchLyrics());
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabChange", Q_ARG(int, 2)));
        QVERIFY(adapter.searches.contains(qMakePair(QStringLiteral("needle"), 2)));
        QCOMPARE(context.musicApi.nowIndex, 2);
        QObject *scope = page->findChild<QObject *>(QStringLiteral("searchSourceScope"));
        QVERIFY(scope);
        QVERIFY(scope->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(scope, "transformed", Q_ARG(int, 1)));
        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("adapter/home"));
        QVERIFY(QMetaObject::invokeMethod(albums, "clicked", Q_ARG(int, 0)));
        QCOMPARE(adapter.browsed.size(), 1);
        QObject *detailWindow = page->findChild<QObject *>(QStringLiteral("searchAdapterDetailWindow"));
        QVERIFY(detailWindow);
        QTRY_VERIFY2(detailWindow->property("visible").toBool(), qPrintable(diagnostics.runtimeErrors()));
        QObject *detailList = page->findChild<QObject *>(QStringLiteral("searchAdapterDetailList"));
        QTRY_VERIFY2(detailList, qPrintable(diagnostics.runtimeErrors()));
        QCOMPARE(detailList->property("model").value<QObject *>(), adapter.categoryItems());
        QCOMPARE(context.musicApi.legacyPlaylistRequests, 0);
        QVERIFY(QMetaObject::invokeMethod(albums, "ended"));
        QCOMPARE(adapter.more.constLast(), qMakePair(3, QStringLiteral("search-albums")));
        QVERIFY(QMetaObject::invokeMethod(page.get(), "retryCurrentSection"));
        QCOMPARE(adapter.retries.constLast(), qMakePair(3, QStringLiteral("search-albums")));
        QCOMPARE(context.musicApi.legacySearches, 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }

    void downloadPageLoadsWithoutCrossPageReferenceErrors()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page = load(engine, QStringLiteral("pages/DownloadPage.qml"), &adapter, &error);
        QVERIFY2(page, qPrintable(error));
        QCoreApplication::processEvents();
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }
};

QTEST_MAIN(OriginalUiActionsQmlTest)
#include "tst_OriginalUiActionsQml.moc"
