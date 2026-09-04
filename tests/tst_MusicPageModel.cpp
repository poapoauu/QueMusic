#include "MusicPageModel.h"
#include "SourceScopeStore.h"

#include <QAbstractItemModelTester>
#include <QJSEngine>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

PageResultV2 sampleResult(const QString &title, const QString &id = QStringLiteral("recent"))
{
    MediaItemV2 item;
    item.ref = {"navidrome", "navidrome/home", "home-account",
                MediaEntityTypeV2::Track, "song-1"};
    item.title = title;
    PageSectionV2 section;
    section.sectionId = id;
    section.titleKey = title;
    section.kind = PageSectionKindV2::RecentlyPlayed;
    section.layoutHint = "horizontal";
    section.items = {item};
    return {{section}, {}, false, true};
}

SourceErrorV2 offlineError()
{
    return {SourceErrorKindV2::Unavailable, "source.disabled", "Source instance is disabled",
            std::nullopt, false};
}

QVariantMap sourceState(const MusicPageModel &model, const QString &id)
{
    for (const auto &value : model.sourceStates()) {
        const auto map = value.toMap();
        if (map.value("sourceInstanceId").toString() == id)
            return map;
    }
    return {};
}

} // namespace

class MusicPageModelTest : public QObject {
    Q_OBJECT
private slots:
    void sourceScopePersistsAcrossInstances();
    void aggregateIsDefaultAndPersistsAsEmptyId();
    void dropsResultsFromOldGeneration();
    void upsertsByStableIdWithoutClearingSiblings();
    void onlyFinalNetworkResponsesCompleteSections();
    void emptyResponsesCountQueriesNotRows();
    void keepsCachedSectionsVisibleWhileRefreshing();
    void successfulRefreshRemovesPreviousGenerationSections();
    void preservesFailedSourceDetailsAndSuccessfulSiblings();
    void finalNetworkStateReplacesCachedFailure();
    void typedFailureWaitsForAllExpectedSections();
    void failedRefreshKeepsCachedItems();
    void emptyFinalResultKeepsCacheOnlyWhenAllSourcesFailed_data();
    void emptyFinalResultKeepsCacheOnlyWhenAllSourcesFailed();
    void qmlRolesExposeTypedRefsAndOnlySuppliedAvailability();
    void qmlReadsPageStateAndNestedItemValues();
    void failedSourceWithoutOptionalErrorIsNotEmpty();
    void cancellationAndContextReset();
    void independentSectionUpdatesAfterFinish();
    void cancellationStopsSourceLoading();
    void scopedFailureWithoutErrorIsNotSuccessfulEmpty();
    void refreshDataChangedCannotOverwriteNewContext();
};

void MusicPageModelTest::refreshDataChangedCannotOverwriteNewContext()
{
    MusicPageModel model(MusicPageKindV2::Search);
    const auto initial = model.beginRequest();
    QVERIFY(model.applyResult(initial, sampleResult("old")));
    QVERIFY(model.finishGeneration(initial, 1));
    const auto interrupted = initial + 1;
    quint64 replacement = 0;
    bool replaced = false;
    connect(&model, &MusicPageModel::dataChanged, &model, [&] {
        if (replaced) return;
        replaced = true;
        QVERIFY(model.resetGeneration(interrupted));
        replacement = model.beginRequest();
        QVERIFY(model.finishGeneration(replacement, 0));
    });
    const auto returned = model.beginRequest();
    QVERIFY(replaced);
    QCOMPARE(model.state(), PageLoadStateV2::Empty);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(returned, interrupted);
    QVERIFY(!model.applyResult(interrupted, sampleResult("late")));
    QVERIFY(model.finishGeneration(replacement, 0));
}

void MusicPageModelTest::scopedFailureWithoutErrorIsNotSuccessfulEmpty()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    const auto generation = model.beginRequest();
    auto result = sampleResult("empty"); result.sections[0].items.clear();
    QVERIFY(model.applyResult(generation, result)); QVERIFY(model.finishGeneration(generation, 1));
    QVERIFY(model.beginSectionRequest(generation, "recent"));
    result.sourceStates.insert("navidrome/home", {SourcePageLoadStateV2::Failed, {}});
    QVERIFY(model.applySectionResult(generation, "recent", result, false));
    QCOMPARE(model.state(), PageLoadStateV2::Failed);
}

