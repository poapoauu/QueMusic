#pragma once

#include <QStringList>

class QCoreApplication;
class QObject;
class SourceManager;

QStringList defaultSourcePluginSearchPaths(const QCoreApplication &application);
SourceManager *createAndLoadSourceManager(const QCoreApplication &application, QObject *parent);
