#pragma once
#include "SourceRegistry.h"
#include "PlaybackSink.h"
#include "QueueHistoryTypes.h"
#include <memory>

// Owner-thread API. Registry/plugin manager and borrowed sink must outlive calls.
// Public values contain stable references/presentation only, never stream data.
class PlaybackCoordinator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(QVariantMap currentItem READ currentItem NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QUuid currentGeneration READ currentGeneration NOTIFY currentChanged)
    Q_PROPERTY(QUuid currentOccurrence READ currentOccurrence NOTIFY currentChanged)
public:
    explicit PlaybackCoordinator(SourceRegistry *sources, PlaybackSink *sink = nullptr, QObject *parent = nullptr);
    ~PlaybackCoordinator() override;
    // play appends a new occurrence and starts it. enqueue returns its occurrence
    // UUID; play/playQueueEntry return a fresh generation. Null means rejected.
    Q_INVOKABLE QUuid play(const QVariantMap &item);
    Q_INVOKABLE QUuid enqueue(const QVariantMap &item);
    Q_INVOKABLE QUuid playQueueEntry(int index);
    Q_INVOKABLE bool removeOccurrence(const QUuid &id);
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool reportPosition(QUuid generation, qint64 positionMs);
    Q_INVOKABLE bool reportStopped(QUuid generation);
    QVariantList queue() const;
    QVariantMap currentItem() const;
    // Host-only action snapshot. Never invokes plugin code from a property read;
    // resolved rights are refreshed during lifecycle events. Not a QML API.
    QVariantMap currentActionItem() const;
    int currentIndex() const;
    QUuid currentGeneration() const;
    QUuid currentOccurrence() const;
    QList<QueueOccurrence> exportQueue() const;
    bool restoreQueue(const QList<QueueOccurrence> &items);
signals:
    void queueChanged();
    void currentChanged();
    void playbackStarted(QUuid generation);
    void playbackStopped(QUuid generation);
    void playbackFailed(QUuid generation, QString messageKey);
    void scrobbleFinished(QUuid generation, bool submission, bool success);
private:
    struct Entry;
    struct Active;
    struct Pending;
    bool current(const std::shared_ptr<Active> &a) const;
    bool allowed(const std::shared_ptr<Active> &a, SourceActionV2 action);
    bool refreshCurrentActions(const std::shared_ptr<Active> &a);
    void resolve(const std::shared_ptr<Active> &a);
    void scrobble(const std::shared_ptr<Active> &a, qint64 position, bool submission);
    void invoke(const std::shared_ptr<Active> &a, const std::shared_ptr<Pending> &p);
    void settle(const std::shared_ptr<Active> &a, const std::shared_ptr<Pending> &p);
    void end(const std::shared_ptr<Active> &a, const QString &error = {});
    void notifyCurrent();
    QVariantMap publicEntry(const std::shared_ptr<Entry> &entry) const;
    QPointer<SourceRegistry> m_sources;
    QPointer<PlaybackSink> m_sink;
    QList<std::shared_ptr<Entry>> m_queue;
    std::shared_ptr<Active> m_active;
    bool m_destroying = false;
};