void MusicPageModelTest::cancellationStopsSourceLoading()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    const auto generation = model.beginRequest();
    auto result = sampleResult("accepted");
    result.complete = false;
    result.sourceStates.insert("navidrome/home", {SourcePageLoadStateV2::Loading, {}});
    QVERIFY(model.applyResult(generation, result));
    QVERIFY(model.cancelGeneration(generation));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QCOMPARE(sourceState(model, "navidrome/home")["state"].toInt(), int(SourcePageLoadStateV2::Ready));
}

void MusicPageModelTest::cancellationAndContextReset()
{
    MusicPageModel model(MusicPageKindV2::Search);
    auto generation = model.beginRequest();
    QVERIFY(model.cancelGeneration(generation));
    QCOMPARE(model.state(), PageLoadStateV2::Idle);
    QVERIFY(!model.applyResult(generation, sampleResult("late")));
    generation = model.beginRequest();
    QVERIFY(model.applyResult(generation, sampleResult("accepted")));
    QVERIFY(model.cancelGeneration(generation));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QCOMPARE(model.rowCount(), 1);
    generation = model.beginRequest();
    QVERIFY(!model.resetGeneration(generation - 1));
    QVERIFY(model.resetGeneration(generation));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.state(), PageLoadStateV2::Idle);
    QVERIFY(!model.applyResult(generation, sampleResult("stale")));
}

void MusicPageModelTest::independentSectionUpdatesAfterFinish()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto generation = model.beginRequest();
    auto first = sampleResult("first");
    first.sections[0].hasMore = true;
    first.sections[0].nextCursor = "cursor-a";
    QVERIFY(model.applyResult(generation, first));
    QVERIFY(model.applyResult(generation, sampleResult("sibling", "other")));
    QVERIFY(model.finishGeneration(generation, 2));
    QVERIFY(model.beginSectionRequest(generation, "recent"));
    QVERIFY(!model.beginSectionRequest(generation, "recent"));
    QVERIFY(model.beginSectionRequest(generation, "other"));
    QVERIFY(model.data(model.index(0), MusicPageModel::LoadingMoreRole).toBool());
    QVERIFY(model.applySectionFailure(generation, "recent", offlineError()));
    QCOMPARE(model.section(0).nextCursor, "cursor-a");
    QCOMPARE(model.section(0).items.size(), 1);
    QVERIFY(model.beginSectionRequest(generation, "recent"));
    auto next = sampleResult("next");
    next.sections.append(sampleResult("must-ignore", "other").sections[0]);
    QVERIFY(model.applySectionResult(generation, "recent", next, true));
    QCOMPARE(model.section(0).items.size(), 2);
    QCOMPARE(model.section(1).items[0].title, "sibling");
    QVERIFY(model.applySectionResult(generation, "other", sampleResult("replacement", "other"), false));
    QCOMPARE(model.section(1).items.size(), 1);
    QCOMPARE(model.section(1).items[0].title, "replacement");
    QVERIFY(model.beginSectionRequest(generation, "recent"));
    QVERIFY(model.cancelGeneration(generation));
    QVERIFY(!model.data(model.index(0), MusicPageModel::LoadingMoreRole).toBool());
    QVERIFY(!model.applySectionResult(generation, "recent", next, true));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
}

// Missing the settings write would lose the shared selection after restart.
void MusicPageModelTest::sourceScopePersistsAcrossInstances()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("scope.ini");
    {
        QSettings settings(path, QSettings::IniFormat);
        SourceScopeStore first(&settings);
        QSignalSpy changes(&first, &SourceScopeStore::selectedSourceInstanceIdChanged);
        first.setSelectedSourceInstanceId("navidrome/home");
        first.setSelectedSourceInstanceId("navidrome/home");
        QCOMPARE(changes.count(), 1);
        SourceScopeStore restored(&settings);
        QCOMPARE(restored.selectedSourceInstanceId(), "navidrome/home");
        QCOMPARE(settings.allKeys(), QStringList{"MusicHub/selectedSourceInstanceId"});
    }
    QSettings reopened(path, QSettings::IniFormat);
    SourceScopeStore restored(&reopened);
    QCOMPARE(restored.property("selectedSourceInstanceId").toString(), "navidrome/home");
}

