#include "Phase6UiSmoke.h"
#include "../plugin-ui/PluginTheme.h"
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QQuickItem>

Phase6UiSmoke::Phase6UiSmoke(QQmlApplicationEngine *engine, const QString &outputDirectory)
    : m_engine(engine), m_output(outputDirectory)
{
    connect(engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
        for (const auto &error : errors) {
            const auto description=error.description();
            if(description.contains("ReferenceError")||description.contains("TypeError")
                ||description.contains("Cannot assign")||description.contains("Unable to assign [undefined]")
                ||description.contains("is not defined")
                ||description.contains("is not a function"))
                m_errors.append(error.toString());
        }
    });
    connect(&m_timer,&QTimer::timeout,this,[this] { tick(); });
    QTimer::singleShot(20000,this,[this] { if(!m_finished)finish("timeout"); });
}
void Phase6UiSmoke::start()
{
    if(m_engine->rootObjects().size()!=1) { finish("root-window-unavailable"); return; }
    m_window=qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    if(!m_window) { finish("root-is-not-a-window"); return; }
    m_content=m_window->findChild<QObject *>("originalMainContent");
    if(!m_content) { finish("original-content-unavailable"); return; }
    if(PluginTheme::instance()->fontBody().pixelSize()<=0) { finish("theme-not-synchronized"); return; }
    m_window->show();
    m_timer.start(700);
}
bool Phase6UiSmoke::capture(const QString &name)
{
    if(!m_window||!m_window->isExposed())return false;
    const auto frame=m_window->grabWindow();
    if(frame.isNull()||!frame.save(QDir(m_output).filePath(name+".png")))return false;
    m_frames.append(name); return true;
}
void Phase6UiSmoke::tick()
{
    if(!m_window||!m_content) { finish("window-destroyed"); return; }
    const QList<int> pages{0,1,3,4,5,6};
    const QStringList names{"home","playlist","favourite","file","download","search"};
    if(m_step<pages.size()) {
        if(!m_prepared) {
            QVariant accepted;
            auto *sidebar=m_window->findChild<QObject *>("originalSidebar");
            QObject *navigation=m_step==5 ? m_content.data() : sidebar;
            const char *method=m_step==5 ? "contentIndexed" : "navigate";
            if(!navigation||!QMetaObject::invokeMethod(navigation,method,Q_RETURN_ARG(QVariant,accepted),
                                         Q_ARG(QVariant,pages[m_step]))||!accepted.toBool()) {
                finish("navigation-rejected"); return;
            }
            m_prepared=true; return;
        }
        const auto loaderName=names[m_step]+"PageLoader";
        auto *loader=m_content->findChild<QObject *>(loaderName);
        if(!loader||loader->property("status").toInt()!=1||!loader->property("item").value<QObject *>()) {
            finish("page-load-failed:"+names[m_step]); return;
        }
        if(m_step==3) {
            auto *page=loader->property("item").value<QObject *>();
            auto *tabs=page->findChild<QQuickItem *>("localDirectoryTabs");
            auto *roots=page->findChild<QQuickItem *>("pluginDirectoryRoots");
            if(!tabs||!roots) { finish("source-directory-tab-unavailable"); return; }
            if(!m_fileTabbed) {
                // Real pointer delivery also updates the tab's visual selection;
                // emitting its signal directly would bypass that interaction.
                const auto point=tabs->mapToScene(QPointF(tabs->width()*0.75,tabs->height()/2));
                const auto global=m_window->mapToGlobal(point.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(m_window,&press);
                QCoreApplication::sendEvent(m_window,&release);
                m_fileTabbed=true; return;
            }
            if(!roots->isVisible()) { finish("source-directory-tab-not-selected"); return; }
        }
        if(!capture(names[m_step])) { finish("page-capture-failed:"+names[m_step]); return; }
        ++m_step; m_prepared=false; return;
    }
    auto *player=m_window->findChild<QObject *>("originalPlayerControl");
    auto *queue=m_window->findChild<QObject *>("originalPlaybackQueue");
    auto *options=m_window->findChild<QObject *>("currentPlaybackOptionsDialog");
    if(!player||!queue||!options) { finish("player-ui-unavailable"); return; }
    if(m_step==6) {
        if(!m_prepared) {
            // Sticky Source with no active track must show an empty Source
            // queue, never borrow a restored Legacy queue or player clock.
            m_window->setProperty("sourceLyricsMode",true);
            if(!QMetaObject::invokeMethod(player,"togglePlayList")) { finish("queue-open-failed"); return; }
            m_prepared=true; return;
        }
        if(!queue->property("secureMode").toBool()||!queue->property("visible").toBool()
            ||!capture("source-queue-idle")) { finish("source-queue-invalid"); return; }
        QMetaObject::invokeMethod(queue,"close");
        ++m_step; m_prepared=false; return;
    }
    if(m_step==8) {
        if(!PluginTheme::instance()->dark()||!capture("source-player-options-dark")) {
            finish("theme-change-not-synchronized"); return;
        }
        QMetaObject::invokeMethod(options,"close"); finish(); return;
    }
    if(!m_prepared) {
        if(!QMetaObject::invokeMethod(options,"open")) { finish("options-open-failed"); return; }
        m_prepared=true; return;
    }
    if(!options->property("visible").toBool()||!capture("source-player-options")) {
        finish("options-capture-failed"); return;
    }
    auto *style=m_engine->singletonInstance<QObject *>("QueMusic","Style");
    auto *settings=style ? style->property("settings").value<QObject *>() : nullptr;
    if(!settings||!settings->setProperty("theme",1)) { finish("host-theme-unavailable"); return; }
    ++m_step;
}
void Phase6UiSmoke::finish(const QString &failure)
{
    if(m_finished)return;
    m_finished=true; m_timer.stop();
    if(!failure.isEmpty())m_errors.append(failure);
    const bool passed=m_errors.isEmpty()&&m_frames.size()==9;
    QJsonObject report{{"passed",passed},{"platform",QGuiApplication::platformName()},
        {"frames",QJsonArray::fromStringList(m_frames)},
        {"errors",QJsonArray::fromStringList(m_errors)},
        {"scope","real application; isolated empty profile; no real service or audio playback"}};
    QSaveFile file(QDir(m_output).filePath("report.json"));
    const auto bytes=QJsonDocument(report).toJson();
    const bool saved=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
    QCoreApplication::exit(passed&&saved ? 0 : 1);
}
