#include "MusicPageModel.h"

#include <QMetaObject>
#include <QPointer>
#include <QUuid>
#include <QTest>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlComponent>
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
            QString error;
            auto object = loadUrl(engine, page,
                                  {{QStringLiteral("hub"), QVariant::fromValue<QObject *>(nullptr)},
                                   {QStringLiteral("playback"), QVariant::fromValue<QObject *>(nullptr)}},
                                  &error);
            QVERIFY2(object, qPrintable(page + QStringLiteral(": ") + error));
        }
    }

    void musicPagesBindExactHubModelsAndActivatePageKinds()
    {
        struct PageCase { QString path; int kind; QObject *(FakeHub::*model)(); };
        const QList<PageCase> pages{
            {QStringLiteral("pages/HomePage.qml"), 0, &FakeHub::recommendation},
            {QStringLiteral("pages/PlaylistPage.qml"), 1, &FakeHub::category},
            {QStringLiteral("pages/FavouritePage.qml"), 2, &FakeHub::favorites},
            {QStringLiteral("pages/SearchPage.qml"), 3, &FakeHub::searchResults}
        };
        for (const auto &page : pages) {
            QQmlEngine engine;
            FakeHub hub;
            FakePlayback playback;
            QString error;
            auto object = loadUrl(engine, page.path,
                                  {{QStringLiteral("hub"), QVariant::fromValue(&hub)},
                                   {QStringLiteral("playback"), QVariant::fromValue(&playback)},
                                   {QStringLiteral("pageActive"), true}}, &error);
            QVERIFY2(object, qPrintable(page.path + QStringLiteral(": ") + error));
            QObject *selector = object->findChild<QObject *>(QStringLiteral("sourceScopeSelector"));
            QObject *sections = object->findChild<QObject *>(QStringLiteral("musicSections"));
            QVERIFY2(selector, qPrintable(page.path));
            QVERIFY2(sections, qPrintable(page.path));
            QCOMPARE(selector->property("modelObject").value<QObject *>(), &hub);
            QCOMPARE(sections->parent()->property("modelObject").value<QObject *>(),
                     (hub.*page.model)());
            QCOMPARE(sections->parent()->property("playback").value<QObject *>(),
                     static_cast<QObject *>(&playback));
            QCOMPARE(hub.lastActivatedPage, page.kind);
        }
    }

    void searchAndCategoryExposeHostSafeCommands()
    {
        QQmlEngine engine;
        FakeHub hub;
        QString error;
        auto searchPage = loadUrl(engine, QStringLiteral("pages/SearchPage.qml"),
                                  {{QStringLiteral("hub"), QVariant::fromValue(&hub)}}, &error);
        QVERIFY2(searchPage, qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(searchPage.get(), "submitSearch",
                                         Q_ARG(QVariant, QStringLiteral("  jazz  "))));
        QCOMPARE(hub.searchedText, QStringLiteral("jazz"));

        auto categoryPage = loadUrl(engine, QStringLiteral("pages/PlaylistPage.qml"),
                                    {{QStringLiteral("hub"), QVariant::fromValue(&hub)}}, &error);
        QVERIFY2(categoryPage, qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(categoryPage.get(), "goBack"));
        QCOMPARE(hub.navigateBackCalls, 1);
    }

    void navigationHasNoSourceLibraryRoute()
    {
        QQmlEngine engine;
        QString error;
        auto content = loadUrl(engine, QStringLiteral("layout/MainContent.qml"), {}, &error);
        QVERIFY2(content, qPrintable(error));
        QVERIFY(!content->findChild<QObject *>(QStringLiteral("sourceLibraryPage")));
        QCOMPARE(content->metaObject()->indexOfSignal("configureSourceRequested()"), -1);
        QVariant accepted;
        QVERIFY(QMetaObject::invokeMethod(content.get(), "contentIndexed",
                                         Q_RETURN_ARG(QVariant, accepted), Q_ARG(QVariant, 7)));
        QCOMPARE(accepted.toBool(), false);

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

    void mainContentInjectsHubAndActivatesSelectedLoader()
    {
        QQmlEngine engine;
        FakeHub hub;
        FakePlayback playback;
        QString error;
        auto content = loadUrl(engine, QStringLiteral("layout/MainContent.qml"),
                               {{QStringLiteral("musicHub"), QVariant::fromValue(&hub)},
                                {QStringLiteral("playbackCoordinator"),
                                 QVariant::fromValue(&playback)},
                                {QStringLiteral("width"), 900},
                                {QStringLiteral("height"), 700}}, &error);
        QVERIFY2(content, qPrintable(error));
        QTRY_COMPARE(hub.lastActivatedPage, 0);
        QObject *selector = content->findChild<QObject *>(QStringLiteral("sourceScopeSelector"));
        QVERIFY(selector);
        QCOMPARE(selector->property("modelObject").value<QObject *>(), &hub);
        QObject *homeSections = content->findChild<QObject *>(QStringLiteral("musicSections"));
        QVERIFY(homeSections);
        QCOMPARE(homeSections->parent()->property("playback").value<QObject *>(),
                 static_cast<QObject *>(&playback));

        QVariant accepted;
        QVERIFY(QMetaObject::invokeMethod(content.get(), "contentIndexed",
                                         Q_RETURN_ARG(QVariant, accepted), Q_ARG(QVariant, 1)));
        QCOMPARE(accepted.toBool(), true);
        QTRY_COMPARE(hub.lastActivatedPage, 1);
        auto hasCategorySections = [&] {
            const auto sections = content->findChildren<QObject *>(
                QStringLiteral("musicSections"));
            for (QObject *list : sections) {
                if (list->parent()->property("modelObject").value<QObject *>()
                        == static_cast<QObject *>(&hub.categoryModel))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(hasCategorySections());
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