// A default source or localized label would stop aggregate restoration working.
void MusicPageModelTest::aggregateIsDefaultAndPersistsAsEmptyId()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath("scope.ini"), QSettings::IniFormat);
    SourceScopeStore scope(&settings);
    QVERIFY(scope.selectedSourceInstanceId().isEmpty());
    scope.setSelectedSourceInstanceId("navidrome/home");
    scope.setSelectedSourceInstanceId({});
    QVERIFY(settings.contains("MusicHub/selectedSourceInstanceId"));
    QVERIFY(settings.value("MusicHub/selectedSourceInstanceId").toString().isEmpty());
    SourceScopeStore restored(&settings);
    QVERIFY(restored.selectedSourceInstanceId().isEmpty());
}

// Stale results/failures/finish must neither mutate the model nor count as completion.
void MusicPageModelTest::dropsResultsFromOldGeneration()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    QVERIFY(!model.applyResult(0, sampleResult("unrequested")));
    QVERIFY(!model.finishGeneration(0, 0));
    const quint64 oldGeneration = model.beginRequest();
    const quint64 currentGeneration = model.beginRequest();
    QVERIFY(currentGeneration > oldGeneration);
    QSignalSpy stateChanges(&model, &MusicPageModel::stateChanged);
    QSignalSpy sourceChanges(&model, &MusicPageModel::sourceStatesChanged);
    QSignalSpy errors(&model, &MusicPageModel::errorChanged);
    QVERIFY(!model.applyResult(oldGeneration, sampleResult("old")));
    QVERIFY(!model.applyFailure(oldGeneration, offlineError()));
    QVERIFY(!model.finishGeneration(oldGeneration, 0));
    QCOMPARE(stateChanges.count(), 0);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(errors.count(), 0);
    QVERIFY(model.applyResult(currentGeneration, sampleResult("current")));
    QCOMPARE(model.section(0).titleKey, "current");
    QVERIFY(!model.finishGeneration(currentGeneration, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
}

// Replacing the entire page for one result would erase completed sibling sections.
void MusicPageModelTest::upsertsByStableIdWithoutClearingSiblings()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto generation = model.beginRequest();
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    auto preview = sampleResult("preview");
    preview.complete = false;
    QVERIFY(model.applyResult(generation, preview));
    QVERIFY(model.applyResult(generation, sampleResult("newest", "newest")));
    QVERIFY(model.applyResult(generation, sampleResult("recent-final")));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.section(0).titleKey, "recent-final");
    QCOMPARE(model.section(1).titleKey, "newest");
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QVERIFY(!model.applyResult(generation, sampleResult("late")));
    QVERIFY(!model.applyFailure(generation, offlineError()));
    QCOMPARE(model.section(0).titleKey, "recent-final");
}

// Counting cached/incomplete emissions would make Loading end before network completes.
void MusicPageModelTest::onlyFinalNetworkResponsesCompleteSections()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    const auto generation = model.beginRequest();
    auto cached = sampleResult("cache");
    cached.cached = true;
    QVERIFY(model.applyResult(generation, cached));
    QVERIFY(model.cached());
    QVERIFY(!model.finishGeneration(generation, 2));
    auto partial = sampleResult("partial");
    partial.complete = false;
    QVERIFY(model.applyResult(generation, partial));
    QVERIFY(!model.finishGeneration(generation, 2));
    QVERIFY(model.applyResult(generation, sampleResult("final-recent")));
    QVERIFY(!model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
    QVERIFY(model.applyResult(generation, sampleResult("final-newest", "newest")));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QVERIFY(!model.cached());
}

