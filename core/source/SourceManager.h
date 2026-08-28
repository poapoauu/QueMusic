#pragma once

#include "IMusicSourcePlugin.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <vector>

class QPluginLoader;

class SourceManager : public QObject {
    Q_OBJECT

public:
    explicit SourceManager(QObject *parent = nullptr);
    ~SourceManager() override;

    void addSearchPath(const QString &path);
    int loadAll();

    QVariantList availableSources() const;
    QStringList sourceIds() const;
    IMusicSourceSession *createSession(const QString &sourceId, const SourceAccount &account,
                                       QObject *parent);
    QUuid requestArtwork(const QString &sourceId, IMusicSourceSession *session,
                         const TrackRef &track) const;

signals:
    void sourceLoaded(QString sourceId);
    void sourceLoadFailed(QString pluginPath, QString error);
    void sourceChanged();

private:
    struct LoadedSource {
        SourceDescriptor descriptor;
        IMusicSourcePlugin *plugin = nullptr;
    };

    void reportLoadFailure(const QString &pluginPath, const QString &error);

    QStringList m_searchPaths;
    QNetworkAccessManager m_network;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    std::vector<LoadedSource> m_sources;
};
