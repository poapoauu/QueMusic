// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "core/playback/AudioSpectrumAnalyzer.h"
#include <QAudioBufferOutput>
#include <QMediaPlayer>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <memory>

// Legacy-only attachment. Core uses the same private analyzer without exposing its player.
class GetWave : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QMediaPlayer *mediaPlayer READ mediaPlayer WRITE setMediaPlayer NOTIFY mediaPlayerChanged)
    Q_PROPERTY(QList<qreal> spectrumData READ spectrumData NOTIFY spectrumChanged)
    Q_PROPERTY(int bands READ bands WRITE setBands NOTIFY bandsChanged)
    Q_PROPERTY(QVector<QPointF> wavePath READ wavePath NOTIFY wavePathChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit GetWave(QObject *parent = nullptr);
    ~GetWave() override;
    QMediaPlayer *mediaPlayer() const { return m_mediaPlayer; }
    void setMediaPlayer(QMediaPlayer *player);
    QList<qreal> spectrumData() const;
    QVector<QPointF> wavePath() const;
    int bands() const { return m_analyzer.bands(); }
    void setBands(int value);
    bool enabled() const { return m_analyzer.enabled(); }
    void setEnabled(bool value);
signals:
    void mediaPlayerChanged();
    void spectrumChanged();
    void bandsChanged();
    void wavePathChanged();
    void enabledChanged();
private:
    AudioSpectrumAnalyzer m_analyzer;
    QPointer<QMediaPlayer> m_mediaPlayer;
    std::unique_ptr<QAudioBufferOutput> m_bufferOutput;
    QMetaObject::Connection m_bufferConnection, m_playerConnection;
    quint64 m_attachmentEpoch = 0;
};