// Empty final queries still complete; a result with many rows remains one query.
void MusicPageModelTest::emptyResponsesCountQueriesNotRows()
{
    MusicPageModel model(MusicPageKindV2::Search);
    auto generation = model.beginRequest();
    QVERIFY(!model.finishGeneration(generation, -1));
    QVERIFY(model.applyResult(generation, {}));
    QVERIFY(!model.finishGeneration(generation, 2));
    QVERIFY(model.applyResult(generation, {}));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Empty);
    generation = model.beginRequest();
    auto manyRows = sampleResult("one");
    manyRows.sections.append(sampleResult("two", "second").sections.first());
    QVERIFY(model.applyResult(generation, manyRows));
    QVERIFY(!model.finishGeneration(generation, 2));
    QVERIFY(model.applyResult(generation, {}));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    generation = model.beginRequest();
    QVERIFY(model.finishGeneration(generation, 0));
    QCOMPARE(model.state(), PageLoadStateV2::Empty);
    QCOMPARE(model.rowCount(), 0);
}

// Clearing rows on refresh or toggling cached from just the last result loses useful data.
void MusicPageModelTest::keepsCachedSectionsVisibleWhileRefreshing()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    auto generation = model.beginRequest();
    auto cached = sampleResult("cached-recent");
    cached.sections.append(sampleResult("cached-newest", "newest").sections.first());
    cached.cached = true;
    QVERIFY(model.applyResult(generation, cached));
    generation = model.beginRequest();
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
    QVERIFY(model.cached());
    QSignalSpy cachedChanges(&model, &MusicPageModel::cachedChanged);
    QVERIFY(model.applyResult(generation, sampleResult("network-recent")));
    QVERIFY(model.cached());
    QCOMPARE(model.section(1).titleKey, "cached-newest");
    QVERIFY(model.applyResult(generation, sampleResult("network-newest", "newest")));
    QVERIFY(!model.cached());
    QCOMPARE(cachedChanges.count(), 1);
    QVERIFY(model.finishGeneration(generation, 2));
}

// An empty fresh scope must not display rows left over from the previous request.
void MusicPageModelTest::successfulRefreshRemovesPreviousGenerationSections()
{
    MusicPageModel model(MusicPageKindV2::Category);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto generation = model.beginRequest();
    QVERIFY(model.applyResult(generation, sampleResult("old")));
    QVERIFY(model.finishGeneration(generation, 1));
    generation = model.beginRequest();
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.applyResult(generation, {}));
    QVERIFY(model.finishGeneration(generation, 1));
    QCOMPARE(model.state(), PageLoadStateV2::Empty);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.cached());
}

// Last-writer-wins source state would hide a failure when the same source succeeds elsewhere.
void MusicPageModelTest::preservesFailedSourceDetailsAndSuccessfulSiblings()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    const auto generation = model.beginRequest();
    auto first = sampleResult("success");
    SourceErrorV2 error{SourceErrorKindV2::Network, "source.network", "Connection refused",
                        503, true};
    first.sourceStates.insert("failed", {SourcePageLoadStateV2::Failed, error});
    first.sourceStates.insert("working", {SourcePageLoadStateV2::Ready, std::nullopt});
    QVERIFY(model.applyResult(generation, first));
    auto sibling = sampleResult("sibling", "newest");
    sibling.sourceStates.insert("failed", {SourcePageLoadStateV2::Ready, std::nullopt});
    sibling.sourceStates.insert("working", {SourcePageLoadStateV2::Empty, std::nullopt});
    QVERIFY(model.applyResult(generation, sibling));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QCOMPARE(model.rowCount(), 2);
    const auto failed = sourceState(model, "failed");
    QCOMPARE(failed.value("state").toInt(), int(SourcePageLoadStateV2::Failed));
    QCOMPARE(failed.value("error").toMap(), (QVariantMap{
        {"kind", int(SourceErrorKindV2::Network)}, {"messageKey", "source.network"},
        {"detail", "Connection refused"}, {"httpStatus", 503}, {"retryable", true}}));
    QCOMPARE(sourceState(model, "working").value("state").toInt(), int(SourcePageLoadStateV2::Ready));
    QCOMPARE(model.data(model.index(0), MusicPageModel::ErrorRole).toMap().value("failed").toMap(),
             failed.value("error").toMap());
    QCOMPARE(model.error().messageKey, "source.network");
}

