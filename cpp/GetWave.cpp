// SPDX-License-Identifier: Apache-2.0
#include "GetWave.h"

GetWave::GetWave(QObject *parent) : QObject(parent)
{
    m_analyzer.setBands(96); m_analyzer.setEnabled(true);
    connect(&m_analyzer, &AudioSpectrumAnalyzer::frameChanged, this, [this] {
        const QPointer<GetWave> guard(this);
        emit spectrumChanged();
        if (guard) emit wavePathChanged();
    });
}
GetWave::~GetWave()
{
    disconnect(m_bufferConnection); disconnect(m_playerConnection);
    if (m_mediaPlayer && m_mediaPlayer->audioBufferOutput() == m_bufferOutput.get())
        m_mediaPlayer->setAudioBufferOutput(nullptr);
}
QList<qreal> GetWave::spectrumData() const { return m_analyzer.frame().spectrum; }
QVector<QPointF> GetWave::wavePath() const { return m_analyzer.frame().path; }
void GetWave::setBands(int value)
{
    const int previous = bands(); const QPointer<GetWave> guard(this);
    m_analyzer.setBands(value);
    if (guard && previous != bands()) emit bandsChanged();
}
void GetWave::setEnabled(bool value)
{
    if (enabled() == value) return;
    const QPointer<GetWave> guard(this); m_analyzer.setEnabled(value);
    if (guard) emit enabledChanged();
}
void GetWave::setMediaPlayer(QMediaPlayer *player)
{
    if (m_mediaPlayer == player) return;
    const auto epoch = ++m_attachmentEpoch;
    const QPointer<GetWave> guard(this);
    const QPointer<QMediaPlayer> next(player);
    disconnect(m_bufferConnection); disconnect(m_playerConnection);
    const auto previous = m_mediaPlayer; m_mediaPlayer = nullptr;
    if (previous && previous->audioBufferOutput() == m_bufferOutput.get()) previous->setAudioBufferOutput(nullptr);
    if (!guard || epoch != m_attachmentEpoch) return;
    m_bufferOutput.reset();
    m_analyzer.reset(); if (!guard || epoch != m_attachmentEpoch) return;
    m_mediaPlayer = next;
    if (next) {
        m_bufferOutput = std::make_unique<QAudioBufferOutput>();
        const QPointer<QAudioBufferOutput> output(m_bufferOutput.get());
        m_bufferConnection = connect(output, &QAudioBufferOutput::audioBufferReceived, this,
            [this, output, epoch](const QAudioBuffer &buffer) {
                if (epoch == m_attachmentEpoch && output && m_bufferOutput.get() == output && m_mediaPlayer) m_analyzer.consume(buffer);
            });
        m_playerConnection = connect(next, &QObject::destroyed, this, [this] {
            m_mediaPlayer = nullptr;
            const QPointer<GetWave> guard(this); m_analyzer.reset();
            if (guard) emit mediaPlayerChanged();
        });
        next->setAudioBufferOutput(output);
        if (!guard || epoch != m_attachmentEpoch) return;
    }
    emit mediaPlayerChanged();
}
