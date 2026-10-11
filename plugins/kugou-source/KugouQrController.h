#pragma once
#include "KugouApiClient.h"
#include "KugouQrProtocol.h"
#include <QElapsedTimer>
#include <QTimer>

// Plugin-private authentication attempt, not a Source login grant or UI API.
class KugouQrController final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Fetching, Waiting, Scanned, Confirmed, Expired, Cancelled, Failed };
    Q_ENUM(State)
    struct Timing { int pollMs = 2000; int totalMs = 120000; };
    KugouQrController(QString instance, QString account, QObject *parent = nullptr);
    KugouQrController(QString instance, QString account, QNetworkAccessManager *network,
                      Timing timing, QObject *parent = nullptr);
    ~KugouQrController() override;
    QUuid start();
    void cancel();
    State state() const { return m_state; }
    QUuid attemptId() const { return m_attempt; }
    QUrl qrUrl() const { return m_qrUrl; }
    QString errorKey() const { return m_error; }
    // One-shot, exact attempt only. Never expose this method/struct to QML.
    std::optional<KugouQrProtocol::Credentials> takeCredentials(const QUuid &attempt);
signals:
    void stateChanged(KugouQrController::State state);
    void confirmationReady(QUuid attemptId);
private:
    bool active() const;
    void reset();
    void send(KugouQrRequests::Operation operation);
    void received(QUuid request, const QJsonObject &response);
    void finish(State state, const QString &error = {});
    bool notify(State state, QUuid attempt);
    QPointer<QNetworkAccessManager> m_network; // borrowed except in the production constructor
    KugouApiClient *m_client;
    std::optional<KugouQrProtocol::Device> m_device;
    Timing m_timing;
    QTimer m_poll, m_deadline;
    QElapsedTimer m_elapsed;
    QUuid m_attempt, m_request;
    KugouQrRequests::Operation m_operation = KugouQrRequests::Operation::CreateCode;
    State m_state = State::Idle;
    QString m_code, m_error;
    QUrl m_qrUrl;
    std::optional<KugouQrProtocol::Credentials> m_credentials;
};
