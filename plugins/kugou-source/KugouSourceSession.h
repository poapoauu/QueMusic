#pragma once
#include "KugouApiClient.h"
#include "v2/IMusicSourceSessionV2.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/item-lookup/v1/IItemLookupProviderV1.h"
#include <QSet>

// Anonymous prototype, only packaged outside the default plugin search root.
class KugouSourceSession final : public IMusicSourceSessionV2,
                                 public IPageProviderV2, public IPlaybackProviderV2,
                                 public IItemLookupProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2 IItemLookupProviderV1)
public:
    explicit KugouSourceSession(SourceConfigurationV2 configuration, QObject *parent = nullptr);
    KugouSourceSession(SourceConfigurationV2 configuration, QNetworkAccessManager *network, QObject *parent);
    ~KugouSourceSession() override;
    SourceIdentityV2 identity() const override;
    SourceSessionStateV2 state() const override { return m_state; }
    CapabilitySetV2 capabilities() const override;
    QUuid open() override;
    void close() override;
    void cancel(const QUuid &id) override;
    QUuid fetchPage(const PageQueryV2 &query) override;
    QUuid resolveStream(const MediaRefV2 &media) override;
    std::optional<MediaItemV2> lookupItem(const MediaRefV2 &ref) const override;
    QUuid fetchArtwork(const MediaRefV2 &media) override;
    QUuid fetchLyrics(const MediaRefV2 &media) override;
private:
    enum class Kind { Open, Page, Stream, Unsupported };
    enum class PageMode { Search, RankCatalog, RankTracks };
    struct Request {
        Kind kind;
        PageQueryV2 query;
        MediaRefV2 media;
        QUuid clientId;
        int page = 1;
        bool needsVerification = false;
        PageMode pageMode = PageMode::Search;
        QString rankId;
    };
    struct Cursor {
        QString search; int limit; int page;
        PageMode mode = PageMode::Search;
        QString rankId;
        QUuid catalogGeneration;
    };
    QUuid enqueue(Request request);
    void dispatch(const QUuid &id);
    void succeeded(const QUuid &clientId, const QJsonObject &response);
    void fail(const QUuid &id, const SourceErrorV2 &error);
    void clearRequests();
    void bindClient();
    bool validConfiguration() const;
    void publishCatalog(const QUuid &id);
    void publishPage(const QUuid &id, PageSectionV2 section);

    SourceConfigurationV2 m_configuration;
    KugouApiClient *m_client;
    SourceSessionStateV2 m_state = SourceSessionStateV2::Closed;
    QHash<QUuid, Request> m_requests;
    QHash<QUuid, QUuid> m_clientIds;
    QHash<QString, Cursor> m_cursors;
    QHash<QString, MediaItemV2> m_tracks;
    QVector<KugouRankProtocol::Entry> m_catalog;
    QUuid m_catalogGeneration;
    QUuid m_openId;
    bool m_closing = false;
};
