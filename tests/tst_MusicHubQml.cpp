#include "MusicPageModel.h"

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QUuid>
#include <QTest>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

class FakeActions final : public QObject {
    Q_OBJECT
public:
    QVariantMap receivedItem;
    QVariantMap receivedPlaylist;
    QVariantMap receivedChange;
    QUrl receivedDestination;
    QString lastMethod;
    bool receivedFavorite = false;
    int receivedRating = -1;
    qint64 receivedBookmark = -1;
    int favoriteCalls = 0;

    Q_INVOKABLE void setFavorite(const QVariantMap &item, bool favorite)
    {
        receivedItem = item;
        receivedFavorite = favorite;
        lastMethod = QStringLiteral("setFavorite");
        ++favoriteCalls;
    }
    Q_INVOKABLE void download(const QVariantMap &item, const QUrl &destination)
    {
        receivedItem = item;
        receivedDestination = destination;
        lastMethod = QStringLiteral("download");
    }
    Q_INVOKABLE void setRating(const QVariantMap &item, int rating)
    {
        receivedItem = item;
        receivedRating = rating;
        lastMethod = QStringLiteral("setRating");
    }
    Q_INVOKABLE void updatePlaylist(const QVariantMap &playlist, const QVariantMap &change)
    {
        receivedPlaylist = playlist;
        receivedChange = change;
        lastMethod = QStringLiteral("updatePlaylist");
    }
    Q_INVOKABLE void setBookmark(const QVariantMap &item, qint64 position)
    {
        receivedItem = item;
        receivedBookmark = position;
        lastMethod = QStringLiteral("setBookmark");
    }
};

class FakeHub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sourceOptions MEMBER sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId MEMBER selectedSourceInstanceId
               NOTIFY selectedSourceInstanceIdChanged)
    Q_PROPERTY(QObject *actions READ actions CONSTANT)
    Q_PROPERTY(QObject *recommendation READ recommendation CONSTANT)
    Q_PROPERTY(QObject *category READ category CONSTANT)
    Q_PROPERTY(QObject *favorites READ favorites CONSTANT)
    Q_PROPERTY(QObject *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(bool canNavigateBack MEMBER canNavigateBack NOTIFY canNavigateBackChanged)
public:
    QVariantList sourceOptions;
    QString selectedSourceInstanceId;
    FakeActions actionRouter;
    QVariantMap browsedItem;
    int browseCalls = 0;
    int loadMoreCalls = 0;
    int retryCalls = 0;
    int cancelledAssets = 0;
    int lastPageKind = -1;
    QString lastSectionId;
    QVariantMap artworkRef;
    QUuid artworkRequestId = QUuid::createUuid();
    MusicPageModel recommendationModel{MusicPageKindV2::Recommendation};
    MusicPageModel categoryModel{MusicPageKindV2::Category};
    MusicPageModel favoritesModel{MusicPageKindV2::Favorites};
    MusicPageModel searchResultsModel{MusicPageKindV2::Search};
    bool canNavigateBack = true;
    int lastActivatedPage = -1;
    QString searchedText;
    int navigateBackCalls = 0;

    QObject *actions() { return &actionRouter; }
    QObject *recommendation() { return &recommendationModel; }
    QObject *category() { return &categoryModel; }
    QObject *favorites() { return &favoritesModel; }
    QObject *searchResults() { return &searchResultsModel; }
    Q_INVOKABLE void activatePage(int pageKind) { lastActivatedPage = pageKind; }
    Q_INVOKABLE void search(const QString &text) { searchedText = text; }
    Q_INVOKABLE bool navigateBack() { ++navigateBackCalls; return true; }
    Q_INVOKABLE void browse(const QVariantMap &item)
    {
        browsedItem = item;
        ++browseCalls;
    }
    Q_INVOKABLE void loadMore(int pageKind, const QString &sectionId)
    {
        ++loadMoreCalls;
        lastPageKind = pageKind;
        lastSectionId = sectionId;
    }
    Q_INVOKABLE void retrySection(int pageKind, const QString &sectionId)
    {
        ++retryCalls;
        lastPageKind = pageKind;
        lastSectionId = sectionId;
    }
    Q_INVOKABLE QUuid loadArtwork(const QVariantMap &media)
    {
        artworkRef = media;
        return artworkRequestId;
    }
    Q_INVOKABLE void cancelAsset(const QUuid &) { ++cancelledAssets; }

signals:
    void sourceOptionsChanged();
    void selectedSourceInstanceIdChanged();
    void canNavigateBackChanged();
    void artworkReady(QUuid requestId, QVariantMap media, QUrl localUrl);
    void assetFailed(QUuid requestId, QVariantMap error);
};

class FakePlayback final : public QObject {
    Q_OBJECT
public:
    QVariantMap playedItem;
    int playCalls = 0;

    Q_INVOKABLE void play(const QVariantMap &item)
    {
        playedItem = item;
        ++playCalls;
    }
};

class FakeContentController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pageIndex MEMBER pageIndex NOTIFY pageIndexChanged)
public:
    int pageIndex = 0;
    int lastIndex = -1;

    Q_INVOKABLE bool contentIndexed(int index)
    {
        lastIndex = index;
        pageIndex = index;
        emit pageIndexChanged();
        return true;
    }

signals:
    void pageIndexChanged();
};

class FakeNavigationWindow final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int exitIndex MEMBER exitIndex NOTIFY exitIndexChanged)
public:
    int exitIndex = 0;

signals:
    void exitIndexChanged();
    void exit();
};

class FakeLegacyListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 0;
    }

    QVariant data(const QModelIndex &, int) const override { return {}; }

    Q_INVOKABLE QVariantMap get(int) const { return {}; }
    Q_INVOKABLE void clear() { emit countChanged(); }
    Q_INVOKABLE void append(const QVariantMap &) { emit countChanged(); }
    Q_INVOKABLE bool isFavorite(const QString &, const QString &) const { return false; }
    Q_INVOKABLE void addFavorite(const QString &, const QString &, const QString &,
                                 const QString &, int, int, const QString &) {}
    Q_INVOKABLE void removeFavorite(const QString &, const QString &) {}

