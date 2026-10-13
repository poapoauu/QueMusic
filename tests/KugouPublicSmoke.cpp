// Explicitly opt-in public protocol probe. No settings, secrets, plugin loader
// or audio sink: uses the production anonymous Session and TLS policy.
#include "KugouSourceSession.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

namespace {
class Probe final : public QObject {
public:
    explicit Probe(QCoreApplication &app, bool artists)
        : m_app(app), m_artists(artists), m_session({"org.quemusic.source.kugou", "kugou", "kugou/public-smoke",
                                "public-smoke", "Anonymous protocol probe", {}, {}}, this)
    {
        connect(&m_session, &IMusicSourceSessionV2::stateChanged, this, [this](auto state) {
            if (state == SourceSessionStateV2::Ready && m_stage == "open") {
                m_query.page = MusicPageKindV2::Category;
                m_query.section = m_artists ? PageSectionKindV2::Artists : PageSectionKindV2::Playlists;
                m_query.scope.sourceInstanceId = "kugou/public-smoke";
                m_query.limit = 2;
                fetch("catalog");
            }
        });
        connect(&m_session, &IMusicSourceSessionV2::requestFailed, this, [this](auto id, auto error) {
            if (id == m_pending) finish(false, error.messageKey, error.httpStatus);
        });
        connect(&m_session, &IMusicSourceSessionV2::pageReady, this,
                [this](auto id, const PageResultV2 &result) { if (id == m_pending) page(result); });
        m_deadline.setSingleShot(true); m_deadline.setTimerType(Qt::PreciseTimer);
        connect(&m_deadline, &QTimer::timeout, this, [this] { finish(false, "smoke.timeout"); });
    }
    void start() { m_elapsed.start(); m_deadline.start(45000); m_pending = m_session.open(); }
private:
    void fetch(const QString &stage)
    {
        m_stage = stage;
        m_pending = m_session.fetchPage(m_query);
    }
    void page(const PageResultV2 &result)
    {
        if (m_elapsed.hasExpired(45000)) { finish(false, "smoke.timeout"); return; }
        const bool catalog = m_stage == "catalog" || m_stage == "catalogNext";
        const auto catalogKind = m_artists ? PageSectionKindV2::Artists : PageSectionKindV2::Playlists;
        const auto catalogEntity = m_artists ? MediaEntityTypeV2::Artist : MediaEntityTypeV2::Playlist;
        const auto state = result.sourceStates.constFind("kugou/public-smoke");
        if (result.sections.size() != 1 || state == result.sourceStates.cend()
            || result.sections[0].kind != (catalog ? catalogKind : PageSectionKindV2::Tracks)
            || result.sections[0].items.size() > 2
            || (state->state != SourcePageLoadStateV2::Ready && state->state != SourcePageLoadStateV2::Empty)) {
            finish(false, "smoke.invalidPage"); return;
        }
        const auto &section = result.sections[0];
        int playable = 0;
        for (const auto &item : section.items) {
            const auto &ref = item.ref;
            if (ref.sourcePluginId != "kugou" || ref.sourceInstanceId != "kugou/public-smoke"
                || ref.accountId != "public-smoke" || !item.metadata.isEmpty() || !item.externalIds.isEmpty()
                || !item.artworkId.isEmpty() || ref.entityType != (catalog ? catalogEntity : MediaEntityTypeV2::Track)) {
                finish(false, "smoke.invalidIdentity"); return;
            }
            if (item.availableActions.value(SourceActionV2::Play).state == AvailabilityV2::Available) ++playable;
        }
        m_results.append(QJsonObject{{"stage", m_stage}, {"items", section.items.size()},
            {"hasMore", section.hasMore}, {"playCandidates", playable}});
        if (section.hasMore && section.nextCursor.isEmpty()) { finish(false, "smoke.invalidCursor"); return; }
        if (m_stage == "catalog") {
            if (section.items.isEmpty()) { finish(false, "smoke.emptyCatalog"); return; }
            const auto ref = section.items[0].ref;
            m_collection = ref.entityId;
            if (m_artists) {
                auto *browse = qobject_cast<IArtistBrowseProviderV1 *>(&m_session);
                if (!browse || browse->artistBrowseTarget(ref) != ArtistBrowseTargetV1::Tracks) {
                    finish(false, "smoke.invalidArtistBrowse"); return;
                }
            } else if (!m_collection.startsWith("rank/")
                       || KugouRankProtocol::rankId(QJsonValue(m_collection.mid(5))) != m_collection.mid(5)) {
                finish(false, "smoke.invalidRank"); return;
            }
            if (section.hasMore) { m_query.cursor = section.nextCursor; fetch("catalogNext"); return; }
        }
        if (catalog) {
            m_query.section = PageSectionKindV2::Tracks;
            m_query.filters = {{m_artists ? QStringLiteral("artistId") : QStringLiteral("playlistId"), m_collection}};
            m_query.cursor.clear(); fetch("tracks"); return;
        }
        if (m_stage == "tracks" && section.hasMore) {
            m_query.cursor = section.nextCursor; fetch("tracksNext"); return;
        }
        // Pass means only these sampled anonymous data pages passed. No token
        // verification, entitlement, resource resolution or actual audio.
        finish(true, {});
    }
    void finish(bool success, QString key, std::optional<int> status = {})
    {
        if (m_finished) return;
        if (success && m_elapsed.hasExpired(45000)) { success = false; key = "smoke.timeout"; }
        m_finished = true; m_deadline.stop(); m_session.close();
        static const QRegularExpression safeKey(QStringLiteral("\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z"));
        if (!key.isEmpty() && !safeKey.match(key).hasMatch()) key = "smoke.requestFailed";
        QJsonObject report{{"schemaVersion", 1}, {"outcome", success ? "passed" : "failed"},
            {"probe", m_artists ? "artists" : "ranks"},
            {"stage", m_stage}, {"errorKey", key}, {"pages", m_results},
            {"authenticated", false}, {"audioTested", false}};
        if (status) report.insert("httpStatus", *status);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
        m_app.exit(success ? 0 : 1);
    }
    QCoreApplication &m_app;
    const bool m_artists;
    KugouSourceSession m_session;
    QTimer m_deadline;
    QElapsedTimer m_elapsed;
    QUuid m_pending;
    PageQueryV2 m_query;
    QString m_stage = "open", m_collection;
    QJsonArray m_results;
    bool m_finished = false;
};
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments().mid(1);
    if (arguments == QStringList{"--help"}) {
        QTextStream(stdout) << "Usage: quemusic_kugou_public_smoke --live-public | --live-public-artists\n"
            "Explicit anonymous HTTPS rank OR artist page probe; no secrets, login or audio.\n";
        return 0;
    }
    const bool artists = arguments == QStringList{"--live-public-artists"};
    if (!artists && arguments != QStringList{"--live-public"}) {
        QTextStream(stderr) << "One explicit --live-public or --live-public-artists required; no network request sent.\n";
        return 2;
    }
    // Qt diagnostic strings may contain URLs/system paths. Reports below are
    // intentionally fixed summaries, never raw bodies, names, hashes or errors.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    Probe probe(app, artists); QTimer::singleShot(0, &probe, [&probe] { probe.start(); });
    return app.exec();
}
