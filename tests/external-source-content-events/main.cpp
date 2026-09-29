#include <extensions/content-events/v1/ISourceContentEventsProviderV1.h>
#include <extensions/content-events/v1/SourceContentEventsV1.h>
#include <v2/IMusicSourceSessionV2.h>

#include <QSignalSpy>
#include <QTest>

class ExternalSession final : public IMusicSourceSessionV2,
                              public ISourceContentEventsProviderV1 {
    Q_OBJECT
    Q_INTERFACES(ISourceContentEventsProviderV1)
public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;
    SourceIdentityV2 identity() const override { return {}; }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Ready; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override {
        const auto id = QUuid::createUuid();
        emit requestStarted(id);
        emit actionCompleted(id, {});
        return id;
    }
    void close() override {}
    void cancel(const QUuid &) override {}
    SourceContentEventsV1 *contentEvents() const override { return m_events; }
    void publish(quint64 revision) { emit m_events->contentChanged(revision); }
private:
    SourceContentEventsV1 *m_events = new SourceContentEventsV1(this);
};

class ExternalContractTest final : public QObject {
    Q_OBJECT
private slots:
    void optionalInterfaceAcrossInstalledRuntime()
    {
        ExternalSession session;
        auto *provider = qobject_cast<ISourceContentEventsProviderV1 *>(&session);
        QVERIFY(provider);
        QCOMPARE(provider->contentEvents()->parent(), &session);
        QSignalSpy changed(provider->contentEvents(), &SourceContentEventsV1::contentChanged);
        session.publish(42);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().first().toULongLong(), quint64(42));
        QSignalSpy started(&session, &IMusicSourceSessionV2::requestStarted);
        QSignalSpy completed(&session, &IMusicSourceSessionV2::actionCompleted);
        const auto id = session.open();
        QCOMPARE(started.size(), 1);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(started.first().first().toUuid(), id);
    }
};

QTEST_GUILESS_MAIN(ExternalContractTest)
#include "main.moc"