signals:
    void countChanged();
};

class FakeLegacyMusicApi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int songSource MEMBER songSource NOTIFY songSourceChanged)
    Q_PROPERTY(int nowIndex MEMBER nowIndex NOTIFY nowIndexChanged)
    Q_PROPERTY(QString globalid MEMBER globalid NOTIFY globalidChanged)
    Q_PROPERTY(QString globaltagid MEMBER globaltagid NOTIFY globaltagidChanged)
    Q_PROPERTY(int loadState MEMBER loadState NOTIFY loadStateChanged)
    Q_PROPERTY(QObject *allPlaylistMenu READ allPlaylistMenu CONSTANT)
    Q_PROPERTY(QObject *getHotlistMenu READ getHotlistMenu CONSTANT)
    Q_PROPERTY(QObject *hotPlayLists READ hotPlayLists CONSTANT)
    Q_PROPERTY(QObject *musicPlaylists READ musicPlaylists CONSTANT)
    Q_PROPERTY(QObject *newSongs READ newSongs CONSTANT)
    Q_PROPERTY(QObject *personalFm READ personalFm CONSTANT)
    Q_PROPERTY(QObject *personalRadar READ personalRadar CONSTANT)
    Q_PROPERTY(QObject *playlistSong READ playlistSong CONSTANT)
    Q_PROPERTY(QObject *recommendSongs READ recommendSongs CONSTANT)
    Q_PROPERTY(QObject *searchSongsResults READ searchSongsResults CONSTANT)
    Q_PROPERTY(QObject *singerList READ singerList CONSTANT)
    Q_PROPERTY(QObject *toplistList READ toplistList CONSTANT)
public:
    int songSource = 0;
    int nowIndex = 0;
    QString globalid;
    QString globaltagid;
    int loadState = 0;
    FakeLegacyListModel allPlaylistMenuModel;
    FakeLegacyListModel hotlistMenuModel;
    FakeLegacyListModel hotPlayListsModel;
    FakeLegacyListModel musicPlaylistsModel;
    FakeLegacyListModel newSongsModel;
    FakeLegacyListModel personalFmModel;
    FakeLegacyListModel personalRadarModel;
    FakeLegacyListModel playlistSongModel;
    FakeLegacyListModel recommendSongsModel;
    FakeLegacyListModel searchSongsResultsModel;
    FakeLegacyListModel singerListModel;
    FakeLegacyListModel toplistListModel;

    QObject *allPlaylistMenu() { return &allPlaylistMenuModel; }
    QObject *getHotlistMenu() { return &hotlistMenuModel; }
    QObject *hotPlayLists() { return &hotPlayListsModel; }
    QObject *musicPlaylists() { return &musicPlaylistsModel; }
    QObject *newSongs() { return &newSongsModel; }
    QObject *personalFm() { return &personalFmModel; }
    QObject *personalRadar() { return &personalRadarModel; }
    QObject *playlistSong() { return &playlistSongModel; }
    QObject *recommendSongs() { return &recommendSongsModel; }
    QObject *searchSongsResults() { return &searchSongsResultsModel; }
    QObject *singerList() { return &singerListModel; }
    QObject *toplistList() { return &toplistListModel; }

    Q_INVOKABLE void getHotPlaylistMenu(int) {}
    Q_INVOKABLE void getHotPlaylists(int) {}
    Q_INVOKABLE void getPlaylistMenu(int) {}
    Q_INVOKABLE void getNewSongs(int, int, int) {}
    Q_INVOKABLE void getAllToplist() {}
    Q_INVOKABLE void getHotSingers(int, int, int) {}
    Q_INVOKABLE void getSingerCategory(int, int, int, int) {}

signals:
    void songSourceChanged();
    void nowIndexChanged();
    void globalidChanged();
    void globaltagidChanged();
    void loadStateChanged();
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

class FakePageWindow final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *completedStart READ completedStart CONSTANT)
    Q_PROPERTY(int exitIndex MEMBER exitIndex NOTIFY exitIndexChanged)
public:
    FakeCompletedStart completed;
    int exitIndex = 0;
    QObject *completedStart() { return &completed; }

signals:
    void exitIndexChanged();
};

