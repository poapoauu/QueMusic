#include "KugouQrController.h"
#include <QDateTime>
#include <utility>

KugouQrController::KugouQrController(QString instance, QString account, QObject *parent)
    : KugouQrController(std::move(instance), std::move(account), new QNetworkAccessManager, Timing{}, parent)
{
    // Production network belongs to this attempt manager, not the Host.
    m_network->setParent(m_client);
}

KugouQrController::KugouQrController(QString instance, QString account, QNetworkAccessManager *network,
                                   Timing timing, QObject *parent)
    : QObject(parent), m_network(network), m_client(new KugouApiClient(network, this)),
      m_device(KugouQrProtocol::createDevice(instance, account)), m_timing(timing)
{
    m_poll.setSingleShot(true); m_deadline.setSingleShot(true);
    m_deadline.setTimerType(Qt::PreciseTimer);
    connect(&m_poll, &QTimer::timeout, this, [this] { send(KugouQrRequests::Operation::PollCode); });
    connect(&m_deadline, &QTimer::timeout, this, [this] { if (active()) finish(State::Expired); });
    connect(m_client, &KugouApiClient::succeeded, this, &KugouQrController::received);
    connect(m_client, &KugouApiClient::failed, this, [this](QUuid request, const SourceErrorV2 &error) {
        if (!active() || request != m_request) return;
        if (m_elapsed.elapsed() >= m_timing.totalMs) finish(State::Expired);
        else finish(State::Failed, error.messageKey);
    });
}
KugouQrController::~KugouQrController() { reset(); }
bool KugouQrController::active() const
{
    return m_state == State::Fetching || m_state == State::Waiting || m_state == State::Scanned;
}
void KugouQrController::reset()
{
    m_poll.stop(); m_deadline.stop();
    m_request = {}; m_attempt = {};
    m_client->cancelAll(); m_credentials.reset(); m_code.clear(); m_qrUrl.clear(); m_error.clear();
}
bool KugouQrController::notify(State state, QUuid attempt)
{
    m_state = state;
    QPointer<KugouQrController> guard(this);
    emit stateChanged(state);
    return guard && m_attempt == attempt && m_state == state;
}
QUuid KugouQrController::start()
{
    reset(); m_attempt = QUuid::createUuid();
    const auto attempt = m_attempt;
    if (!m_device || m_timing.pollMs < 1 || m_timing.totalMs < 1 || m_timing.totalMs > 600000) {
        finish(State::Failed, "kugou.qrInvalidConfiguration"); return attempt;
    }
    m_elapsed.start(); m_deadline.start(m_timing.totalMs);
    if (notify(State::Fetching, attempt)) send(KugouQrRequests::Operation::CreateCode);
    return attempt;
}
void KugouQrController::cancel()
{
    if (m_state == State::Cancelled && m_attempt.isNull()) return;
    reset(); notify(State::Cancelled, {});
}
void KugouQrController::finish(State state, const QString &error)
{
    m_poll.stop(); m_deadline.stop();
    m_request = {}; m_client->cancelAll(); m_code.clear(); m_qrUrl.clear(); m_error = error;
    if (state != State::Confirmed) m_credentials.reset();
    notify(state, m_attempt);
}
void KugouQrController::send(KugouQrRequests::Operation operation)
{
    if (!active() || !m_request.isNull()) return;
    if (m_elapsed.elapsed() >= m_timing.totalMs) { finish(State::Expired); return; }
    m_operation = operation;
    m_request = m_client->getQr(operation, m_device->requestIdentity,
                               QDateTime::currentSecsSinceEpoch(), m_code);
}
void KugouQrController::received(QUuid request, const QJsonObject &response)
{
    if (!active() || request != m_request) return;
    m_request = {};
    if (m_elapsed.elapsed() >= m_timing.totalMs) { finish(State::Expired); return; }
    const auto attempt = m_attempt;
    if (m_operation == KugouQrRequests::Operation::CreateCode) {
        const auto code = KugouQrProtocol::mapCode(response);
        if (!code) { finish(State::Failed, "kugou.qrInvalidResponse"); return; }
        m_code = code->key; m_qrUrl = code->landingUrl;
        if (notify(State::Waiting, attempt)) m_poll.start(m_timing.pollMs);
        return;
    }
    const auto poll = KugouQrProtocol::mapPoll(response);
    if (!poll) { finish(State::Failed, "kugou.qrInvalidResponse"); return; }
    using PollState = KugouQrProtocol::PollState;
    if (poll->state == PollState::Expired) { finish(State::Expired); return; }
    if (poll->state == PollState::Confirmed) {
        m_credentials = poll->credentials;
        const QPointer<KugouQrController> guard(this);
        finish(State::Confirmed);
        if (guard && m_attempt == attempt && m_state == State::Confirmed && m_credentials) emit confirmationReady(attempt);
        return;
    }
    const auto state = poll->state == PollState::Scanned ? State::Scanned : State::Waiting;
    if (notify(state, attempt)) m_poll.start(m_timing.pollMs);
}
std::optional<KugouQrProtocol::Credentials> KugouQrController::takeCredentials(const QUuid &attempt)
{
    if (attempt.isNull() || attempt != m_attempt || m_state != State::Confirmed) return std::nullopt;
    return std::exchange(m_credentials, std::nullopt);
}
