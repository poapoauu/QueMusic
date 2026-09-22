#include "QtPlaybackController.h"

#include "PlaybackCoordinator.h"

#include <QAudioOutput>
#include <QDir>
#include <QMediaPlayer>
#include <QThread>

#include <cmath>
#include <utility>

class QtMultimediaBackend final : public QtPlaybackController::Backend {
public:
    QtMultimediaBackend()
        : m_audio(std::make_unique<QAudioOutput>()),
          m_player(std::make_unique<QMediaPlayer>())
    {
        m_player->setAudioOutput(m_audio.get());
    }

    ~QtMultimediaBackend() override
    {
        clearCallbacks();
        m_player.reset();
        m_audio.reset();
    }

    void setCallbacks(QtPlaybackController::BackendCallbacks callbacks) override
    {
        clearCallbacks();
        m_callbacks = std::move(callbacks);
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::positionChanged,
                                           [this](qint64 value) { if (m_callbacks.position) m_callbacks.position(value); });
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::durationChanged,
                                           [this](qint64 value) { if (m_callbacks.duration) m_callbacks.duration(value); });
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::seekableChanged,
                                           [this](bool value) { if (m_callbacks.seekable) m_callbacks.seekable(value); });
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::playbackStateChanged,
                                           [this](QMediaPlayer::PlaybackState state) {
            if (!m_callbacks.playbackState) return;
            using Public = QtPlaybackController::BackendPlaybackState;
            switch (state) {
            case QMediaPlayer::PlayingState: m_callbacks.playbackState(Public::Playing); break;
            case QMediaPlayer::PausedState: m_callbacks.playbackState(Public::Paused); break;
            case QMediaPlayer::StoppedState: m_callbacks.playbackState(Public::Stopped); break;
            }
        });
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::mediaStatusChanged,
                                           [this](QMediaPlayer::MediaStatus status) {
            if (!m_callbacks.mediaStatus) return;
            using Public = QtPlaybackController::BackendMediaStatus;
            switch (status) {
            case QMediaPlayer::NoMedia: m_callbacks.mediaStatus(Public::NoMedia); break;
            case QMediaPlayer::LoadingMedia:
            case QMediaPlayer::StalledMedia:
            case QMediaPlayer::BufferingMedia: m_callbacks.mediaStatus(Public::Loading); break;
            case QMediaPlayer::LoadedMedia:
            case QMediaPlayer::BufferedMedia: m_callbacks.mediaStatus(Public::Loaded); break;
            case QMediaPlayer::EndOfMedia: m_callbacks.mediaStatus(Public::EndOfMedia); break;
            case QMediaPlayer::InvalidMedia: m_callbacks.mediaStatus(Public::InvalidMedia); break;
            }
        });
        m_connections << QObject::connect(m_player.get(), &QMediaPlayer::errorOccurred,
                                           [this](QMediaPlayer::Error, const QString &) {
            if (m_callbacks.error) m_callbacks.error();
        });
    }

    void clearCallbacks() override
    {
        for (const auto &connection : std::exchange(m_connections, {}))
            QObject::disconnect(connection);
        m_callbacks = {};
    }
    void setSource(const QUrl &source) override { m_player->setSource(source); }
    void play() override { m_player->play(); }
    void pause() override { m_player->pause(); }
    void stop() override { m_player->stop(); }
    void setPosition(qint64 position) override { m_player->setPosition(position); }
    void setVolume(qreal volume) override { m_audio->setVolume(float(volume)); }
    void setPlaybackRate(qreal rate) override { m_player->setPlaybackRate(rate); }
    void setMuted(bool muted) override { m_audio->setMuted(muted); }

private:
    std::unique_ptr<QAudioOutput> m_audio;
    std::unique_ptr<QMediaPlayer> m_player;
    QtPlaybackController::BackendCallbacks m_callbacks;
    QList<QMetaObject::Connection> m_connections;
};

QtPlaybackController::QtPlaybackController(QObject *parent)
    : QtPlaybackController([] { return std::make_unique<QtMultimediaBackend>(); }, {}, parent)
{
}

QtPlaybackController::QtPlaybackController(BackendFactory factory, Reporter reporter, QObject *parent)
    : PlaybackSink(parent), m_factory(std::move(factory)), m_reporter(std::move(reporter))
{
}

QtPlaybackController::~QtPlaybackController()
{
    m_destroying = true;
    retireBackend();
}

