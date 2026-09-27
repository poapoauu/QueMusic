#include "v2/IMusicSourceSessionV2.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include "extensions/content-events/v1/ISourceContentEventsProviderV1.h"
#include "extensions/content-events/v1/SourceContentEventsV1.h"

class LegacyContentSession : public IMusicSourceSessionV2 {
    Q_OBJECT
public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;
    SourceIdentityV2 identity() const override { return {"fixture", "fixture/home", "home", "Home"}; }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Ready; }
    CapabilitySetV2 capabilities() const override { return {}; }
    QUuid open() override { return {}; }
    void close() override {}
    void cancel(const QUuid &) override {}
};

class ExtendedContentSession final : public LegacyContentSession,
                                     public ISourceContentEventsProviderV1 {
    Q_OBJECT
    Q_INTERFACES(ISourceContentEventsProviderV1)
public:
    explicit ExtendedContentSession(QObject *parent = nullptr)
        : LegacyContentSession(parent), m_events(new SourceContentEventsV1(this)) {}
    SourceContentEventsV1 *contentEvents() const override { return m_events; }
private:
    SourceContentEventsV1 *m_events;
};

class SourceContentEventsContractTest final : public QObject {
    Q_OBJECT
private slots:
    void optionalInterfaceRoundTrip()
    {
        LegacyContentSession legacy;
        ExtendedContentSession extended;
        auto *provider = qobject_cast<ISourceContentEventsProviderV1 *>(&extended);
        QVERIFY(provider);
        QVERIFY(!qobject_cast<ISourceContentEventsProviderV1 *>(&legacy));
        // Host discovery is by the published IID, not concrete plugin type.
        QCOMPARE(extended.qt_metacast("org.quemusic.source.ContentEventsProvider/1.0"),
                 static_cast<void *>(provider));
        QCOMPARE(provider->contentEvents()->parent(), &extended);
        QSignalSpy changed(provider->contentEvents(), &SourceContentEventsV1::contentChanged);
        QVERIFY(changed.isValid());
        emit provider->contentEvents()->contentChanged(1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toULongLong(), quint64(1));
    }

    void queuedErrorPreservesV2Payload()
    {
        ExtendedContentSession extended;
        QObject consumer;
        SourceErrorV2 received;
        int calls = 0;
        connect(extended.contentEvents(), &SourceContentEventsV1::refreshFailed, &consumer,
                [&](SourceErrorV2 error) { received = error; ++calls; }, Qt::QueuedConnection);
        SourceErrorV2 sent{SourceErrorKindV2::Unavailable, QStringLiteral("fixture.refresh.failed"),
                           QStringLiteral("fixture detail"), 503, false};
        emit extended.contentEvents()->refreshFailed(sent);
        QCOMPARE(calls, 0);
        QTRY_COMPARE(calls, 1);
        QCOMPARE(received.kind, SourceErrorKindV2::Unavailable);
        QCOMPARE(received.messageKey, QStringLiteral("fixture.refresh.failed"));
        QCOMPARE(received.detail, QStringLiteral("fixture detail"));
        QVERIFY(received.httpStatus.has_value());
        QCOMPARE(*received.httpStatus, 503);
        QVERIFY(!received.retryable);
    }

    void eventsDieWithOwningSession()
    {
        QPointer<SourceContentEventsV1> events;
        {
            ExtendedContentSession extended;
            events = extended.contentEvents();
            QVERIFY(events);
        }
        QVERIFY(events.isNull());
    }
};

QTEST_GUILESS_MAIN(SourceContentEventsContractTest)
#include "tst_SourceContentEventsContract.moc"
