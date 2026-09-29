#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include "plugins/local-source/LocalIdentityStore.h"

namespace {
bool isOpaqueUuid(const QString &id)
{
    return !id.startsWith("file:") && !QUuid(id).isNull()
           && id == QUuid(id).toString(QUuid::WithoutBraces);
}

QJsonObject readIndex(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}

class LocalIdentityStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void samePathKeepsIdAcrossRestart()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString index = temp.filePath("index.json");
        const QString track = temp.filePath("song.mp3");
        const QString directory = temp.filePath("album");
        LocalIdentitySnapshot first;
        QString error;
        {
            LocalIdentityStore store(index, "local/home");
            QVERIFY(store.reconcile({track}, {directory, track}, &first, &error));
        }
        QVERIFY(isOpaqueUuid(first.trackIdsByPath.value(track)));
        QVERIFY(isOpaqueUuid(first.directoryIdsByPath.value(directory)));
        QVERIFY(isOpaqueUuid(first.directoryIdsByPath.value(track)));
        QVERIFY(first.trackIdsByPath.value(track) != first.directoryIdsByPath.value(directory));
        QVERIFY(first.trackIdsByPath.value(track) != first.directoryIdsByPath.value(track));
        const auto data = readIndex(index);
        QCOMPARE(data.value("version").toInt(), 1);
        QCOMPARE(data.value("sourceInstanceId").toString(), QString("local/home"));
        LocalIdentitySnapshot second;
        LocalIdentityStore restarted(index, "local/home");
        QVERIFY(restarted.reconcile({track}, {directory, track}, &second, &error));
        QCOMPARE(second.trackIdsByPath.value(track), first.trackIdsByPath.value(track));
        QCOMPARE(second.directoryIdsByPath.value(directory), first.directoryIdsByPath.value(directory));
        QCOMPARE(second.directoryIdsByPath.value(track), first.directoryIdsByPath.value(track));
    }

    void independentInstancesDoNotShareIds()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString track = temp.filePath("same.mp3");
        const QString directory = temp.filePath("same-dir");
        LocalIdentitySnapshot home, office;
        QString error;
        LocalIdentityStore homeStore(temp.filePath("home.json"), "local/home");
        LocalIdentityStore officeStore(temp.filePath("office.json"), "local/office");
        QVERIFY(homeStore.reconcile({track}, {directory}, &home, &error));
        QVERIFY(officeStore.reconcile({track}, {directory}, &office, &error));
        QVERIFY(home.trackIdsByPath.value(track) != office.trackIdsByPath.value(track));
        QVERIFY(home.directoryIdsByPath.value(directory) != office.directoryIdsByPath.value(directory));
        LocalIdentityStore wrongInstance(temp.filePath("home.json"), "local/office");
        LocalIdentitySnapshot untouched = office;
        QVERIFY(!wrongInstance.reconcile({track}, {directory}, &untouched, &error));
        QCOMPARE(untouched.trackIdsByPath, office.trackIdsByPath);
        QCOMPARE(readIndex(temp.filePath("home.json")).value("sourceInstanceId").toString(),
                 QString("local/home"));
    }

    void removedThenRecreatedGetsNewId()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath("song.mp3");
        const QString directory = temp.filePath("album");
        const QString index = temp.filePath("index.json");
        LocalIdentityStore store(index, "local/home");
        LocalIdentitySnapshot first, removed, recreated;
        QString error;
        QVERIFY(store.reconcile({path}, {directory}, &first, &error));
        QVERIFY(store.reconcile({}, {}, &removed, &error));
        QVERIFY(removed.trackIdsByPath.isEmpty());
        QVERIFY(removed.directoryIdsByPath.isEmpty());
        QVERIFY(store.reconcile({path}, {directory}, &recreated, &error));
        QVERIFY(first.trackIdsByPath.value(path) != recreated.trackIdsByPath.value(path));
        QVERIFY(first.directoryIdsByPath.value(directory) != recreated.directoryIdsByPath.value(directory));
    }

    void failedAtomicWriteDoesNotPublishNewId()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("private"));
        const QString parent = temp.filePath("private");
        const QString index = parent + "/index.json";
        const QString path = temp.filePath("song.mp3");
        LocalIdentityStore store(index, "local/home");
        LocalIdentitySnapshot published;
        QString error;
        QVERIFY(store.reconcile({path}, {}, &published, &error));
        QFile before(index);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();
        const auto permissions = QFile::permissions(parent);
        QVERIFY(QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        LocalIdentitySnapshot output = published;
        const bool succeeded = store.reconcile({path, temp.filePath("new.mp3")}, {}, &output, &error);
        QVERIFY(QFile::setPermissions(parent, permissions));
        QVERIFY(!succeeded);
        QVERIFY(!error.isEmpty());
        QCOMPARE(output.trackIdsByPath, published.trackIdsByPath);
        QFile after(index);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);
    }

    void corruptIndexDoesNotReuseAnOldId()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString index = temp.filePath("index.json");
        const QString path = temp.filePath("song.mp3");
        LocalIdentitySnapshot first, afterCorruption;
        QString error;
        LocalIdentityStore store(index, "local/home");
        QVERIFY(store.reconcile({path}, {}, &first, &error));
        QFile corrupt(index);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(corrupt.write("{broken") > 0);
        corrupt.close();
        LocalIdentityStore restarted(index, "local/home");
        QVERIFY(restarted.reconcile({path}, {}, &afterCorruption, &error));
        QVERIFY(isOpaqueUuid(afterCorruption.trackIdsByPath.value(path)));
        QVERIFY(afterCorruption.trackIdsByPath.value(path) != first.trackIdsByPath.value(path));
        QCOMPARE(readIndex(index).value("version").toInt(), 1);
    }

    void largeFutureVersionPreservesIndex()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString index = temp.filePath("index.json");
        const QByteArray futureIndex =
            R"({"version":2147483648,"sourceInstanceId":"local/home","tracks":{},"directories":{}})";
        QFile file(index);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(futureIndex), futureIndex.size());
        file.close();

        LocalIdentitySnapshot output;
        output.trackIdsByPath.insert("sentinel", "unchanged");
        QString error;
        LocalIdentityStore store(index, "local/home");
        QVERIFY(!store.reconcile({temp.filePath("song.mp3")}, {}, &output, &error));
        QCOMPARE(error, QString("local.identity.futureVersion"));
        QCOMPARE(output.trackIdsByPath.value("sentinel"), QString("unchanged"));
        QFile after(index);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), futureIndex);
    }
};

QTEST_GUILESS_MAIN(LocalIdentityStoreTest)
#include "tst_LocalIdentityStore.moc"