qint64 QtPlaybackController::position() const { return m_position; }
qint64 QtPlaybackController::duration() const { return m_duration; }
bool QtPlaybackController::playing() const { return m_playing; }
bool QtPlaybackController::seekable() const { return m_seekable; }
qreal QtPlaybackController::volume() const { return m_volume; }
qreal QtPlaybackController::playbackRate() const { return m_playbackRate; }
bool QtPlaybackController::muted() const { return m_muted; }
QtPlaybackController::State QtPlaybackController::state() const { return m_state; }

bool QtPlaybackController::ownerThread() const
{
    return QThread::currentThread() == thread();
}

bool QtPlaybackController::validStream(const StreamDescriptorV2 &stream, const QUuid &generation) const
{
    if (generation.isNull() || !stream.headers.isEmpty() || stream.url.isEmpty()
        || !stream.url.isValid() || stream.url.isRelative()
        || (stream.expiresAt.isValid() && stream.expiresAt <= QDateTime::currentDateTimeUtc()))
        return false;
    const auto scheme = stream.url.scheme().toLower();
    if (scheme == QStringLiteral("file"))
        return stream.url.host().isEmpty() && stream.url.userInfo().isEmpty()
            && !stream.url.hasQuery() && !stream.url.hasFragment()
            && QDir::isAbsolutePath(stream.url.toLocalFile());
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        && !stream.url.host().isEmpty();
}

bool QtPlaybackController::prepare(StreamDescriptorV2 stream, QUuid generation)
{
    if (!ownerThread() || !validStream(stream, generation) || !m_factory)
        return false;
    auto replacement = m_factory();
    if (!replacement)
        return false;

    retireBackend();
    m_backend = std::move(replacement);
    m_generation = generation;
    m_descriptorSeekable = stream.seekable;
    m_terminalEligible = false;
    m_terminalReported = false;
    m_occurrenceClosed = false;
    m_errorEmitted = false;
    if (m_position != 0) {
        m_position = 0;
        emit positionChanged();
    }
    setDurationValue(0);
    setSeekableValue(false);
    setPlayingValue(false);
    setStateValue(Loading);
    auto *identity = m_backend.get();
    bindCallbacks(identity, generation);
    m_backend->setVolume(m_volume);
    m_backend->setPlaybackRate(m_playbackRate);
    m_backend->setMuted(m_muted);
    m_backend->setSource(stream.url);
    return true;
}

void QtPlaybackController::retireBackend()
{
    if (!m_backend)
        return;
    m_backend->clearCallbacks();
    m_backend->stop();
    m_backend.reset();
}

void QtPlaybackController::bindCallbacks(Backend *identity, QUuid generation)
{
    BackendCallbacks callbacks;
    callbacks.position = [this, identity, generation](qint64 value) {
        setPositionValue(value, generation, identity);
    };
    callbacks.duration = [this, identity, generation](qint64 value) {
        if (!m_occurrenceClosed && m_backend.get() == identity && m_generation == generation)
            setDurationValue(value);
    };
    callbacks.seekable = [this, identity, generation](bool value) {
        if (!m_occurrenceClosed && m_backend.get() == identity && m_generation == generation)
            setSeekableValue(m_descriptorSeekable && value);
    };
    callbacks.playbackState = [this, identity, generation](BackendPlaybackState value) {
        if (m_occurrenceClosed || m_backend.get() != identity || m_generation != generation) return;
        switch (value) {
        case BackendPlaybackState::Idle: setPlayingValue(false); setStateValue(Idle); break;
        case BackendPlaybackState::Playing: setPlayingValue(true); setStateValue(Playing); break;
        case BackendPlaybackState::Paused: setPlayingValue(false); setStateValue(Paused); break;
        case BackendPlaybackState::Stopped:
            setPlayingValue(false); setStateValue(Stopped);
            if (m_terminalEligible) reportTerminal(generation, false);
            break;
        }
    };
    callbacks.mediaStatus = [this, identity, generation](BackendMediaStatus value) {
        if (m_occurrenceClosed || m_backend.get() != identity || m_generation != generation) return;
        switch (value) {
        case BackendMediaStatus::NoMedia: setStateValue(Idle); break;
        case BackendMediaStatus::Loading: setStateValue(Loading); break;
        case BackendMediaStatus::Loaded:
            if (m_state == Loading) setStateValue(Idle);
            break;
        case BackendMediaStatus::EndOfMedia:
            setPlayingValue(false); setStateValue(Stopped); reportTerminal(generation, false); break;
        case BackendMediaStatus::InvalidMedia:
            setPlayingValue(false); setStateValue(Error); reportTerminal(generation, true); break;
        }
    };
    callbacks.error = [this, identity, generation] {
        if (m_occurrenceClosed || m_backend.get() != identity || m_generation != generation) return;
        setPlayingValue(false); setStateValue(Error); reportTerminal(generation, true);
    };
    m_backend->setCallbacks(std::move(callbacks));
}

