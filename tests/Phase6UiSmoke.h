#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class QQmlApplicationEngine;
class QQuickWindow;

// Test-build-only driver of the real application, not an alternate UI shell.
class Phase6UiSmoke final : public QObject {
public:
    explicit Phase6UiSmoke(QQmlApplicationEngine *engine, const QString &outputDirectory);
    void start();
private:
    void tick();
    void finish(const QString &failure = {});
    bool capture(const QString &name);
    QQmlApplicationEngine *m_engine;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_content;
    QString m_output;
    QStringList m_errors, m_frames;
    QTimer m_timer;
    int m_step = 0;
    bool m_prepared = false, m_finished = false;
    bool m_fileTabbed = false;
};
