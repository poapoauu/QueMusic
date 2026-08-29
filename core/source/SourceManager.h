#pragma once

#include "IMusicSourcePlugin.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <vector>

class PluginManager;

class SourceManager : public QObject {
    Q_OBJECT

public:
    explicit SourceManager(PluginManager *pluginManager = nullptr, QObject *parent = nullptr);
    ~SourceManager() override;

    void addSearchPath(const QString &path);
    int loadAll();
    bool loadSourcePackage(const QString &packageId);

    QVariantList availableSources() const;
    QStringList sourceIds() const;
    PluginManager *pluginManager() const;
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
        QString packageId;
    };

    void synchronizeSourcePackage(const QString &packageId);
    void removeSourcePackage(const QString &packageId);

    QPointer<PluginManager> m_pluginManager;
    QNetworkAccessManager m_network;
    std::vector<LoadedSource> m_sources;
};
