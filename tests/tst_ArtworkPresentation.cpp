#include "cpp/ColorExtractor.h"
#include "meshgradient/MeshGradientItem.h"

#include <QBuffer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

namespace {
QImage image(QColor color) {
    QImage result(32, 32, QImage::Format_ARGB32); result.fill(color); return result;
}
QByteArray png(const QImage &value) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    value.save(&buffer, "PNG"); return bytes;
}
QUrl dataUrl(const QImage &value) {
    return QUrl("data:image/png;base64," + QString::fromLatin1(png(value).toBase64()));
}
void respond(QTcpSocket *socket, const QImage &value) {
    const auto body = png(value);
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    socket->disconnectFromHost();
}
}

class ArtworkPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void localAndResourceCoversResetWithoutNetwork() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path = dir.filePath("blue.png"); QVERIFY(image(Qt::blue).save(path));
        ColorExtractor extractor;
        QSignalSpy colors(&extractor, &ColorExtractor::colorsExtracted);
        QSignalSpy network(extractor.findChild<QNetworkAccessManager *>(), &QNetworkAccessManager::finished);
        extractor.extractColorsFromUrl(QUrl::fromLocalFile(path));
        QCOMPARE(colors.size(), 1); QVERIFY(!extractor.dominantColors().isEmpty());
        QVERIFY(extractor.renderUrl().toString().startsWith("data:image/png;base64,"));
        extractor.extractColorsFromUrl(QUrl("qrc:/QueMusic/resources/app/musicpic.png"));
        QCOMPARE(colors.size(), 2); QVERIFY(!extractor.renderUrl().isEmpty());
        extractor.extractColorsFromUrl(QUrl::fromLocalFile(dir.filePath("absent.png")));
        QCOMPARE(colors.size(), 3); QVERIFY(extractor.dominantColors().isEmpty());
        QVERIFY(extractor.renderUrl().isEmpty()); QCOMPARE(network.size(), 0);
        QImage gray(8, 8, QImage::Format_Grayscale8); gray.fill(100);
        QVERIFY(gray.save(dir.filePath("gray.png")));
        extractor.extractColorsFromUrl(QUrl::fromLocalFile(dir.filePath("gray.png")));
        QVERIFY(!extractor.dominantColors().isEmpty()); QVERIFY(!extractor.renderUrl().isEmpty());
    }
    void lateNetworkColorsCannotReplaceCurrentLocalCover() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QTemporaryDir dir; const auto path = dir.filePath("blue.png"); QVERIFY(image(Qt::blue).save(path));
        ColorExtractor extractor;
        QSignalSpy colors(&extractor, &ColorExtractor::colorsExtracted);
        QSignalSpy finished(extractor.findChild<QNetworkAccessManager *>(), &QNetworkAccessManager::finished);
        extractor.extractColorsFromUrl(QUrl(QString("http://127.0.0.1:%1/old.png").arg(server.serverPort())));
        QTRY_VERIFY(server.hasPendingConnections()); auto *socket = server.nextPendingConnection();
        extractor.extractColorsFromUrl(QUrl::fromLocalFile(path));
        const auto currentColors = extractor.dominantColors(); const auto currentRender = extractor.renderUrl();
        respond(socket, image(Qt::red)); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(colors.size(), 1); QCOMPARE(extractor.dominantColors(), currentColors);
        QCOMPARE(extractor.renderUrl(), currentRender);
    }
    void colorObserversCannotRestoreAnOlderRender() {
        QTemporaryDir dir; const auto red = dir.filePath("red.png"), blue = dir.filePath("blue.png");
        QVERIFY(image(Qt::red).save(red)); QVERIFY(image(Qt::blue).save(blue));
        ColorExtractor extractor; bool switched = false;
        connect(&extractor, &ColorExtractor::colorsExtracted, &extractor, [&] {
            if (!switched) { switched = true; extractor.extractColorsFromUrl(QUrl::fromLocalFile(blue)); }
        });
        extractor.extractColorsFromUrl(QUrl::fromLocalFile(red));
        ColorExtractor expected; expected.extractColorsFromUrl(QUrl::fromLocalFile(blue));
        QCOMPARE(extractor.imageSource(), expected.imageSource());
        QCOMPARE(extractor.renderUrl(), expected.renderUrl());
    }
    void lateMeshWorkerCannotReplaceANewerTexture() {
        QTemporaryDir dir; const auto path = dir.filePath("red.png"); QVERIFY(image(Qt::red).save(path));
        auto *pool = QThreadPool::globalInstance(); pool->waitForDone();
        const auto oldMaximum = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        pool->start([&] { entered.release(); release.acquire(); });
        // Always release the worker, including when a Qt test assertion returns early.
        struct Cleanup { QThreadPool *pool; QSemaphore &release; int maximum;
            ~Cleanup() { release.release(); pool->waitForDone(); pool->setMaxThreadCount(maximum); }
        } cleanup{pool, release, oldMaximum};
        QVERIFY(entered.tryAcquire(1, 1000));
        MeshGradientItem mesh; mesh.setAnimating(false);
        mesh.setCoverUrl(QUrl::fromLocalFile(path)); // Queued behind the blocked worker.
        const auto blue = QImage::fromData(png(image(Qt::blue))); mesh.setCoverUrl(dataUrl(blue));
        QCOMPARE(mesh.m_lastImage, blue);
        release.release(); pool->waitForDone(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(mesh.m_lastImage, blue); QCOMPARE(mesh.m_pendingImage, blue);
    }
    void lateMeshNetworkCannotReplaceANewerTexture() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        MeshGradientItem mesh; mesh.setAnimating(false);
        QSignalSpy finished(mesh.m_net, &QNetworkAccessManager::finished);
        mesh.setCoverUrl(QUrl(QString("http://127.0.0.1:%1/old.png").arg(server.serverPort())));
        QTRY_VERIFY(server.hasPendingConnections()); auto *socket = server.nextPendingConnection();
        const auto blue = QImage::fromData(png(image(Qt::blue))); mesh.setCoverUrl(dataUrl(blue));
        respond(socket, image(Qt::red)); QTRY_COMPARE(finished.size(), 1);
        QThreadPool::globalInstance()->waitForDone(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(mesh.m_lastImage, blue); QCOMPARE(mesh.m_pendingImage, blue);
    }
};

QTEST_MAIN(ArtworkPresentationTest)
#include "tst_ArtworkPresentation.moc"