void QtPlaybackController::setCoordinator(PlaybackCoordinator *coordinator)
{
    if (!ownerThread()) return;
    m_coordinator = coordinator;
    m_reporter.position = [guard = QPointer<PlaybackCoordinator>(coordinator)](QUuid generation, qint64 value) {
        if (guard) guard->reportPosition(generation, value);
    };
    m_reporter.stopped = [guard = QPointer<PlaybackCoordinator>(coordinator)](QUuid generation) {
        if (guard) guard->reportStopped(generation);
    };
}

void QtPlaybackController::play(QUuid generation)
{
    if (!ownerThread() || !m_backend || m_occurrenceClosed
        || generation.isNull() || generation != m_generation) return;
    m_terminalEligible = true;
    m_backend->play();
}

void QtPlaybackController::stop(QUuid generation)
{
    if (!ownerThread() || !m_backend || m_occurrenceClosed
        || generation.isNull() || generation != m_generation) return;
    m_terminalEligible = true;
    m_backend->stop();
    setPlayingValue(false);
    setStateValue(Stopped);
    reportTerminal(generation, false);
}

void QtPlaybackController::play()
{
    play(m_generation);
}

void QtPlaybackController::pause()
{
    if (!ownerThread() || !m_backend || m_occurrenceClosed || m_generation.isNull()) return;
    m_backend->pause();
    setPlayingValue(false);
    setStateValue(Paused);
}

void QtPlaybackController::stop()
{
    stop(m_generation);
}

void QtPlaybackController::seek(qint64 value)
{
    if (!ownerThread() || !m_backend || m_occurrenceClosed || !m_seekable || value < 0
        || (m_duration > 0 && value > m_duration)) return;
    m_backend->setPosition(value);
}

void QtPlaybackController::setVolume(qreal value)
{
    if (!ownerThread() || !std::isfinite(value)) return;
    value = qBound(qreal(0), value, qreal(1));
    if (m_volume == value) return;
    m_volume = value;
    if (m_backend) m_backend->setVolume(value);
    emit volumeChanged();
}

void QtPlaybackController::setPlaybackRate(qreal value)
{
    if (!ownerThread() || !std::isfinite(value) || value <= 0) return;
    if (m_playbackRate == value) return;
    m_playbackRate = value;
    if (m_backend) m_backend->setPlaybackRate(value);
    emit playbackRateChanged();
}

void QtPlaybackController::setMuted(bool value)
{
    if (!ownerThread() || m_muted == value) return;
    m_muted = value;
    if (m_backend) m_backend->setMuted(value);
    emit mutedChanged();
}

void QtPlaybackController::setPositionValue(qint64 value, QUuid generation, Backend *identity)
{
    if (m_occurrenceClosed || m_backend.get() != identity
        || m_generation != generation || value < 0) return;
    if (m_position != value) { m_position = value; emit positionChanged(); }
    if (!m_destroying && m_reporter.position) m_reporter.position(generation, value);
}

void QtPlaybackController::setDurationValue(qint64 value)
{
    value = qMax<qint64>(0, value);
    if (m_duration == value) return;
    m_duration = value;
    emit durationChanged();
}

void QtPlaybackController::setSeekableValue(bool value)
{
    if (m_seekable == value) return;
    m_seekable = value;
    emit seekableChanged();
}

void QtPlaybackController::setPlayingValue(bool value)
{
    if (m_playing == value) return;
    m_playing = value;
    emit playingChanged();
}

void QtPlaybackController::setStateValue(State value)
{
    if (m_state == value) return;
    m_state = value;
    emit stateChanged();
}

void QtPlaybackController::reportTerminal(QUuid generation, bool error)
{
    if (m_terminalReported || generation.isNull() || generation != m_generation) return;
    m_terminalReported = true;
    m_occurrenceClosed = true;
    if (error && !m_errorEmitted) {
        m_errorEmitted = true;
        emit playbackError(QStringLiteral("music.playbackBackendError"));
    }
    if (!m_destroying && m_reporter.stopped) m_reporter.stopped(generation);
}