class PageTestContext final {
public:
    explicit PageTestContext(QQmlEngine &engine)
    {
        const QVariantMap themes{
            {QStringLiteral("containColor"), QStringLiteral("#ffffff")},
            {QStringLiteral("fontColor"), QStringLiteral("#202020")},
            {QStringLiteral("fullColor"), QStringLiteral("#ffffff")},
            {QStringLiteral("hoverColor"), QStringLiteral("#10000000")},
            {QStringLiteral("primaryBlurColor"), QStringLiteral("#f0f0f0")},
            {QStringLiteral("primaryColor"), QStringLiteral("#ffffff")},
            {QStringLiteral("secondaryColor"), QStringLiteral("#f5f5f5")},
            {QStringLiteral("shadowColor"), QStringLiteral("#20000000")},
            {QStringLiteral("sideColor"), QStringLiteral("#eeeeee")},
            {QStringLiteral("textColor"), QStringLiteral("#505050")},
            {QStringLiteral("themeColor"), QStringLiteral("#3481fa")}};
        const QVariantMap settings{
            {QStringLiteral("cubeRadius"), 8}, {QStringLiteral("labelRadius"), 12},
            {QStringLiteral("pageTitle"), 20}, {QStringLiteral("text"), 14},
            {QStringLiteral("textH1"), 18}, {QStringLiteral("textH2"), 16},
            {QStringLiteral("textTip"), 12}, {QStringLiteral("texticon"), 16},
            {QStringLiteral("textmain"), 14}};
        const QVariantMap lastSong{{QStringLiteral("hash"), QString{}},
                                  {QStringLiteral("name"), QString{}},
                                  {QStringLiteral("artist"), QString{}},
                                  {QStringLiteral("cover"), QString{}},
                                  {QStringLiteral("source"), 0}};

        QQmlContext *context = engine.rootContext();
        context->setContextProperty(QStringLiteral("MusicApi"), &musicApi);
        context->setContextProperty(QStringLiteral("window"), &window);
        context->setContextProperty(QStringLiteral("favoritesList"), &favoritesList);
        context->setContextProperty(QStringLiteral("favoritesSong"), &favoritesSong);
        context->setContextProperty(QStringLiteral("favoritesArtist"), &favoritesArtist);
        context->setContextProperty(QStringLiteral("playListModel"), &playListModel);
        context->setContextProperty(QStringLiteral("Style"),
                                    QVariantMap{{QStringLiteral("themes"), themes},
                                                {QStringLiteral("settings"), settings}});
        context->setContextProperty(QStringLiteral("Options"),
                                    QVariantMap{{QStringLiteral("lastSongs"), lastSong},
                                                {QStringLiteral("settings"),
                                                 QVariantMap{{QStringLiteral("soundQuality"), 0}}}});
        context->setContextProperty(QStringLiteral("iconFont"),
                                    QVariantMap{{QStringLiteral("name"), QString{}}});
        context->setContextProperty(QStringLiteral("mainLayout"),
                                    QVariantMap{{QStringLiteral("state"), QString{}}});
        context->setContextProperty(QStringLiteral("mainSearchInput"),
                                    QVariantMap{{QStringLiteral("text"), QString{}}});
        context->setContextProperty(QStringLiteral("mainWarn"), &mainWarn);
    }

private:
    FakeLegacyMusicApi musicApi;
    FakePageWindow window;
    FakeLegacyListModel favoritesList;
    FakeLegacyListModel favoritesSong;
    FakeLegacyListModel favoritesArtist;
    FakeLegacyListModel playListModel;
    FakeLegacyListModel mainWarn;
};

class QmlDiagnosticCapture final {
public:
    QmlDiagnosticCapture()
    {
        Q_ASSERT(!activeCapture);
        activeCapture = this;
        previousHandler = qInstallMessageHandler(messageHandler);
    }

    ~QmlDiagnosticCapture()
    {
        qInstallMessageHandler(previousHandler);
        activeCapture = nullptr;
    }

    QString runtimeErrors() const
    {
        QStringList errors;
        for (const QString &message : messages) {
            if (message.contains(QStringLiteral("ReferenceError"))
                || message.contains(QStringLiteral("TypeError"))
                || message.contains(QStringLiteral("Cannot read property"))
                || message.contains(QStringLiteral("Cannot call method"))
                || message.contains(QStringLiteral("is not defined"))
                || message.contains(QStringLiteral("is not a function")))
                errors << message;
        }
        return errors.join(QLatin1Char('\n'));
    }

private:
    static void messageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
    {
        if (activeCapture)
            activeCapture->messages << message;
    }

    inline static QmlDiagnosticCapture *activeCapture = nullptr;
    QtMessageHandler previousHandler = nullptr;
    QStringList messages;
};

namespace {
std::unique_ptr<QObject> load(QQmlEngine &engine, const QString &name,
                              const QVariantMap &properties, QString *error)
{
    QQmlComponent component(&engine,
        QUrl(QStringLiteral("qrc:/QueMusic/components/") + name));
    if (component.status() != QQmlComponent::Ready) {
        *error = component.errorString();
        return {};
    }
    auto object = std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
    if (!object) *error = component.errorString();
    return object;
}

std::unique_ptr<QObject> loadUrl(QQmlEngine &engine, const QString &path,
                                 const QVariantMap &properties, QString *error)
{
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/QueMusic/") + path));
    if (component.status() != QQmlComponent::Ready) {
        *error = component.errorString();
        return {};
    }
    auto object = std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
    if (!object) *error = component.errorString();
    return object;
}

QVariantMap actionAvailability(int state, const QString &reasonKey = {})
{
    return {{QStringLiteral("state"), state},
            {QStringLiteral("reasonKey"), reasonKey},
            {QStringLiteral("constraints"), QVariantMap{}}};
}

QVariantMap mediaItem(const QVariantMap &actions)
{
    return {
        {QStringLiteral("ref"), QVariantMap{
            {QStringLiteral("sourcePluginId"), QStringLiteral("org.quemusic.test")},
            {QStringLiteral("sourceInstanceId"), QStringLiteral("source-a")},
            {QStringLiteral("accountId"), QStringLiteral("account-a")},
            {QStringLiteral("entityType"), 0},
            {QStringLiteral("entityId"), QStringLiteral("track-42")}}},
        {QStringLiteral("title"), QStringLiteral("Test Track")},
        {QStringLiteral("subtitle"), QStringLiteral("Test Artist")},
        {QStringLiteral("availableActions"), actions}
    };
}

QVariant invokeVariant(QObject *object, const char *method, const QVariant &argument)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(object, method,
        Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
    Q_ASSERT(invoked);
    return result;
}

QVariant invokeVariant(QObject *object, const char *method)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(object, method,
        Q_RETURN_ARG(QVariant, result));
    Q_ASSERT(invoked);
    return result;
}

QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName)
{
    if (!root) return nullptr;
    if (root->objectName() == objectName) return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = findVisualItem(child, objectName)) return match;
    }
    return nullptr;
}
}

class MusicHubQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void musicPagesLoadWithNullDependencies()
    {
        for (const QString &page : {QStringLiteral("pages/HomePage.qml"),
                                    QStringLiteral("pages/PlaylistPage.qml"),
                                    QStringLiteral("pages/FavouritePage.qml"),
                                    QStringLiteral("pages/SearchPage.qml")}) {
            QQmlEngine engine;
            PageTestContext context(engine);
            QmlDiagnosticCapture diagnostics;
            QString error;
            auto object = loadUrl(engine, page,
                                  {{QStringLiteral("musicAdapter"), QVariant::fromValue<QObject *>(nullptr)},
                                   {QStringLiteral("playbackAdapter"), QVariant::fromValue<QObject *>(nullptr)}},
                                  &error);
            QVERIFY2(object, qPrintable(page + QStringLiteral(": ") + error));
            QCoreApplication::processEvents();
            QVERIFY2(diagnostics.runtimeErrors().isEmpty(),
                     qPrintable(page + QStringLiteral(" emitted a QML runtime error:\n")
                                + diagnostics.runtimeErrors()));
        }
    }

    void musicPagesAcceptNullableAdapterInjection()
    {
        for (const QString &page : {QStringLiteral("pages/HomePage.qml"),
                                    QStringLiteral("pages/PlaylistPage.qml"),
                                    QStringLiteral("pages/FavouritePage.qml"),
                                    QStringLiteral("pages/SearchPage.qml")}) {
            QQmlEngine engine;
            PageTestContext context(engine);
            QmlDiagnosticCapture diagnostics;
            FakeHub hub;
            FakePlayback playback;
            QString error;
            auto object = loadUrl(engine, page,
                                  {{QStringLiteral("musicAdapter"), QVariant::fromValue(&hub)},
                                   {QStringLiteral("playbackAdapter"), QVariant::fromValue(&playback)}},
                                  &error);
            QVERIFY2(object, qPrintable(page + QStringLiteral(": ") + error));
            QCOMPARE(object->property("musicAdapter").value<QObject *>(), &hub);
            QCOMPARE(object->property("playbackAdapter").value<QObject *>(),
                     static_cast<QObject *>(&playback));
            QCoreApplication::processEvents();
            QVERIFY2(diagnostics.runtimeErrors().isEmpty(),
                     qPrintable(page + QStringLiteral(" emitted a QML runtime error:\n")
                                + diagnostics.runtimeErrors()));
        }
    }

    void navigationHasNoSourceLibraryRoute()
    {
        QQmlEngine engine;
        PageTestContext context(engine);
        QmlDiagnosticCapture diagnostics;
        QString error;
        auto content = loadUrl(engine, QStringLiteral("layout/MainContent.qml"), {}, &error);
        QVERIFY2(content, qPrintable(error));
        QVERIFY(!content->findChild<QObject *>(QStringLiteral("sourceLibrary" "Page")));
        QCOMPARE(content->metaObject()->indexOfSignal("configureSourceRequested()"), -1);
        for (const QVariant &invalid : {QVariant(-1), QVariant(2), QVariant(7),
                                        QVariant(1.5), QVariant(QStringLiteral("1")), QVariant()})
            QCOMPARE(invokeVariant(content.get(), "contentIndexed", invalid).toBool(), false);
        QCOMPARE(content->property("pageIndex").toInt(), 0);
        QVERIFY2(diagnostics.runtimeErrors().isEmpty(),
                 qPrintable(QStringLiteral("invalid navigation emitted a QML runtime error:\n")
                            + diagnostics.runtimeErrors()));

        QObject *home = content->findChild<QObject *>(QStringLiteral("homePageLoader"));
        QObject *playlist = content->findChild<QObject *>(QStringLiteral("playlistPageLoader"));
        QObject *favourite = content->findChild<QObject *>(QStringLiteral("favouritePageLoader"));
        QObject *file = content->findChild<QObject *>(QStringLiteral("filePageLoader"));
        QObject *download = content->findChild<QObject *>(QStringLiteral("downloadPageLoader"));
        QObject *search = content->findChild<QObject *>(QStringLiteral("searchPageLoader"));
        QVERIFY(home);
        QVERIFY(playlist);
        QVERIFY(favourite);
        QVERIFY(file);
        QVERIFY(download);
        QVERIFY(search);
        QCOMPARE(home->property("active").toBool(), true);
        QCOMPARE(home->property("visible").toBool(), true);
        QCOMPARE(playlist->property("active").toBool(), false);
        QCOMPARE(playlist->property("visible").toBool(), false);

        QCOMPARE(invokeVariant(content.get(), "contentIndexed", 1).toBool(), true);
        QCOMPARE(content->property("pageIndex").toInt(), 1);
        QCOMPARE(home->property("active").toBool(), false);
        QCOMPARE(home->property("visible").toBool(), false);
        QCOMPARE(playlist->property("active").toBool(), true);
        QCOMPARE(playlist->property("visible").toBool(), true);

        QCOMPARE(invokeVariant(content.get(), "contentIndexed", 3).toBool(), true);
        QCOMPARE(content->property("pageIndex").toInt(), 3);
        QCOMPARE(playlist->property("active").toBool(), false);
        QCOMPARE(playlist->property("visible").toBool(), false);
        QCOMPARE(favourite->property("active").toBool(), true);
        QCOMPARE(favourite->property("visible").toBool(), true);

        QVERIFY2(diagnostics.runtimeErrors().isEmpty(),
                 qPrintable(QStringLiteral("navigation emitted a QML runtime error:\n")
                            + diagnostics.runtimeErrors()));

        FakeContentController controller;
        FakeNavigationWindow window;
        auto sidebar = loadUrl(engine, QStringLiteral("layout/LeftSideBar.qml"),
                               {{QStringLiteral("contentController"),
                                 QVariant::fromValue(&controller)},
                                {QStringLiteral("windowObject"),
                                 QVariant::fromValue(&window)}}, &error);
        QVERIFY2(sidebar, qPrintable(error));
        QVERIFY(!sidebar->findChild<QObject *>(QStringLiteral("sourceLibraryChoice")));
        QObject *navigation = sidebar->findChild<QObject *>(QStringLiteral("sidebarNavigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->property("count").toInt(), 6);
        QCOMPARE(invokeVariant(sidebar.get(), "contentIndexForNav", 5).toInt(), 5);
        QCOMPARE(invokeVariant(sidebar.get(), "navigate", 1).toBool(), true);
        QCOMPARE(controller.lastIndex, 1);

        controller.pageIndex = 6;
        controller.lastIndex = -1;
        window.exitIndex = 1;
        emit window.exit();
        QCOMPARE(controller.lastIndex, 1);
    }

    void mainContentAcceptsNullableAdapterInjection()
    {
        QQmlEngine engine;
        PageTestContext context(engine);
        FakeHub hub;
        FakePlayback playback;
        QString error;
        auto content = loadUrl(engine, QStringLiteral("layout/MainContent.qml"),
                               {{QStringLiteral("musicAdapter"), QVariant::fromValue(&hub)},
                                {QStringLiteral("playbackAdapter"),
                                 QVariant::fromValue(&playback)},
                                {QStringLiteral("width"), 900},
                                {QStringLiteral("height"), 700}}, &error);
        QVERIFY2(content, qPrintable(error));
        QCOMPARE(content->property("musicAdapter").value<QObject *>(), &hub);
        QCOMPARE(content->property("playbackAdapter").value<QObject *>(),
                 static_cast<QObject *>(&playback));
    }

    void sharedComponentsLoadWithNullDependencies()
    {
        QQmlEngine engine;
        QString error;
        auto selector = load(engine, QStringLiteral("SourceScopeSelector.qml"),
                             {{QStringLiteral("modelObject"), QVariant::fromValue<QObject *>(nullptr)}}, &error);
        QVERIFY2(selector, qPrintable(error));
        auto section = load(engine, QStringLiteral("MusicSectionView.qml"),
                            {{QStringLiteral("hub"), QVariant::fromValue<QObject *>(nullptr)},
                             {QStringLiteral("modelObject"), QVariant::fromValue<QObject *>(nullptr)},
                             {QStringLiteral("playback"), QVariant::fromValue<QObject *>(nullptr)}}, &error);
        QVERIFY2(section, qPrintable(error));
        auto menu = load(engine, QStringLiteral("MediaActionMenu.qml"),
                         {{QStringLiteral("hub"), QVariant::fromValue<QObject *>(nullptr)},
                          {QStringLiteral("item"), QVariantMap{}}}, &error);
        QVERIFY2(menu, qPrintable(error));
    }

    void sourceSelectorRejectsUnavailableSourceAndAcceptsAvailableSource()
    {
        QQmlEngine engine;
        FakeHub hub;
        hub.sourceOptions = {
            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("source-a")},
                        {QStringLiteral("displayName"), QStringLiteral("Source A")},
                        {QStringLiteral("available"), true}},
            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("source-b")},
                        {QStringLiteral("displayName"), QStringLiteral("Source B")},
                        {QStringLiteral("available"), false}}
        };
        hub.selectedSourceInstanceId = QStringLiteral("source-a");

        QString error;
        auto selector = load(engine, QStringLiteral("SourceScopeSelector.qml"),
                             {{QStringLiteral("modelObject"), QVariant::fromValue(&hub)}}, &error);
        QVERIFY2(selector, qPrintable(error));
        QTRY_COMPARE(selector->property("currentIndex").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(selector.get(), "activated", Q_ARG(int, 1)));
        QCOMPARE(hub.selectedSourceInstanceId, QStringLiteral("source-a"));
        QTRY_COMPARE(selector->property("currentIndex").toInt(), 0);

        hub.selectedSourceInstanceId = QStringLiteral("source-b");
        emit hub.selectedSourceInstanceIdChanged();
        QTRY_COMPARE(selector->property("currentIndex").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(selector.get(), "activated", Q_ARG(int, 0)));
        QCOMPARE(hub.selectedSourceInstanceId, QStringLiteral("source-a"));
    }

    void actionMenuKeepsUnsupportedHiddenAndUnavailableDisabled()
    {
        QQmlEngine engine;
        FakeHub hub;
        const QVariantMap actions{
            {QStringLiteral("3"), actionAvailability(1)},
            {QStringLiteral("4"), actionAvailability(1)},
            {QStringLiteral("5"), actionAvailability(2, QStringLiteral("music.loginRequired"))},
            {QStringLiteral("11"), actionAvailability(1)},
            {QStringLiteral("12"), actionAvailability(1)},
            {QStringLiteral("16"), actionAvailability(3, QStringLiteral("music.readOnly"))}
        };
        const QVariantMap item = mediaItem(actions);
        QString error;
        auto menu = load(engine, QStringLiteral("MediaActionMenu.qml"),
                         {{QStringLiteral("hub"), QVariant::fromValue(&hub)},
                          {QStringLiteral("item"), item}}, &error);
        QVERIFY2(menu, qPrintable(error));
        QQuickWindow window;
        window.resize(640, 480);
        window.show();
        QVERIFY(menu->setProperty("parent", QVariant::fromValue(window.contentItem())));
        QVERIFY(QMetaObject::invokeMethod(menu.get(), "open"));
        QTRY_VERIFY(menu->property("visible").toBool());

        QObject *download = menu->findChild<QObject *>(QStringLiteral("downloadAction"));
        QObject *favorite = menu->findChild<QObject *>(QStringLiteral("favoriteAction"));
        QObject *unfavorite = menu->findChild<QObject *>(QStringLiteral("unfavoriteAction"));
        QObject *rating = menu->findChild<QObject *>(QStringLiteral("ratingAction"));
        QObject *addPlaylist = menu->findChild<QObject *>(QStringLiteral("addPlaylistAction"));
        QObject *removePlaylist = menu->findChild<QObject *>(QStringLiteral("removePlaylistAction"));
        QVERIFY(download);
        QVERIFY(favorite);
        QVERIFY(unfavorite);
        QVERIFY(rating);
        QVERIFY(addPlaylist);
        QVERIFY(removePlaylist);
        QCOMPARE(download->property("visible").toBool(), true);
        QCOMPARE(download->property("enabled").toBool(), false);
        QCOMPARE(favorite->property("visible").toBool(), true);
        QCOMPARE(favorite->property("enabled").toBool(), true);
        QCOMPARE(unfavorite->property("visible").toBool(), true);
        QCOMPARE(unfavorite->property("enabled").toBool(), false);
        QCOMPARE(rating->property("visible").toBool(), false);
        QCOMPARE(invokeVariant(menu.get(), "shown", 3).toBool(), true);
        QCOMPARE(invokeVariant(menu.get(), "allowed", 4).toBool(), true);
        QCOMPARE(invokeVariant(menu.get(), "shown", 5).toBool(), true);
        QCOMPARE(invokeVariant(menu.get(), "allowed", 5).toBool(), false);
        QCOMPARE(unfavorite->property("reasonKey").toString(),
                 QStringLiteral("music.loginRequired"));
        QCOMPARE(invokeVariant(menu.get(), "shown", 11).toBool(), true);
        QCOMPARE(invokeVariant(menu.get(), "sameSourcePlaylistContext").toBool(), false);
        QCOMPARE(addPlaylist->property("visible").toBool(), true);
        QCOMPARE(addPlaylist->property("enabled").toBool(), false);
        QCOMPARE(removePlaylist->property("visible").toBool(), true);
        QCOMPARE(removePlaylist->property("enabled").toBool(), false);
        QCOMPARE(addPlaylist->property("reasonKey").toString(),
                 QStringLiteral("music.playlistSelectionRequired"));
        QCOMPARE(invokeVariant(menu.get(), "shown", 16).toBool(), true);
        QCOMPARE(invokeVariant(menu.get(), "allowed", 16).toBool(), false);
        QObject *bookmark = menu->findChild<QObject *>(QStringLiteral("bookmarkAction"));
        QVERIFY(bookmark);
        QCOMPARE(bookmark->property("visible").toBool(), true);
        QCOMPARE(bookmark->property("enabled").toBool(), false);
        QCOMPARE(bookmark->property("reasonKey").toString(), QStringLiteral("music.readOnly"));

        const QVariantMap crossPluginPlaylist{
            {QStringLiteral("playlist"), QVariantMap{
                {QStringLiteral("ref"), QVariantMap{
                    {QStringLiteral("sourcePluginId"), QStringLiteral("org.other.plugin")},
                    {QStringLiteral("sourceInstanceId"), QStringLiteral("source-a")},
                    {QStringLiteral("accountId"), QStringLiteral("account-a")},
                    {QStringLiteral("entityType"), 3},
                    {QStringLiteral("entityId"), QStringLiteral("playlist-1")}}}}}
        };
        QVERIFY(menu->setProperty("playlistContext", crossPluginPlaylist));
        QCOMPARE(invokeVariant(menu.get(), "sameSourcePlaylistContext").toBool(), false);

        QVariantMap exactPlaylist = crossPluginPlaylist;
        QVariantMap exactPlaylistItem = exactPlaylist.value(QStringLiteral("playlist")).toMap();
        QVariantMap exactPlaylistRef = exactPlaylistItem.value(QStringLiteral("ref")).toMap();
        exactPlaylistRef.insert(QStringLiteral("sourcePluginId"),
                                QStringLiteral("org.quemusic.test"));
        exactPlaylistItem.insert(QStringLiteral("ref"), exactPlaylistRef);
        exactPlaylist.insert(QStringLiteral("playlist"), exactPlaylistItem);
        QVERIFY(menu->setProperty("playlistContext", exactPlaylist));
        QCOMPARE(invokeVariant(menu.get(), "sameSourcePlaylistContext").toBool(), true);
        QTRY_COMPARE(addPlaylist->property("enabled").toBool(), true);
        QCOMPARE(removePlaylist->property("visible").toBool(), true);
        QCOMPARE(removePlaylist->property("enabled").toBool(), false);
        QCOMPARE(removePlaylist->property("reasonKey").toString(),
                 QStringLiteral("music.playlistSelectionRequired"));

        QVariantMap unavailableActions = actions;
        unavailableActions.insert(QStringLiteral("12"),
                                  actionAvailability(2, QStringLiteral("music.readOnly")));
        QVERIFY(menu->setProperty("item", mediaItem(unavailableActions)));
        QVERIFY(menu->setProperty("playlistContext", QVariant{}));
        QTRY_COMPARE(removePlaylist->property("reasonKey").toString(),
                     QStringLiteral("music.readOnly"));
    }

    void favoriteActionRoutesTheCompleteMediaItem()
    {
        QQmlEngine engine;
        FakeHub hub;
        const QVariantMap item = mediaItem({
            {QStringLiteral("4"), actionAvailability(1)}
        });
        QString error;
        auto menu = load(engine, QStringLiteral("MediaActionMenu.qml"),
                         {{QStringLiteral("hub"), QVariant::fromValue(&hub)},
                          {QStringLiteral("item"), item}}, &error);
        QVERIFY2(menu, qPrintable(error));
        QObject *favorite = menu->findChild<QObject *>(QStringLiteral("favoriteAction"));
        QVERIFY(favorite);
        QVERIFY(QMetaObject::invokeMethod(favorite, "triggered"));
        QCOMPARE(hub.actionRouter.favoriteCalls, 1);
        QCOMPARE(hub.actionRouter.receivedFavorite, true);
        QCOMPARE(hub.actionRouter.receivedItem, item);
    }

    void actionMenuRoutesAllSupportedOperationsWithExactContext()
    {
        QQmlEngine engine;
        FakeHub hub;
        QVariantMap availability;
        for (const int action : {3, 4, 5, 6, 11, 12, 16})
            availability.insert(QString::number(action), actionAvailability(1));
        const QVariantMap item = mediaItem(availability);
        const QVariantMap playlist{
            {QStringLiteral("ref"), QVariantMap{
                {QStringLiteral("sourcePluginId"), QStringLiteral("org.quemusic.test")},
                {QStringLiteral("sourceInstanceId"), QStringLiteral("source-a")},
                {QStringLiteral("accountId"), QStringLiteral("account-a")},
                {QStringLiteral("entityType"), 3},
                {QStringLiteral("entityId"), QStringLiteral("playlist-9")}}}
        };
        const QVariantMap playlistContext{
            {QStringLiteral("playlist"), playlist},
            {QStringLiteral("trackIndexesToRemove"), QVariantList{2}}
        };
        QString error;
        auto menu = load(engine, QStringLiteral("MediaActionMenu.qml"),
                         {{QStringLiteral("hub"), QVariant::fromValue(&hub)},
                          {QStringLiteral("item"), item},
                          {QStringLiteral("downloadDestination"),
                           QUrl(QStringLiteral("file:///tmp/music.flac"))},
                          {QStringLiteral("rating"), 4},
                          {QStringLiteral("bookmarkPosition"), 12345},
                          {QStringLiteral("playlistContext"), playlistContext}}, &error);
        QVERIFY2(menu, qPrintable(error));

        auto trigger = [&](const QString &name) {
            QObject *action = menu->findChild<QObject *>(name);
            QVERIFY(action);
            QVERIFY(QMetaObject::invokeMethod(action, "triggered"));
        };
        trigger(QStringLiteral("downloadAction"));
        QCOMPARE(hub.actionRouter.lastMethod, QStringLiteral("download"));
        QCOMPARE(hub.actionRouter.receivedItem, item);
        QCOMPARE(hub.actionRouter.receivedDestination,
                 QUrl(QStringLiteral("file:///tmp/music.flac")));
        trigger(QStringLiteral("favoriteAction"));
        QCOMPARE(hub.actionRouter.receivedFavorite, true);
        QCOMPARE(hub.actionRouter.receivedItem, item);
        trigger(QStringLiteral("unfavoriteAction"));
        QCOMPARE(hub.actionRouter.receivedFavorite, false);
        QCOMPARE(hub.actionRouter.receivedItem, item);
        trigger(QStringLiteral("ratingAction"));
        QCOMPARE(hub.actionRouter.lastMethod, QStringLiteral("setRating"));
        QCOMPARE(hub.actionRouter.receivedRating, 4);
        QCOMPARE(hub.actionRouter.receivedItem, item);
        trigger(QStringLiteral("addPlaylistAction"));
        QCOMPARE(hub.actionRouter.receivedPlaylist, playlist);
        QCOMPARE(hub.actionRouter.receivedChange.value(QStringLiteral("tracksToAdd")).toList(),
                 QVariantList{item});
        trigger(QStringLiteral("removePlaylistAction"));
        QCOMPARE(hub.actionRouter.receivedPlaylist, playlist);
        QCOMPARE(hub.actionRouter.receivedChange.value(
                     QStringLiteral("trackIndexesToRemove")).toList(), QVariantList{2});
        trigger(QStringLiteral("bookmarkAction"));
        QCOMPARE(hub.actionRouter.lastMethod, QStringLiteral("setBookmark"));
        QCOMPARE(hub.actionRouter.receivedBookmark, 12345);
        QCOMPARE(hub.actionRouter.receivedItem, item);
    }

    void sectionActivationRoutesCompleteTrackAndBrowsableItems()
    {
        QQmlEngine engine;
        FakeHub hub;
        FakePlayback playback;
        QString error;
        auto section = load(engine, QStringLiteral("MusicSectionView.qml"),
                            {{QStringLiteral("hub"), QVariant::fromValue(&hub)},
                             {QStringLiteral("modelObject"), QVariant::fromValue<QObject *>(nullptr)},
                             {QStringLiteral("playback"), QVariant::fromValue(&playback)}}, &error);
        QVERIFY2(section, qPrintable(error));

        const QVariantMap track = mediaItem({});
        QVERIFY(QMetaObject::invokeMethod(section.get(), "activateItem",
                                         Q_ARG(QVariant, track)));
        QCOMPARE(playback.playCalls, 1);
        QCOMPARE(playback.playedItem, track);
        QCOMPARE(hub.browseCalls, 0);

        for (const int entityType : {1, 2, 3, 4}) {
            QVariantMap browsable = track;
            QVariantMap browsableRef = browsable.value(QStringLiteral("ref")).toMap();
            browsableRef.insert(QStringLiteral("entityType"), entityType);
            browsableRef.insert(QStringLiteral("entityId"),
                                QStringLiteral("browse-%1").arg(entityType));
            browsable.insert(QStringLiteral("ref"), browsableRef);
            QVERIFY(QMetaObject::invokeMethod(section.get(), "activateItem",
                                             Q_ARG(QVariant, browsable)));
            QCOMPARE(hub.browseCalls, entityType);
            QCOMPARE(hub.browsedItem, browsable);
        }
        QCOMPARE(playback.playCalls, 1);

        QVariantMap directory = track;
        QVariantMap directoryRef = directory.value(QStringLiteral("ref")).toMap();
        directoryRef.insert(QStringLiteral("entityType"), 5);
        directory.insert(QStringLiteral("ref"), directoryRef);
        QVERIFY(QMetaObject::invokeMethod(section.get(), "activateItem",
                                         Q_ARG(QVariant, directory)));
        QCOMPARE(playback.playCalls, 1);
        QCOMPARE(hub.browseCalls, 4);
    }

    void sectionRendersSuccessfulItemsFromPartialResultsAndRoutesControls()
    {
        QQmlEngine engine;
        FakeHub hub;
        hub.sourceOptions = {
            QVariantMap{{QStringLiteral("sourceInstanceId"), QStringLiteral("source-a")},
                        {QStringLiteral("displayName"), QStringLiteral("NAS A")},
                        {QStringLiteral("available"), true}}
        };
        FakePlayback playback;
        MusicPageModel model(MusicPageKindV2::Recommendation);
        MediaItemV2 typedItem;
        typedItem.ref = {QStringLiteral("org.quemusic.test"), QStringLiteral("source-a"),
                         QStringLiteral("account-a"), MediaEntityTypeV2::Track,
                         QStringLiteral("track-42")};
        typedItem.title = QStringLiteral("Kept Track");
        typedItem.artworkId = QStringLiteral("cover-42");
        PageSectionV2 typedSection;
        typedSection.sectionId = QStringLiteral("recent");
        typedSection.titleKey = QStringLiteral("Recently played");
        typedSection.layoutHint = QStringLiteral("horizontal");
        typedSection.items = {typedItem};
        typedSection.hasMore = true;
        SourceErrorV2 sourceError{SourceErrorKindV2::Network,
                                  QStringLiteral("source.networkUnavailable"), {}, {}, true};
        PageResultV2 result;
        result.sections = {typedSection};
        result.sourceStates.insert(QStringLiteral("source-b"),
                                   {SourcePageLoadStateV2::Failed, sourceError});
        const quint64 generation = model.beginRequest();
        QVERIFY(model.applyResult(generation, result));
        QVERIFY(model.finishGeneration(generation, 1));

        QString error;
        const QVariantMap viewProperties{
            {QStringLiteral("hub"), QVariant::fromValue(&hub)},
            {QStringLiteral("modelObject"), QVariant::fromValue(&model)},
            {QStringLiteral("playback"), QVariant::fromValue(&playback)},
            {QStringLiteral("pageKind"), 0},
            {QStringLiteral("width"), 800},
            {QStringLiteral("height"), 600}
        };
        auto view = load(engine, QStringLiteral("MusicSectionView.qml"), viewProperties, &error);
        QVERIFY2(view, qPrintable(error));
        QQuickWindow window;
        window.resize(800, 600);
        auto *viewItem = qobject_cast<QQuickItem *>(view.get());
        QVERIFY(viewItem);
        viewItem->setParentItem(window.contentItem());
        window.show();
        QObject *sections = view->findChild<QObject *>(QStringLiteral("musicSections"));
        QVERIFY(sections);
        QTRY_COMPARE(sections->property("count").toInt(), 1);
        QTRY_VERIFY(findVisualItem(window.contentItem(), QStringLiteral("mediaItem")));
        QQuickItem *media = findVisualItem(window.contentItem(), QStringLiteral("mediaItem"));
        QQuickItem *sectionItems = findVisualItem(window.contentItem(), QStringLiteral("sectionItems"));
        QQuickItem *badge = findVisualItem(media, QStringLiteral("sourceBadge"));
        QQuickItem *loadMore = findVisualItem(window.contentItem(), QStringLiteral("sectionLoadMore"));
        QQuickItem *retry = findVisualItem(window.contentItem(), QStringLiteral("sectionRetry"));
        QVERIFY(badge);
        QVERIFY(loadMore);
        QVERIFY(retry);
        QVERIFY(sectionItems);
        QVERIFY(sectionItems->property("columns").toInt() > 1);
        QCOMPARE(badge->property("text").toString(), QStringLiteral("NAS A"));
        QCOMPARE(media->property("fullItem").toMap().value(QStringLiteral("artworkId")).toString(),
                 QStringLiteral("cover-42"));
        QTRY_COMPARE(hub.artworkRef.value(QStringLiteral("entityId")).toString(),
                     QStringLiteral("track-42"));
        emit hub.artworkReady(QUuid::createUuid(), hub.artworkRef,
                              QUrl(QStringLiteral("file:///wrong-cover.jpg")));
        QCoreApplication::processEvents();
        QCOMPARE(media->property("artworkUrl").toUrl(), QUrl{});
        QVariantMap wrongRef = hub.artworkRef;
        wrongRef.insert(QStringLiteral("entityId"), QStringLiteral("other-track"));
        emit hub.artworkReady(hub.artworkRequestId, wrongRef,
                              QUrl(QStringLiteral("file:///wrong-ref.jpg")));
        QCoreApplication::processEvents();
        QCOMPARE(media->property("artworkUrl").toUrl(), QUrl{});
        emit hub.artworkReady(hub.artworkRequestId, hub.artworkRef,
                              QUrl(QStringLiteral("file:///cover-42.jpg")));
        QTRY_COMPARE(media->property("artworkUrl").toUrl(),
                     QUrl(QStringLiteral("file:///cover-42.jpg")));
        QVERIFY(QMetaObject::invokeMethod(media, "clicked"));
        QCOMPARE(playback.playCalls, 1);
        QCOMPARE(playback.playedItem.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Kept Track"));
        QVERIFY(QMetaObject::invokeMethod(loadMore, "clicked"));
        QCOMPARE(hub.loadMoreCalls, 1);
        QCOMPARE(hub.lastPageKind, 0);
        QCOMPARE(hub.lastSectionId, QStringLiteral("recent"));
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(hub.retryCalls, 1);
        QCOMPARE(hub.lastPageKind, 0);
        QCOMPARE(hub.lastSectionId, QStringLiteral("recent"));

        view.reset();
        QCOMPARE(hub.cancelledAssets, 0);

        auto pendingView = load(engine, QStringLiteral("MusicSectionView.qml"),
                                viewProperties, &error);
        QVERIFY2(pendingView, qPrintable(error));
        auto *pendingItem = qobject_cast<QQuickItem *>(pendingView.get());
        QVERIFY(pendingItem);
        pendingItem->setParentItem(window.contentItem());
        QTRY_VERIFY(findVisualItem(pendingItem, QStringLiteral("mediaItem")));
        pendingView.reset();
        QCOMPARE(hub.cancelledAssets, 1);
    }
};

QTEST_MAIN(MusicHubQmlTest)
#include "tst_MusicHubQml.moc"
