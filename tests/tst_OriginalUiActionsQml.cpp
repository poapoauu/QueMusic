#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QQuickItem>
#include <QTest>
#include <QTimer>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

class FakeListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString sectionId MEMBER sectionId NOTIFY presentationStateChanged)
    Q_PROPERTY(bool hasMore MEMBER hasMore NOTIFY presentationStateChanged)
    Q_PROPERTY(bool loadingMore MEMBER loadingMore NOTIFY presentationStateChanged)
    Q_PROPERTY(QVariantMap error MEMBER error NOTIFY presentationStateChanged)
    Q_PROPERTY(QStringList paginationSectionIds MEMBER paginationSectionIds NOTIFY presentationStateChanged)
    Q_PROPERTY(QStringList retrySectionIds MEMBER retrySectionIds NOTIFY presentationStateChanged)
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
    QStringList paginationSectionIds;
    QStringList retrySectionIds;
signals:
    void countChanged();
    void presentationStateChanged();
private:
    QVariantList m_rows; QHash<int, QByteArray> m_roles;
};

class FakeAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *favoriteSongs READ favoriteSongs CONSTANT)
    Q_PROPERTY(QObject *favoriteLists READ favoriteLists CONSTANT)
    Q_PROPERTY(QObject *favoriteArtists READ favoriteArtists CONSTANT)
    Q_PROPERTY(QString favoriteArtistsState MEMBER favoriteArtistsState NOTIFY favoriteStatusChanged)
    Q_PROPERTY(bool categoryCanNavigateBack READ categoryCanNavigateBack NOTIFY categoryNavigationChanged)
    Q_PROPERTY(QString categoryTitle READ categoryTitle NOTIFY categoryNavigationChanged)
    Q_PROPERTY(QObject *categoryItems READ categoryItems CONSTANT)
    Q_PROPERTY(QObject *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(QObject *searchLists READ searchLists CONSTANT)
    Q_PROPERTY(QObject *searchAlbums READ searchAlbums CONSTANT)
    Q_PROPERTY(QObject *searchLyrics READ searchLyrics CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QVariantList downloadTasks MEMBER downloadTasks NOTIFY downloadTasksChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    FakeAdapter()
        : songs(QVariantList{row(QStringLiteral("Song"), QStringLiteral("favorite-songs")),
                             row(QStringLiteral("Unavailable"), QStringLiteral("favorite-songs"),
                                 true, false, false, false, false)}),
          lists(QVariantList{row(QStringLiteral("List"), QStringLiteral("favorite-lists"),
                                 true, false, true, true, true)}),
          artists(QVariantList{row(QStringLiteral("<b>Followed artist</b>"), QStringLiteral("favorite-artists"),
                                    false, false, false, true, true)}),
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
        artists.sectionId = QStringLiteral("favorite-artists");
        categoryRows.sectionId = QStringLiteral("detail-tracks");
        searchRows.sectionId = QStringLiteral("search-songs");
        searchListRows.sectionId = QStringLiteral("search-lists");
        searchAlbumRows.sectionId = QStringLiteral("search-albums");
        searchLyricRows.sectionId = QStringLiteral("search-lyrics");
        songs.sectionId = QStringLiteral("favorite-songs");
        for (auto *model : {&songs, &lists, &artists, &categoryRows, &searchRows, &searchListRows, &searchAlbumRows, &searchLyricRows})
            model->paginationSectionIds = {model->sectionId};
    }
    QObject *favoriteSongs() { return &songs; }
    QObject *favoriteLists() { return &lists; }
    QObject *favoriteArtists() { return &artists; }
    bool categoryCanNavigateBack() const { return !navigation.isEmpty(); }
    QString categoryTitle() const { return navigation.isEmpty() ? QString{} : navigation.last().value("title").toString(); }
    QObject *categoryItems() { return &categoryRows; }
    QObject *searchSongs() { return &searchRows; }
    QObject *searchLists() { return &searchListRows; }
    QObject *searchAlbums() { return &searchAlbumRows; }
    QObject *searchLyrics() { return &searchLyricRows; }
    QVariantList sourceOptions() const
    {
        if (!customSourceOptions.isEmpty()) return customSourceOptions;
        return {QVariantMap{{QStringLiteral("sourceInstanceId"), QString{}},
                            {QStringLiteral("displayName"), QStringLiteral("All")},
                            {QStringLiteral("available"), true}},
                QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("adapter/home")},
                            {QStringLiteral("displayName"), QStringLiteral("Home")},
                            {QStringLiteral("available"), true}}};
    }
    void setSourceOptions(QVariantList options)
    {
        customSourceOptions = std::move(options);
        emit sourceOptionsChanged();
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
    Q_INVOKABLE void loadMore(int page, const QString &section) {
        more << qMakePair(page, section);
        if (pagingMutationModel) {
            pagingMutationModel->paginationSectionIds.clear();
            emit pagingMutationModel->presentationStateChanged();
        }
    }
    Q_INVOKABLE void retry(int page, const QString &section) { retries << qMakePair(page, section); }
    FakeListModel *pagingMutationModel = nullptr;
    Q_INVOKABLE bool browse(const QVariantMap &value) {
        browsed << value; navigation << value; emit categoryNavigationChanged(); emit browseRequested(); return true;
    }
    Q_INVOKABLE void closeCategoryBrowse() { ++closedBrowses; navigation.clear(); emit categoryNavigationChanged(); }
    Q_INVOKABLE bool categoryBack() {
        if (navigation.isEmpty()) return false;
        navigation.removeLast(); emit categoryNavigationChanged(); return true;
    }
    Q_INVOKABLE QUuid play(const QVariantMap &value) { played << value; return QUuid::createUuid(); }
    Q_INVOKABLE QUuid enqueue(const QVariantMap &value) { enqueued << value; return QUuid::createUuid(); }
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &value, bool favorite) { favorites << qMakePair(value, favorite); return QUuid::createUuid(); }
    Q_INVOKABLE bool dismissDownloadTask(const QString &id) {
        for (qsizetype i = 0; i < downloadTasks.size(); ++i) {
            const auto row = downloadTasks.at(i).toMap();
            if (row.value("taskId").toString() != id || row.value("state").toString() == "pending") continue;
            dismissedDownloads << id;
            downloadTasks.removeAt(i);
            emit downloadTasksChanged();
            return true;
        }
        return false;
    }
    Q_INVOKABLE bool cancelDownloadTask(const QString &id) {
        if (cancelledDownloads.contains(id)) return false;
        for (const auto &value : downloadTasks) {
            const auto row = value.toMap();
            if (row.value("taskId").toString() != id || row.value("state").toString() != "pending") continue;
            cancelledDownloads << id;
            QTimer::singleShot(0, this, [this, id] {
                for (auto &value : downloadTasks) {
                    auto row = value.toMap();
                    if (row.value("taskId").toString() == id) { row["state"] = "cancelled"; value = row; }
                }
                emit downloadTasksChanged();
            });
            return true;
        }
        return false;
    }
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
    FakeListModel songs, lists, artists, categoryRows, searchRows, searchListRows,
        searchAlbumRows, searchLyricRows;
    QList<int> activated;
    QList<QPair<QString, int>> searches;
    QList<QPair<int, QString>> more, retries;
    QList<QVariantMap> played, enqueued, browsed;
    QList<QVariantMap> navigation;
    int closedBrowses = 0;
    QString favoriteArtistsState = "ready";
    QList<QPair<QVariantMap, bool>> favorites;
    QString selectedSource;
    QVariantList customSourceOptions;
    QVariantList downloadTasks;
    QStringList dismissedDownloads;
    QStringList cancelledDownloads;
