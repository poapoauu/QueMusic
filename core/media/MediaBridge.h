#pragma once

#include "MediaListModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUuid>

#include <optional>

class IMusicSourceSession;
class SourceSessionRegistry;
class QJsonValue;
struct SourceError;

class MediaBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(MediaListModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(MediaListModel *browseResults READ browseResults CONSTANT)
    Q_PROPERTY(MediaRequestState requestState READ requestState NOTIFY requestStateChanged)

public:
    explicit MediaBridge(SourceSessionRegistry *registry, QObject *parent = nullptr);

    MediaListModel *searchResults() const;
    MediaListModel *browseResults() const;
    MediaRequestState requestState() const;

    Q_INVOKABLE void search(const QString &sourceScope, const QString &keyword, int limit = 50);
    Q_INVOKABLE void browse(const QString &sourceId, const QString &accountId,
                            const QString &nativeId, int kind, int limit = 50);
    Q_INVOKABLE void open(const QVariantMap &item);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void cancel();

signals:
    void requestStateChanged();

private:
    enum class OperationType {
        Search,
        Browse,
    };

    struct Intent {
        OperationType type;
        MediaId target;
        QString keyword;
        int limit = 50;
        bool append = false;
    };

    struct PendingRequest {
        Intent intent;
        QPointer<MediaListModel> model;
        QPointer<IMusicSourceSession> session;
    };

    static std::optional<MediaId> mediaIdForScope(const QString &sourceScope);
    static std::optional<MediaKind> mediaKindFromNormalized(const QString &kind);
    static MediaError mediaErrorFromSourceError(const SourceError &error);
    static MediaItem mediaItemFromNormalized(const QJsonObject &item, const MediaId &provider);

    MediaListModel *modelFor(OperationType type) const;
    std::optional<Intent> *intentFor(OperationType type);
    const std::optional<Intent> *intentFor(OperationType type) const;
    void dispatch(const Intent &intent);
    void connectSession(IMusicSourceSession *session);
    void cancelOperation(OperationType type);
    void invalidate(const QString &sourceId, const QString &accountId);
    void handleSucceeded(const QUuid &requestId, const QString &operation, const QJsonValue &result);
    void handleFailed(const QUuid &requestId, const SourceError &error);
    void emitRequestStateChangedFor(OperationType type);

    QPointer<SourceSessionRegistry> m_registry;
    MediaListModel *m_searchResults = nullptr;
    MediaListModel *m_browseResults = nullptr;
    std::optional<OperationType> m_lastOperation;
    std::optional<Intent> m_searchIntent;
    std::optional<Intent> m_browseIntent;
    QHash<QUuid, PendingRequest> m_pendingRequests;
    QSet<IMusicSourceSession *> m_connectedSessions;
};
