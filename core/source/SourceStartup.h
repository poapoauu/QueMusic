#pragma once

#include <QStringList>

class QCoreApplication;
class QObject;
class QQmlApplicationEngine;
class SourceManager;

QStringList defaultSourcePluginSearchPaths(const QCoreApplication &application);
SourceManager *createAndLoadSourceManager(const QCoreApplication &application, QObject *parent);
SourceManager *initializeSourceStartupBoundary(const QCoreApplication &application,
                                              QQmlApplicationEngine &engine);
