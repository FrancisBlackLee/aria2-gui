#include "AriaBackend.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QRegularExpression>

// A local range-capable server: deliberately truncates one response, then serves
// chunks slowly enough to exercise pause, saved sessions and byte-range resume.
class FileServer : public QTcpServer {
public:
    QByteArray payload = QByteArray(4 * 1024 * 1024, '\0');
    bool dropNext = true;
    int ranges = 0, drops = 0, slowRequests = 0;
    FileServer() {
        for (int i = 0; i < payload.size(); ++i) payload[i] = char((i * 31 + i / 997) % 251);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n") || socket->property("served").toBool()) return;
                    socket->setProperty("served", true);
                    const bool slow = request->contains("/slow.bin");
                    if (slow) ++slowRequests;
                    if (request->contains("/missing")) {
                        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return;
                    }
                    auto match = QRegularExpression("Range: bytes=(\\d+)-(\\d*)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(*request));
                    int begin = match.hasMatch() ? match.captured(1).toInt() : 0;
                    int end = match.hasMatch() && !match.captured(2).isEmpty() ? match.captured(2).toInt() : payload.size() - 1;
                    end = qMin(end, int(payload.size()) - 1);
                    if (begin > 0) ++ranges;
                    QByteArray header = match.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                    header += "Accept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(end - begin + 1) + "\r\n";
                    if (match.hasMatch()) header += "Content-Range: bytes " + QByteArray::number(begin) + "-" + QByteArray::number(end) + "/" + QByteArray::number(payload.size()) + "\r\n";
                    socket->write(header + "Connection: close\r\n\r\n");
                    bool drop = dropNext; dropNext = false;
                    auto offset = std::make_shared<int>(begin);
                    auto ticks = std::make_shared<int>(0);
                    auto *timer = new QTimer(socket); timer->setInterval(slow ? 50 : 20);
                    connect(timer, &QTimer::timeout, socket, [this, socket, offset, end, begin, drop, timer, slow, ticks] {
                        if (socket->state() != QAbstractSocket::ConnectedState) { timer->stop(); return; }
                        int count = qMin(slow && ++*ticks < 240 ? 256 : 32768, end - *offset + 1);
                        socket->write(payload.mid(*offset, count)); *offset += count;
                        if (drop && *offset - begin >= 512 * 1024) { ++drops; timer->stop(); socket->disconnectFromHost(); }
                        else if (*offset > end) { timer->stop(); socket->disconnectFromHost(); }
                    }); timer->start();
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QString url(QString name) const { return QString("http://127.0.0.1:%1/%2").arg(serverPort()).arg(name); }
};

class BackendTests : public QObject {
    Q_OBJECT
private slots:
    void validation() {
        QVERIFY(AriaBackend::validateUrl("https://zenodo.org/records/123/files/file.zip?download=1").isEmpty());
        QVERIFY(!AriaBackend::validateUrl("file:///C:/secret").isEmpty());
        QVERIFY(!AriaBackend::validateUrl("https://user:password@example.com/file").isEmpty());
        for (int code : {2, 5, 6, 19, 29}) QVERIFY(AriaBackend::transientError(code));
        for (int code : {3, 8, 9, 13, 17, 24, 32}) QVERIFY(!AriaBackend::transientError(code));
        QCOMPARE(AriaBackend::transferOptions(8)["lowest-speed-limit"].toString(), QString("0"));
    }
    void realDownloads() {
        QTemporaryDir state, output;
        QVERIFY(state.isValid() && output.isValid());
        FileServer server;
        QVERIFY(server.isListening());
        QJsonArray tasks;
        auto find = [&tasks](const QString &name) {
            for (auto v : tasks) {
                auto t = v.toObject();
                const auto file = t["files"].toArray().first().toObject();
                if (file["path"].toString().endsWith(name)) return t;
                for (auto uri : file["uris"].toArray())
                    if (QUrl(uri.toObject()["uri"].toString()).fileName() == name) return t;
            }
            return QJsonObject {};
        };
        AriaBackend backend(state.path());
        connect(&backend, &AriaBackend::message, this, [](const QString &text) { qInfo().noquote() << text; });
        connect(&backend, &AriaBackend::error, this, [](const QString &text) { qWarning().noquote() << text; });
        QSignalSpy errors(&backend, &AriaBackend::error);
        connect(&backend, &AriaBackend::tasksChanged, this, [&tasks](QJsonArray t) { tasks = t; });
        backend.start(); QTRY_VERIFY_WITH_TIMEOUT(backend.isReady(), 20000);
        backend.add(server.url("drop.bin"), output.path(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(find("drop.bin")["status"].toString(), QString("complete"), 35000);
        QVERIFY(server.drops == 1); QVERIFY(server.ranges > 0);
        QFile file(output.path() + "/drop.bin"); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256), QCryptographicHash::hash(server.payload, QCryptographicHash::Sha256)); file.close();
        backend.add(server.url("resume.bin"), output.path(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(find("resume.bin")["completedLength"].toString().toLongLong() > 0, 10000);
        backend.pause(find("resume.bin")["gid"].toString());
        QTRY_COMPARE_WITH_TIMEOUT(find("resume.bin")["status"].toString(), QString("paused"), 10000);
        backend.stop();
        QVERIFY(QFile::exists(output.path() + "/resume.bin.aria2"));
        tasks = {}; backend.start(); QTRY_VERIFY_WITH_TIMEOUT(backend.isReady(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(find("resume.bin")["status"].toString(), QString("paused"), 10000);
        backend.resume(find("resume.bin")["gid"].toString());
        QTRY_COMPARE_WITH_TIMEOUT(find("resume.bin")["status"].toString(), QString("complete"), 20000);
        QFile resumed(output.path() + "/resume.bin"); QVERIFY(resumed.open(QIODevice::ReadOnly)); QCOMPARE(resumed.readAll(), server.payload); resumed.close();
        backend.add(server.url("missing"), output.path(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(find("missing")["status"].toString(), QString("error"), 15000);
        backend.remove(find("missing")["gid"].toString());
        QTRY_VERIFY_WITH_TIMEOUT(find("missing").isEmpty(), 10000);
        backend.remove(find("resume.bin")["gid"].toString());
        QTRY_VERIFY_WITH_TIMEOUT(find("resume.bin").isEmpty(), 10000);
        QVERIFY(QFile::exists(output.path() + "/resume.bin"));
        backend.add(server.url("slow.bin"), output.path(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(find("slow.bin")["status"].toString(), QString("complete"), 35000);
        QCOMPARE(server.slowRequests, 1); // A healthy slow connection must not be repeatedly restarted.
        QFile slowFile(output.path() + "/slow.bin"); QVERIFY(slowFile.open(QIODevice::ReadOnly));
        QCOMPARE(slowFile.readAll(), server.payload); slowFile.close();
        backend.stop(); QCOMPARE(errors.size(), 0);
    }
};
QTEST_GUILESS_MAIN(BackendTests)
#include "backend_tests.moc"
