#pragma once

#include "LocalLibraryIndex.h"
#include "extensions/content-events/v1/ISourceContentEventsProviderV1.h"
#include "extensions/content-events/v1/SourceContentEventsV1.h"
#include "extensions/legacy-identity/v1/ILegacyMediaIdentityProviderV1.h"
#include "v2/IMusicSourceSessionV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QHash>
#include <QThreadPool>
#include <memory>

class LocalSourceSession final : public IMusicSourceSessionV2,
                                 public IPageProviderV2,
                                 public IPlaybackProviderV2,
                                 public ISettingsActionProviderV2,
                                 public ISourceContentEventsProviderV1,
                                 public ILegacyMediaIdentityProviderV1 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2 ISettingsActionProviderV2
                 ISourceContentEventsProviderV1 ILegacyMediaIdentityProviderV1)
public:
    LocalSourceSession(SourceConfigurationV2 configuration, LocalLibraryIndexPool *pool,
                       QObject *parent = nullptr);
    ~LocalSourceSession() override;
    SourceIdentityV2 identity() const override;
    SourceSessionStateV2 state() const override;
    CapabilitySetV2 capabilities() const override;
    QUuid open() override;
    void close() override;
    void cancel(const QUuid &requestId) override;
    QUuid fetchPage(const PageQueryV2 &query) override;
    QUuid resolveStream(const MediaRefV2 &media) override;
    std::optional<MediaRefV2> claimLegacyFile(const QUrl &fileUrl) const override;
    QUuid fetchArtwork(const MediaRefV2 &media) override;
    QUuid fetchLyrics(const MediaRefV2 &media) override;
    SettingsActionCapabilitiesV2 settingsCapabilities() const override;
    QUuid runSettingsAction(const QString &actionId) override;
    SourceContentEventsV1 *contentEvents() const override;
private:
    enum class RequestKind { Open, Page, Rescan, Stream, Artwork, Lyrics };
    struct Request {
        RequestKind kind;
        PageQueryV2 query;
        MediaRefV2 media;
        quint64 previousRevision = 0;
    };
    QUuid start(Request request);
    void fail(const QUuid &id, SourceErrorV2 error);
    void onSnapshotChanged(quint64 revision);
    void onRefreshFailed(SourceErrorV2 error);
    void finishPage(const QUuid &id);
    void finishResource(const QUuid &id);
    bool validMedia(const MediaRefV2 &media) const;
    QByteArray queryKey(const PageQueryV2 &query) const;
    QByteArray m_configToken;
    SourceConfigurationV2 m_configuration;
    LocalLibraryIndexPool *m_pool = nullptr;
    std::shared_ptr<LocalLibraryIndex> m_index;
    SourceContentEventsV1 *m_events = nullptr;
    SourceSessionStateV2 m_state = SourceSessionStateV2::Closed;
    QHash<QUuid, Request> m_requests;
    QThreadPool m_resources;
};
