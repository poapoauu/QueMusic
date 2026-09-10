#pragma once

#include <QStringList>
#include <memory>

class QCoreApplication;
class QObject;
class QQmlApplicationEngine;
class SourceManager;
class PluginManager;
class MusicHub;
class PlaybackCoordinator;
class QtPlaybackController;
class PluginSettingsController;

QStringList defaultSourcePluginSearchPaths(const QCoreApplication &application);
SourceManager *createAndLoadSourceManager(const QCoreApplication &application, QObject *parent);
SourceManager *initializeSourceStartupBoundary(const QCoreApplication &application,
                                              QQmlApplicationEngine &engine);
std::unique_ptr<PluginManager> createAndLoadPluginManager(
    const QCoreApplication &application, QObject *parent = nullptr);
void installSourceRuntimeContext(QQmlApplicationEngine &engine,
                                 PluginManager *plugins, MusicHub *musicHub,
                                 PlaybackCoordinator *playbackCoordinator,
                                 QtPlaybackController *playbackController,
                                 PluginSettingsController *pluginSettings);
