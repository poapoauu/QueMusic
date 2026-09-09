#pragma once

#include "PlaybackSink.h"

#include <QPointer>

#include <functional>
#include <memory>

class PlaybackCoordinator;
class QtMultimediaBackend;
class QtPlaybackControllerTestAccess;

// Host-private playback facade. Do not register this type with QML until the
// application-composition task; its meta-object deliberately contains no
// stream, backend, metadata, or raw-error surface.
class QtPlaybackController final : public PlaybackSink {
    Q_OBJECT
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(qreal volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
public:
    enum State { Idle, Loading, Playing, Paused, Stopped, Error };
    Q_ENUM(State)

    explicit QtPlaybackController(QObject *parent = nullptr);
    ~QtPlaybackController() override;

    qint64 position() const;
    qint64 duration() const;
    bool playing() const;
    bool seekable() const;
    qreal volume() const;
    qreal playbackRate() const;
    bool muted() const;
    State state() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void setVolume(qreal volume);
    Q_INVOKABLE void setPlaybackRate(qreal rate);
    Q_INVOKABLE void setMuted(bool muted);

    void setCoordinator(PlaybackCoordinator *coordinator);
    bool prepare(StreamDescriptorV2 stream, QUuid generation) override;
    void play(QUuid generation) override;
    void stop(QUuid generation) override;

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void seekableChanged();
    void volumeChanged();
    void playbackRateChanged();
    void mutedChanged();
    void stateChanged();
    void playbackError(QString messageKey);

private:
    enum class BackendPlaybackState { Idle, Playing, Paused, Stopped };
    enum class BackendMediaStatus { NoMedia, Loading, Loaded, EndOfMedia, InvalidMedia };
    struct BackendCallbacks {
        std::function<void(qint64)> position;
        std::function<void(qint64)> duration;
        std::function<void(bool)> seekable;
        std::function<void(BackendPlaybackState)> playbackState;
        std::function<void(BackendMediaStatus)> mediaStatus;
        std::function<void()> error;
    };
    class Backend {
    public:
        virtual ~Backend() = default;
        virtual void setCallbacks(BackendCallbacks callbacks) = 0;
        virtual void clearCallbacks() = 0;
        virtual void setSource(const QUrl &source) = 0;
        virtual void play() = 0;
        virtual void pause() = 0;
        virtual void stop() = 0;
        virtual void setPosition(qint64 position) = 0;
        virtual void setVolume(qreal volume) = 0;
        virtual void setPlaybackRate(qreal rate) = 0;
        virtual void setMuted(bool muted) = 0;
    };
    using BackendFactory = std::function<std::unique_ptr<Backend>()>;
    struct Reporter {
        std::function<void(QUuid, qint64)> position;
        std::function<void(QUuid)> stopped;
    };

    QtPlaybackController(BackendFactory factory, Reporter reporter, QObject *parent = nullptr);
    bool ownerThread() const;
    bool validStream(const StreamDescriptorV2 &stream, const QUuid &generation) const;
    void retireBackend();
    void bindCallbacks(Backend *identity, QUuid generation);
    void setPositionValue(qint64 value, QUuid generation, Backend *identity);
    void setDurationValue(qint64 value);
    void setSeekableValue(bool value);
    void setPlayingValue(bool value);
    void setStateValue(State value);
    void reportTerminal(QUuid generation, bool error);

    BackendFactory m_factory;
    Reporter m_reporter;
    QPointer<PlaybackCoordinator> m_coordinator;
    std::unique_ptr<Backend> m_backend;
    QUuid m_generation;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qreal m_volume = 1.0;
    qreal m_playbackRate = 1.0;
    bool m_playing = false;
    bool m_seekable = false;
    bool m_descriptorSeekable = false;
    bool m_muted = false;
    bool m_terminalEligible = false;
    bool m_terminalReported = false;
    bool m_occurrenceClosed = false;
    bool m_errorEmitted = false;
    bool m_destroying = false;
    State m_state = Idle;

    friend class QtPlaybackControllerTestAccess;
    friend class QtMultimediaBackend;
};