// A cached failure is provisional and must not poison a recovered network source.
void MusicPageModelTest::finalNetworkStateReplacesCachedFailure()
{
    MusicPageModel model(MusicPageKindV2::Favorites);
    const auto generation = model.beginRequest();
    auto result = sampleResult("cached");
    result.cached = true;
    result.sourceStates.insert("home", {SourcePageLoadStateV2::Failed, offlineError()});
    QVERIFY(model.applyResult(generation, result));
    result.cached = false;
    result.sourceStates["home"] = {SourcePageLoadStateV2::Ready, std::nullopt};
    QVERIFY(model.applyResult(generation, result));
    QVERIFY(model.finishGeneration(generation, 1));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QCOMPARE(sourceState(model, "home").value("state").toInt(), int(SourcePageLoadStateV2::Ready));
    QVERIFY(sourceState(model, "home").value("error").toMap().isEmpty());
    QVERIFY(model.property("error").toMap().isEmpty());
    QVERIFY(model.data(model.index(0), MusicPageModel::ErrorRole).toMap().isEmpty());
}

// Early applyFailure must keep Loading; selected disabled failures retain typed details for Task7.
void MusicPageModelTest::typedFailureWaitsForAllExpectedSections()
{
    MusicPageModel model(MusicPageKindV2::Favorites);
    auto generation = model.beginRequest();
    QVERIFY(model.applyFailure(generation, offlineError()));
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
    QVERIFY(!model.finishGeneration(generation, 2));
    QVERIFY(model.applyFailure(generation, offlineError()));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Failed);
    const SourceErrorV2 error = model.error();
    QCOMPARE(error.kind, SourceErrorKindV2::Unavailable);
    QCOMPARE(error.messageKey, "source.disabled");
    QCOMPARE(error.detail, "Source instance is disabled");
    QVERIFY(!error.httpStatus.has_value());
    QVERIFY(!error.retryable);
    const auto qmlError = model.property("error").toMap();
    QCOMPARE(qmlError.value("messageKey").toString(), "source.disabled");
    QVERIFY(!qmlError.contains("httpStatus"));
    generation = model.beginRequest();
    QVERIFY(model.property("error").toMap().isEmpty());
    QVERIFY(model.sourceStates().isEmpty());
    QVERIFY(model.applyFailure(generation, offlineError()));
    QVERIFY(model.applyResult(generation, sampleResult("working")));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Ready);
    QCOMPARE(model.error().messageKey, "source.disabled");
}

// All-failed refresh must not label retained cached rows as a successful network load.
void MusicPageModelTest::failedRefreshKeepsCachedItems()
{
    MusicPageModel model(MusicPageKindV2::Search);
    auto generation = model.beginRequest();
    auto result = sampleResult("cached");
    result.cached = true;
    QVERIFY(model.applyResult(generation, result));
    generation = model.beginRequest();
    QVERIFY(model.applyFailure(generation, offlineError()));
    QVERIFY(model.finishGeneration(generation, 1));
    QCOMPARE(model.state(), PageLoadStateV2::Failed);
    QCOMPARE(model.itemAt(0, 0).value("title").toString(), "cached");
    QVERIFY(model.cached());
}

// An empty failed result must not erase cached items; a successful empty result must replace them.
void MusicPageModelTest::emptyFinalResultKeepsCacheOnlyWhenAllSourcesFailed_data()
{
    QTest::addColumn<bool>("allFailed");
    QTest::addColumn<bool>("successfulSibling");
    QTest::newRow("all-sources-failed") << true << false;
    QTest::newRow("successful-empty") << false << false;
    QTest::newRow("failed-section-with-successful-sibling") << true << true;
}