signals:
    void favoriteStatusChanged();
    void categoryNavigationChanged();
    void browseRequested();
    void selectedSourceInstanceIdChanged();
    void sourceOptionsChanged();
    void downloadTasksChanged();
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

static QQuickItem *visualChild(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *found = visualChild(child, name)) return found;
    return nullptr;
}

class OriginalUiActionsQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterType<FakeDownloadedMusicModel>("QueMusic", 1, 0, "DownloadedMusicModel");
    }

    void favoritesWithoutAdapterStayEmptyInsteadOfUsingLegacyRows()
    {
        QQmlEngine engine; PageContext context(engine); QmlDiagnosticCapture diagnostics; QString error;
        context.legacy.setRows({FakeAdapter::row(QStringLiteral("Legacy item"),
                                                  QStringLiteral("legacy"))});
        auto page = load(engine, QStringLiteral("pages/FavouritePage.qml"), nullptr, &error);
        QVERIFY2(page, qPrintable(error));
        QObject *songs = page->findChild<QObject *>(QStringLiteral("favoriteSongsList"));
        QObject *lists = page->findChild<QObject *>(QStringLiteral("favoritePlaylistsList"));
        QVERIFY(songs); QVERIFY(lists);
        QCOMPARE(songs->property("count").toInt(), 0);
        QCOMPARE(lists->property("count").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(songs, "clicked", Q_ARG(int, 0)));
        QVERIFY(QMetaObject::invokeMethod(lists, "clicked", Q_ARG(int, 0)));
        QCOMPARE(context.musicApi.legacyPlays, 0);
        QCOMPARE(context.musicApi.legacyPlaylistRequests, 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
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
        adapter.songs.loadingMore = true;
        adapter.songs.paginationSectionIds.clear();
        emit adapter.songs.presentationStateChanged();
        QVERIFY(QMetaObject::invokeMethod(songView, "ended"));
        QCOMPARE(adapter.more.size(), requestsAfterFirstPage);
        adapter.songs.loadingMore = false;
        adapter.songs.hasMore = false;
        emit adapter.songs.presentationStateChanged();
        QVERIFY(QMetaObject::invokeMethod(songView, "ended"));
        QCOMPARE(adapter.more.size(), requestsAfterFirstPage);
        adapter.songs.error = {{"favorite-songs", QVariantMap{{"failed", true}}}};
        adapter.songs.retrySectionIds = {"favorite-songs"};
        emit adapter.songs.presentationStateChanged();
        QObject *retryArea = songView->findChild<QObject *>(QStringLiteral("sectionRetryArea"));
        QVERIFY(retryArea);
        QVERIFY(retryArea->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(songView, "retrySection"));
        QCOMPARE(adapter.retries.constLast(), qMakePair(2, QStringLiteral("favorite-songs")));
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }

    void followedArtistsUseSafeBrowseUnfavoriteAndNestedNavigation()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page=load(engine,"pages/FavouritePage.qml",&adapter,&error); QVERIFY2(page,qPrintable(error));
        auto *view=page->findChild<QObject *>("favoriteArtistsList"); QVERIFY(view);
        QCOMPARE(view->property("model").value<QObject *>(),adapter.favoriteArtists());
        QVERIFY(view->property("sourcePaging").toBool()); QCOMPARE(view->property("sourcePageKind").toInt(),2);
        QVERIFY(view->property("isList").toBool()); QVERIFY(!view->property("showListCount").toBool());
        auto *tabs=page->findChild<QObject *>("favoriteTabs"); QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(tabs,"tabChange",Q_ARG(int,2)));
        QVERIFY(QMetaObject::invokeMethod(view,"forceLayout"));
        auto *artistItem=qobject_cast<QQuickItem *>(view); QVERIFY(artistItem);
        QTRY_VERIFY(visualChild(artistItem,"sourceRowTitle"));
        auto *rowTitle=visualChild(artistItem,"sourceRowTitle");
        QCOMPARE(rowTitle->property("text").toString(),QString("<b>Followed artist</b>"));
        QCOMPARE(rowTitle->property("textFormat").toInt(),0);
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        QCOMPARE(adapter.browsed.size(),1); QCOMPARE(adapter.played.size(),0);
        auto *detail=page->findChild<QObject *>("favoriteAdapterDetailWindow"); QVERIFY(detail);
        QVERIFY(detail->property("visible").toBool());
        auto *title=page->findChild<QObject *>("favoriteDetailTitle"); QVERIFY(title);
        QCOMPARE(title->property("text").toString(),QString("<b>Followed artist</b>"));
        QCOMPARE(title->property("textFormat").toInt(),0);
        QVERIFY(QMetaObject::invokeMethod(view,"toolClicked",Q_ARG(int,0),Q_ARG(int,0)));
        QCOMPARE(adapter.enqueued.size(),0);
        QVERIFY(QMetaObject::invokeMethod(view,"toolClicked",Q_ARG(int,0),Q_ARG(int,1)));
        QCOMPARE(adapter.favorites.size(),1); QVERIFY(!adapter.favorites.last().second);
        adapter.categoryRows.setRows({FakeAdapter::row("<i>Album</i>","albums",false,false,false,false,true)});
        auto *children=page->findChild<QObject *>("favoriteAdapterDetailList"); QVERIFY(children);
        QVERIFY(QMetaObject::invokeMethod(children,"clicked",Q_ARG(int,0)));
        QCOMPARE(adapter.browsed.size(),2); QCOMPARE(adapter.played.size(),0);
        QCOMPARE(title->property("text").toString(),QString("<i>Album</i>"));
        auto *back=page->findChild<QObject *>("favoriteDetailBack"); QVERIFY(back);
        QVERIFY(QMetaObject::invokeMethod(back,"clicked")); QVERIFY(detail->property("visible").toBool());
        QCOMPARE(title->property("text").toString(),QString("<b>Followed artist</b>"));
        QVERIFY(QMetaObject::invokeMethod(back,"clicked")); QVERIFY(!detail->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(children,"clicked",Q_ARG(int,0))); QCOMPARE(adapter.browsed.size(),2);
        const auto moreBeforeClose=adapter.more.size();
        QVERIFY(QMetaObject::invokeMethod(children,"ended")); QCOMPARE(adapter.more.size(),moreBeforeClose);
        adapter.artists.setRows({FakeAdapter::row("Denied","artists",false,false,false,false,false)});
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,-1)));
        QVERIFY(QMetaObject::invokeMethod(view,"toolClicked",Q_ARG(int,0),Q_ARG(int,1)));
        QCOMPARE(adapter.browsed.size(),2); QCOMPARE(adapter.favorites.size(),1);
        QCOMPARE(context.musicApi.legacyPlaylistRequests,0); QCOMPARE(context.musicApi.legacyPlays,0);
        QCOMPARE(context.legacy.legacyFavoriteCalls,0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(),qPrintable(diagnostics.runtimeErrors()));
    }

    void followedArtistStatusPagingAndContextChangesDoNotUseLegacy()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter,replacement; QmlDiagnosticCapture diagnostics; QString error;
        auto page=load(engine,"pages/FavouritePage.qml",&adapter,&error); QVERIFY2(page,qPrintable(error));
        auto *view=page->findChild<QObject *>("favoriteArtistsList"); QVERIFY(view);
        auto *status=page->findChild<QObject *>("favoriteArtistsStatus"); QVERIFY(status);
        auto *tabs=page->findChild<QObject *>("favoriteTabs"); QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(tabs,"tabChange",Q_ARG(int,2)));
        adapter.artists.setRows({});
        for (const auto &entry:QList<QPair<QString,QString>>{{"loading","正在加载关注歌手…"},{"failed","关注歌手加载失败，请重试"},{"empty","当前范围暂无关注歌手"},{"unavailable","当前音源不可用"}}) {
            adapter.favoriteArtistsState=entry.first; emit adapter.favoriteStatusChanged();
            QCOMPARE(status->property("text").toString(),entry.second); QVERIFY(status->property("visible").toBool());
        }
        adapter.artists.retrySectionIds={"failed-artists"}; emit adapter.artists.presentationStateChanged();
        QVERIFY(QMetaObject::invokeMethod(page.get(),"retryCurrentSection"));
        QCOMPARE(adapter.retries,(QList<QPair<int,QString>>{{2,"failed-artists"}}));
        adapter.artists.paginationSectionIds={"artists-a","artists-b"}; emit adapter.artists.presentationStateChanged();
        adapter.more.clear(); // Empty ListView also legitimately requests its first continuation atYEnd.
        QVERIFY(QMetaObject::invokeMethod(view,"ended"));
        QCOMPARE(adapter.more,(QList<QPair<int,QString>>{{2,"artists-a"},{2,"artists-b"}}));
        adapter.artists.paginationSectionIds.clear(); adapter.artists.retrySectionIds.clear(); emit adapter.artists.presentationStateChanged();
        QVERIFY(QMetaObject::invokeMethod(view,"ended")); QVERIFY(QMetaObject::invokeMethod(view,"retrySection"));
        QCOMPARE(adapter.more.size(),2); QCOMPARE(adapter.retries.size(),1);
        adapter.artists.setRows({FakeAdapter::row("Artist","artists",false,false,false,true,true)});
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        auto *detail=page->findChild<QObject *>("favoriteAdapterDetailWindow"); QVERIFY(detail);
        QVERIFY(detail->property("visible").toBool());
        page->setProperty("chooseIndex",QVariantList{0}); page->setProperty("setMode",1);
        adapter.setSelectedSourceInstanceId("other/account");
        QVERIFY(!detail->property("visible").toBool()); QCOMPARE(page->property("setMode").toInt(),0);
        QCOMPARE(adapter.closedBrowses,0);
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        replacement.browse(FakeAdapter::row("Replacement navigation","replacement"));
        QVERIFY(page->setProperty("musicAdapter",QVariant::fromValue<QObject *>(&replacement)));
        QVERIFY(!detail->property("visible").toBool()); QCOMPARE(replacement.closedBrowses,0);
        QCOMPARE(replacement.navigation.size(),1);
        QVERIFY(page->setProperty("musicAdapter",QVariant::fromValue<QObject *>(nullptr)));
        QCOMPARE(view->property("count").toInt(),0);
        QCOMPARE(status->property("text").toString(),QString("当前音源不可用"));
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        QVERIFY(QMetaObject::invokeMethod(view,"toolClicked",Q_ARG(int,0),Q_ARG(int,1)));
        QCOMPARE(replacement.closedBrowses,0); QCOMPARE(context.musicApi.legacyPlays,0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(),qPrintable(diagnostics.runtimeErrors()));
    }

    void favoriteBrowseStopsWhenItsCallbackReplacesAdapter()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter,replacement; QString error;
        auto page=load(engine,"pages/FavouritePage.qml",&adapter,&error); QVERIFY2(page,qPrintable(error));
        connect(&adapter,&FakeAdapter::browseRequested,page.get(),[&] {
            page->setProperty("musicAdapter",QVariant::fromValue<QObject *>(&replacement));
        });
        auto *view=page->findChild<QObject *>("favoriteArtistsList"); QVERIFY(view);
        QVERIFY(QMetaObject::invokeMethod(view,"clicked",Q_ARG(int,0)));
        auto *detail=page->findChild<QObject *>("favoriteAdapterDetailWindow"); QVERIFY(detail);
        QVERIFY(!detail->property("visible").toBool()); QCOMPARE(replacement.browsed.size(),0);
        QCOMPARE(replacement.closedBrowses,0); QCOMPARE(context.musicApi.legacyPlaylistRequests,0);
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
        QVERIFY(adapter.searches.isEmpty());
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
        QCOMPARE(context.musicApi.nowIndex, 0);
        QObject *scope = page->findChild<QObject *>(QStringLiteral("searchSourceScope"));
        QVERIFY(scope);
        QVERIFY(scope->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(scope, "transformed", Q_ARG(int, 1)));
        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("adapter/home"));
        adapter.setSourceOptions({QVariantMap{{"sourceInstanceId", QString{}},
                                              {"displayName", "All"}, {"available", true}},
                                  QVariantMap{{"sourceInstanceId", "adapter/home"},
                                              {"displayName", "Home"}, {"available", true}},
                                  QVariantMap{{"sourceInstanceId", "third/instance"},
                                              {"displayName", "Third-party"}, {"available", true}},
                                  QVariantMap{{"sourceInstanceId", "disabled/instance"},
                                              {"displayName", "Disabled"}, {"available", false}}});
        QVERIFY(QMetaObject::invokeMethod(scope, "transformed", Q_ARG(int, 2)));
        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("third/instance"));
        QCOMPARE(scope->property("choice").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(scope, "transformed", Q_ARG(int, 3)));
        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("third/instance"));
        QCOMPARE(scope->property("choice").toInt(), 2);
        QCOMPARE(context.musicApi.songSource, 0);
        QVERIFY(QMetaObject::invokeMethod(songs, "clicked", Q_ARG(int, 0)));
        QCOMPARE(adapter.played.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(songs, "clicked", Q_ARG(int, 1)));
        QCOMPARE(adapter.played.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(songs, "toolClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
        QCOMPARE(adapter.enqueued.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(songs, "toolClicked", Q_ARG(int, 1), Q_ARG(int, 1)));
        QCOMPARE(adapter.favorites.size(), 0);
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
        adapter.searchAlbumRows.error = {{"search-albums", QVariantMap{{"failed", true}}}};
        adapter.searchAlbumRows.retrySectionIds = {"search-albums"};
        emit adapter.searchAlbumRows.presentationStateChanged();
        QVERIFY(QMetaObject::invokeMethod(page.get(), "retryCurrentSection"));
        QCOMPARE(adapter.retries.constLast(), qMakePair(3, QStringLiteral("search-albums")));
        QCOMPARE(context.musicApi.legacySearches, 0);
        QCOMPARE(context.musicApi.legacyPlays, 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }

    void searchAndFavoriteListsUseAggregateStateEvenWithStaleOrMissingRows()
    {
        for (const bool search : {true, false}) {
            QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
            auto page = load(engine, search ? "pages/SearchPage.qml" : "pages/FavouritePage.qml", &adapter, &error);
            QVERIFY2(page, qPrintable(error));
            QList<FakeListModel *> models = search
                ? QList<FakeListModel *>{&adapter.searchRows, &adapter.searchListRows, &adapter.searchAlbumRows, &adapter.searchLyricRows}
                : QList<FakeListModel *>{&adapter.songs, &adapter.lists, &adapter.artists};
            const QStringList names = search
                ? QStringList{"searchSongsList", "searchListsList", "searchAlbumsList", "searchLyricsList"}
                : QStringList{"favoriteSongsList", "favoritePlaylistsList", "favoriteArtistsList"};
            const int pageKind = search ? 3 : 2;
            for (int i = 0; i < models.size(); ++i) {
                auto *model = models[i];
                auto *view = page->findChild<QObject *>(names[i]);
                QVERIFY2(view, qPrintable(names[i]));
                auto stale = FakeAdapter::row("Stale last row", "wrong-last-section");
                stale["hasMore"] = false;
                model->setRows({stale});
                model->sectionId = "exhausted-first";
                model->hasMore = true; model->loadingMore = true;
                model->paginationSectionIds = {"ready-a", "ready-b"};
                emit model->presentationStateChanged();
                QVERIFY(view->property("hasMore").toBool());
                QVERIFY(!view->property("loadingMore").toBool());
                QVERIFY(!view->property("useLegacyLoadingState").toBool());
                const int before = adapter.more.size();
                adapter.pagingMutationModel = model;
                QVERIFY(QMetaObject::invokeMethod(view, "ended"));
                adapter.pagingMutationModel = nullptr;
                QCOMPARE(adapter.more.size(), before + 2);
                QCOMPARE(adapter.more.at(before), qMakePair(pageKind, QString("ready-a")));
                QCOMPARE(adapter.more.at(before + 1), qMakePair(pageKind, QString("ready-b")));
                model->paginationSectionIds.clear(); emit model->presentationStateChanged();
                QVERIFY(QMetaObject::invokeMethod(view, "ended"));
                QCOMPARE(adapter.more.size(), before + 2);
                model->loadingMore = false;
                model->error = {{"failed-a", QVariantMap{{"detail", "private diagnostic"}}}};
                model->retrySectionIds = {"failed-a", "failed-b"};
                model->setRows({}); emit model->presentationStateChanged();
                QCOMPARE(view->property("count").toInt(), 0);
                QCOMPARE(view->property("sectionError").toMap(), QVariantMap({{"failed", true}}));
                const int retries = adapter.retries.size();
                QVERIFY(QMetaObject::invokeMethod(view, "retrySection"));
                QCOMPARE(adapter.retries.size(), retries + 2);
                QCOMPARE(adapter.retries.at(retries), qMakePair(pageKind, QString("failed-a")));
                QCOMPARE(adapter.retries.at(retries + 1), qMakePair(pageKind, QString("failed-b")));
                model->retrySectionIds.clear(); model->loadingMore = true; emit model->presentationStateChanged();
                QVERIFY(QMetaObject::invokeMethod(view, "retrySection"));
                QCOMPARE(adapter.retries.size(), retries + 2);
            }
            QCOMPARE(context.musicApi.legacyPlays, 0);
            QCOMPARE(context.musicApi.legacySearches, 0);
            const int before = adapter.more.size();
            QVERIFY(page->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
            auto *view = page->findChild<QObject *>(names.first());
            QVERIFY(view);
            QVERIFY(QMetaObject::invokeMethod(view, "ended"));
            QVERIFY(QMetaObject::invokeMethod(view, "clicked", Q_ARG(int, 0)));
            QVERIFY(QMetaObject::invokeMethod(view, "toolClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
            QCOMPARE(adapter.more.size(), before);
            QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
        }
    }

    void searchAndFavoriteDetailsUseCategorySectionScope()
    {
        for (const bool search : {true, false}) {
            QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
            auto page = load(engine, search ? "pages/SearchPage.qml" : "pages/FavouritePage.qml", &adapter, &error);
            QVERIFY2(page, qPrintable(error));
            auto *parentList = page->findChild<QObject *>(search ? "searchListsList" : "favoritePlaylistsList");
            QVERIFY(parentList);
            QVERIFY(QMetaObject::invokeMethod(parentList, "clicked", Q_ARG(int, 0)));
            auto *detail = page->findChild<QObject *>(search ? "searchAdapterDetailList" : "favoriteAdapterDetailList");
            QVERIFY(detail);
            auto &model = adapter.categoryRows;
            model.paginationSectionIds = {"detail-a", "detail-b"};
            emit model.presentationStateChanged();
            const int before = adapter.more.size();
            QVERIFY(QMetaObject::invokeMethod(detail, "ended"));
            QCOMPARE(adapter.more.size(), before + 2);
            QCOMPARE(adapter.more.at(before), qMakePair(1, QString("detail-a")));
            QCOMPARE(adapter.more.at(before + 1), qMakePair(1, QString("detail-b")));
            model.paginationSectionIds.clear(); model.loadingMore = true;
            emit model.presentationStateChanged();
            QVERIFY(QMetaObject::invokeMethod(detail, "ended"));
            QCOMPARE(adapter.more.size(), before + 2);
            model.loadingMore = false; model.hasMore = false;
            model.error = {{"detail-failed", QVariantMap{{"detail", "private diagnostic"}}}};
            model.retrySectionIds = {"detail-failed"};
            model.setRows({}); emit model.presentationStateChanged();
            QVERIFY(QMetaObject::invokeMethod(detail, "retrySection"));
            QCOMPARE(adapter.retries, (QList<QPair<int, QString>>{{1, "detail-failed"}}));
            QCOMPARE(detail->property("sectionError").toMap(), QVariantMap({{"failed", true}}));
            QVERIFY(!detail->property("useLegacyLoadingState").toBool());
            QCOMPARE(context.musicApi.legacyPlaylistRequests, 0);
            QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
        }
    }

    void downloadPageLoadsWithoutCrossPageReferenceErrors()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        auto page = load(engine, QStringLiteral("pages/DownloadPage.qml"), &adapter, &error);
        QVERIFY2(page, qPrintable(error));
        QCoreApplication::processEvents();
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }
    void downloadPageUsesSessionTasksAndDismissesOnlyTerminalRecords()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        const auto task = [](const QString &id, const QString &state) {
            return QVariantMap{{"taskId", id}, {"state", state}, {"fileName", "saved.bin"},
                {"title", "<b>Track</b>"}, {"artist", "Artist"}, {"sourceLabel", "Source"}};
        };
        adapter.downloadTasks = {task("running", "pending"), task("failed", "failed"), task("saved", "completed")};
        auto page = load(engine, "pages/DownloadPage.qml", &adapter, &error); QVERIFY2(page, qPrintable(error));
        auto *active = page->findChild<QObject *>("sourceActiveDownloadTasks");
        auto *completed = page->findChild<QObject *>("sourceCompletedDownloadTasks"); QVERIFY(active); QVERIFY(completed);
        QTRY_COMPARE(active->property("count").toInt(), 2); QTRY_COMPARE(completed->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(active, "forceLayout"));
        auto *pageItem = qobject_cast<QQuickItem *>(page.get()); QVERIFY(pageItem);
        QTRY_VERIFY2(visualChild(pageItem, "dismissSourceDownloadTask_failed"), qPrintable(diagnostics.runtimeErrors()));
        auto *pendingButton = visualChild(pageItem, "dismissSourceDownloadTask_running"); QVERIFY(pendingButton);
        QVERIFY(!pendingButton->property("enabled").toBool()); QVERIFY(!pendingButton->property("visible").toBool());
        // Even programmatic activation of a hidden button must not dismiss a live request.
        QVERIFY(QMetaObject::invokeMethod(pendingButton, "clicked")); QVERIFY(adapter.dismissedDownloads.isEmpty());
        auto *title = visualChild(pageItem, "sourceDownloadTitle_running"); QVERIFY(title);
        QCOMPARE(title->property("text").toString(), QString("saved.bin · <b>Track</b>"));
        QCOMPARE(title->property("textFormat").toInt(), 0); // Text.PlainText, not rich plugin markup.
        auto *failedButton = visualChild(pageItem, "dismissSourceDownloadTask_failed"); QVERIFY(failedButton);
        QVERIFY(failedButton->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(failedButton, "clicked"));
        QCOMPARE(adapter.dismissedDownloads, QStringList{"failed"}); QTRY_COMPARE(active->property("count").toInt(), 1);
        // Task completion moves into the original completed tab without consulting current playback.
        adapter.downloadTasks = {task("running", "completed"), task("saved", "completed")}; emit adapter.downloadTasksChanged();
        QTRY_COMPARE(active->property("count").toInt(), 0); QTRY_COMPARE(completed->property("count").toInt(), 2);
        auto *tabs = page->findChild<QObject *>("downloadTabs"); QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabChange", Q_ARG(int, 1))); QCOMPARE(page->property("downloadTab").toInt(), 1);
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(completed, "forceLayout"));
        QTRY_VERIFY2(visualChild(pageItem, "dismissSourceDownloadTask_saved"), qPrintable(diagnostics.runtimeErrors()));
        auto *savedButton = visualChild(pageItem, "dismissSourceDownloadTask_saved"); QVERIFY(savedButton);
        QVERIFY(QMetaObject::invokeMethod(savedButton, "clicked")); QCOMPARE(adapter.dismissedDownloads, QStringList({"failed", "saved"}));
        QTRY_COMPARE(completed->property("count").toInt(), 1);
        QVERIFY(page->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
        QTRY_COMPARE(active->property("count").toInt(), 0); QTRY_COMPARE(completed->property("count").toInt(), 0);
        QCOMPARE(context.musicApi.legacyPlays, 0); QCOMPARE(context.musicApi.legacyPlaylistRequests, 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }
    void downloadPageCancelsOnlyPendingSourceTasks()
    {
        QQmlEngine engine; PageContext context(engine); FakeAdapter adapter; QmlDiagnosticCapture diagnostics; QString error;
        const auto task = [](const QString &id, const QString &state) {
            return QVariantMap{{"taskId", id}, {"state", state}, {"fileName", "saved.bin"},
                {"title", "Track"}, {"artist", "Artist"}, {"sourceLabel", "Source"}};
        };
        adapter.downloadTasks = {task("running", "pending"), task("failed", "failed"), task("saved", "completed")};
        auto page = load(engine, "pages/DownloadPage.qml", &adapter, &error); QVERIFY2(page, qPrintable(error));
        auto *active = page->findChild<QObject *>("sourceActiveDownloadTasks"); QVERIFY(active);
        auto *completed = page->findChild<QObject *>("sourceCompletedDownloadTasks"); QVERIFY(completed);
        QVERIFY(QMetaObject::invokeMethod(active, "forceLayout"));
        auto *pageItem = qobject_cast<QQuickItem *>(page.get()); QVERIFY(pageItem);
        auto *cancel = visualChild(pageItem, "cancelSourceDownloadTask_running"); QVERIFY(cancel);
        auto *failedCancel = visualChild(pageItem, "cancelSourceDownloadTask_failed"); QVERIFY(failedCancel);
        QVERIFY(cancel->property("enabled").toBool()); QVERIFY(!failedCancel->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(failedCancel, "clicked")); QVERIFY(adapter.cancelledDownloads.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked")); QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QCOMPARE(adapter.cancelledDownloads, QStringList{"running"});
        QTRY_COMPARE(adapter.downloadTasks.first().toMap().value("state").toString(), QString("cancelled"));
        QVERIFY(QMetaObject::invokeMethod(active, "forceLayout"));
        cancel = visualChild(pageItem, "cancelSourceDownloadTask_running"); QVERIFY(cancel);
        QVERIFY(!cancel->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QCOMPARE(adapter.cancelledDownloads, QStringList{"running"});
        auto *dismiss = visualChild(pageItem, "dismissSourceDownloadTask_running"); QVERIFY(dismiss);
        QVERIFY(dismiss->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(dismiss, "clicked"));
        QCOMPARE(adapter.dismissedDownloads, QStringList{"running"}); QTRY_COMPARE(active->property("count").toInt(), 1);
        QCOMPARE(completed->property("count").toInt(), 1);
        QVERIFY(page->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
        QVariant result;
        QVERIFY(QMetaObject::invokeMethod(page.get(), "cancelSourceTask", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, "running")));
        QVERIFY(!result.toBool()); QCOMPARE(adapter.cancelledDownloads, QStringList{"running"});
        QCOMPARE(context.musicApi.legacyPlays, 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(), qPrintable(diagnostics.runtimeErrors()));
    }
};

QTEST_MAIN(OriginalUiActionsQmlTest)
#include "tst_OriginalUiActionsQml.moc"
