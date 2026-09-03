#pragma once
#include "ArtworkCache.h"
#include "SourceRegistry.h"
#include <memory>

// Task10 wire contract: action/subject must match the request exactly. Artwork
// supplies QByteArray bytes + QString mimeType, lyrics supplies QString lyrics.
// No host HTTP/auth fallback, no URL/header payload accepted as artwork bytes.
namespace MediaAssetPayloadV2 {
inline constexpr const char *Bytes = "bytes";
inline constexpr const char *MimeType = "mimeType";
inline constexpr const char *Lyrics = "lyrics";
struct ArtworkValue { QByteArray bytes; QString mimeType; };
struct LyricsValue { QString lyrics; };
}

class MediaAssetRepository final : public QObject {
    Q_OBJECT
public:
    MediaAssetRepository(SourceRegistry *sources, ArtworkCache *artwork,
                         QObject *parent = nullptr);
    ~MediaAssetRepository() override;
    QUuid requestArtwork(const MediaRefV2 &media);
    QUuid requestLyrics(const MediaRefV2 &media);
    void cancel(const QUuid &requestId);
signals:
    void artworkReady(QUuid requestId, MediaRefV2 media, QUrl localUrl);
    void lyricsReady(QUuid requestId, MediaRefV2 media, QString lyrics);
    void failed(QUuid requestId, SourceErrorV2 error);
private:
    struct Request;
    QUuid request(const MediaRefV2 &media, SourceActionV2 action);
    void begin(const QUuid &id);
    void dispatch(const QUuid &id);
    void complete(const QUuid &id, const ActionResultV2 &result);
    void fail(const QUuid &id, SourceErrorKindV2 kind);
    void sourceChanged(const QString &source);
    QPointer<SourceRegistry> m_sources;
    std::shared_ptr<ArtworkCache> m_artwork;
    QHash<QUuid,std::shared_ptr<Request>> m_requests;
    QHash<QString,QHash<QString,QString>> m_lyrics;
};