void MusicPageModelTest::emptyFinalResultKeepsCacheOnlyWhenAllSourcesFailed()
{
    QFETCH(bool, allFailed);
    QFETCH(bool, successfulSibling);
    MusicPageModel model(MusicPageKindV2::Recommendation);
    auto generation = model.beginRequest();
    auto snapshot = sampleResult("cached-song");
    snapshot.cached = true;
    QVERIFY(model.applyResult(generation, snapshot));
    generation = model.beginRequest();

    auto finalResult = sampleResult("music.section.recent");
    finalResult.sections.first().items.clear();
    const SourceErrorV2 failure{SourceErrorKindV2::Network, "source.network",
                                "Connection refused", 503, true};
    const SourcePageStateV2 source = allFailed
        ? SourcePageStateV2{SourcePageLoadStateV2::Failed, failure}
        : SourcePageStateV2{SourcePageLoadStateV2::Empty, std::nullopt};
    finalResult.sourceStates.insert("navidrome/home", source);
    finalResult.sourceStates.insert("navidrome/backup", source);
    if (successfulSibling)
        QVERIFY(model.applyResult(generation, sampleResult("fresh-sibling", "newest")));
    QVERIFY(model.applyResult(generation, finalResult));
    QCOMPARE(model.state(), PageLoadStateV2::Loading);
    QVERIFY(model.finishGeneration(generation, successfulSibling ? 2 : 1));
    QCOMPARE(model.rowCount(), successfulSibling ? 2 : 1);
    if (successfulSibling)
        QCOMPARE(model.itemAt(1, 0).value("title").toString(), "fresh-sibling");

    if (allFailed) {
        QCOMPARE(model.state(), successfulSibling ? PageLoadStateV2::Ready : PageLoadStateV2::Failed);
        QCOMPARE(model.error().detail, "Connection refused");
        QCOMPARE(sourceState(model, "navidrome/home").value("error").toMap().value("httpStatus").toInt(), 503);
        QCOMPARE(model.data(model.index(0), MusicPageModel::ErrorRole).toMap()
                     .value("navidrome/home").toMap().value("messageKey").toString(), "source.network");
        QCOMPARE(model.itemAt(0, 0).value("title").toString(), "cached-song");
        QVERIFY(model.cached());
    } else {
        QCOMPARE(model.state(), PageLoadStateV2::Empty);
        QCOMPARE(model.section(0).titleKey, "music.section.recent");
        QVERIFY(model.itemAt(0, 0).isEmpty());
        QVERIFY(!model.cached());
        QVERIFY(model.errorMap().isEmpty());
    }
}

// Flattening identity or defaulting absent actions to available would enable unsupported UI actions.
void MusicPageModelTest::qmlRolesExposeTypedRefsAndOnlySuppliedAvailability()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    auto result = sampleResult("music.section.recent");
    auto &section = result.sections.first();
    section.hasMore = true;
    section.nextCursor = "page-2";
    auto &item = section.items.first();
    item.subtitle = "subtitle";
    item.artists = {"Artist"};
    item.album = "Album";
    item.durationMs = 123456;
    item.artworkId = "cover-1";
    item.externalIds = {{"mbid", "external-1"}};
    item.metadata = {{"year", 2026}};
    item.availableActions.insert(SourceActionV2::Download,
        {AvailabilityV2::Forbidden, "account.download_denied", {{"maxBitrate", 128}}});
    item.availableActions.insert(SourceActionV2::Rating, {AvailabilityV2::Unsupported, {}, {}});
    const auto generation = model.beginRequest();
    QVERIFY(model.applyResult(generation, result));
    const auto roles = model.roleNames();
    QCOMPARE(roles.size(), 7);
    const QMap<QByteArray, QVariant> expected{
        {"sectionId", "recent"}, {"title", "music.section.recent"},
        {"layoutHint", "horizontal"}, {"hasMore", true}, {"loadingMore", false},
        {"error", QVariantMap{}}};
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        QVERIFY(roles.values().contains(it.key()));
        QCOMPARE(model.data(model.index(0), roles.key(it.key())), it.value());
    }
    const auto qmlItem = model.itemAt(0, 0);
    QCOMPARE(qmlItem.value("ref").toMap(), (QVariantMap{
        {"sourcePluginId", "navidrome"}, {"sourceInstanceId", "navidrome/home"},
        {"accountId", "home-account"}, {"entityType", int(MediaEntityTypeV2::Track)},
        {"entityId", "song-1"}}));
    QCOMPARE(qmlItem.value("subtitle").toString(), "subtitle");
    QCOMPARE(qmlItem.value("artists").toStringList(), QStringList{"Artist"});
    QCOMPARE(qmlItem.value("album").toString(), "Album");
    QCOMPARE(qmlItem.value("durationMs").toLongLong(), 123456);
    QCOMPARE(qmlItem.value("artworkId").toString(), "cover-1");
    QCOMPARE(qmlItem.value("externalIds").toMap().value("mbid").toString(), "external-1");
    QCOMPARE(qmlItem.value("metadata").toMap().value("year").toInt(), 2026);
    const auto actions = qmlItem.value("availableActions").toMap();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.value(QString::number(int(SourceActionV2::Download))).toMap(), (QVariantMap{
        {"state", int(AvailabilityV2::Forbidden)}, {"reasonKey", "account.download_denied"},
        {"constraints", QVariantMap{{"maxBitrate", 128}}}}));
    QVERIFY(!actions.contains(QString::number(int(SourceActionV2::Play))));
    QCOMPARE(actions.value(QString::number(int(SourceActionV2::Rating))).toMap().value("state").toInt(),
             int(AvailabilityV2::Unsupported));
    const auto items = model.data(model.index(0), roles.key("items")).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap(), qmlItem);
    QCOMPARE(model.section(0).nextCursor, "page-2");
    QVERIFY(model.itemAt(-1, 0).isEmpty());
    QVERIFY(model.itemAt(0, -1).isEmpty());
    QVERIFY(model.itemAt(0, 1).isEmpty());
    QVERIFY(model.itemAt(1, 0).isEmpty());
    QVERIFY(!model.data({}).isValid());
    QVERIFY(!model.data(model.index(0), -1).isValid());
}

// The QML/JS consumer must receive scalar state and nested maps, not opaque DTOs.
void MusicPageModelTest::qmlReadsPageStateAndNestedItemValues()
{
    QJSEngine engine;
    MusicPageModel model(MusicPageKindV2::Recommendation);
    engine.globalObject().setProperty("page", engine.newQObject(&model));
    auto generation = model.beginRequest();
    QCOMPARE(engine.evaluate("page.state").toInt(), int(PageLoadStateV2::Loading));
    auto result = sampleResult("Song");
    result.sections.first().items.first().availableActions.insert(SourceActionV2::Play,
        {AvailabilityV2::Unavailable, "source.offline", {}});
    QVERIFY(model.applyResult(generation, result));
    QVERIFY(model.finishGeneration(generation, 1));
    const auto ref = engine.evaluate("page.itemAt(0, 0).ref");
    QVERIFY(ref.isObject());
    QCOMPARE(ref.property("sourceInstanceId").toString(), "navidrome/home");
    QCOMPARE(ref.property("entityType").toInt(), int(MediaEntityTypeV2::Track));
    QCOMPARE(engine.evaluate("page.itemAt(0, 0).availableActions[0].state").toInt(),
             int(AvailabilityV2::Unavailable));
    QCOMPARE(engine.evaluate("page.state").toInt(), int(PageLoadStateV2::Ready));
    generation = model.beginRequest();
    QVERIFY(model.applyFailure(generation, offlineError()));
    QVERIFY(model.finishGeneration(generation, 1));
    QCOMPARE(engine.evaluate("page.error.messageKey").toString(), "source.disabled");
    QVERIFY(engine.evaluate("page.error.httpStatus === undefined").toBool());
}

// Failure is determined by the typed state, even when optional error detail is absent.
void MusicPageModelTest::failedSourceWithoutOptionalErrorIsNotEmpty()
{
    MusicPageModel model(MusicPageKindV2::Search);
    const auto generation = model.beginRequest();
    PageResultV2 result;
    result.sourceStates.insert("failed", {SourcePageLoadStateV2::Failed, std::nullopt});
    QVERIFY(model.applyResult(generation, result));
    QVERIFY(!model.finishGeneration(generation, 2));
    QVERIFY(model.applyResult(generation, result));
    QVERIFY(model.finishGeneration(generation, 2));
    QCOMPARE(model.state(), PageLoadStateV2::Failed);
    QCOMPARE(sourceState(model, "failed").value("state").toInt(), int(SourcePageLoadStateV2::Failed));
    QVERIFY(sourceState(model, "failed").value("error").toMap().isEmpty());
}

QTEST_GUILESS_MAIN(MusicPageModelTest)
#include "tst_MusicPageModel.moc"
