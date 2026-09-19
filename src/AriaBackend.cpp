#include "AriaBackend.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUuid>
#include <QRegularExpression>

AriaBackend::AriaBackend(QString directory, QObject *parent)
    : QObject(parent), stateDir(std::move(directory)) {
    network.setProxy(QNetworkProxy::NoProxy);
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, &AriaBackend::poll);
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        outputBuffer += process.readAllStandardOutput();
        QStringList useful;
        static const QRegularExpression timestamp("^\\d{2}/\\d{2} ");
        while (outputBuffer.contains('\n')) {
            const auto end = outputBuffer.indexOf('\n');
            const auto line = QString::fromLocal8Bit(outputBuffer.left(end)).trimmed();
            outputBuffer.remove(0, end + 1);
            // INFO exposes transfer retry reasons, but also very noisy RPC traffic.
            // Keep diagnostic records, not HTTP headers or local polling chatter.
            if (!timestamp.match(line).hasMatch() && !line.startsWith("Exception:") && !line.startsWith("->")) continue;
            bool skip = false;
            for (const auto *noise : {"RPC:", "HTTP Server", "Executing RPC", "HttpServer:", "Persist connection", "Serialized session", "Not considered:", "configured address"})
                if (line.contains(QLatin1String(noise))) { skip = true; break; }
            if (!skip) useful.append(line);
        }
        if (!useful.isEmpty()) emit message(useful.join('\n'));
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (!stopping && e == QProcess::FailedToStart)
            emit error("Could not start aria2c. Install it on PATH or place it beside AriaDownload.exe.\n" + process.errorString());
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        ready = false; emit readyChanged(false); timer.stop();
        if (!stopping) emit error(QString("aria2 exited (code %1). Reopen the app to restore saved downloads. See the activity log.").arg(code));
    });
}
AriaBackend::~AriaBackend() { stop(); }

void AriaBackend::start() {
    if (process.state() != QProcess::NotRunning) return;
    stopping = false;
    configured.clear(); lastProgressAt.clear(); lastBytes.clear(); stalled.clear();
    if (!QDir().mkpath(stateDir)) { emit error("Cannot create the application data folder."); return; }
    const QString session = stateDir + "/aria2.session";
    QFile file(session);
    if (!file.open(QIODevice::ReadWrite)) { emit error("Cannot open the download session: " + file.errorString()); return; }
    file.close();
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) { emit error("Cannot allocate a local RPC port."); return; }
    port = probe.serverPort(); probe.close();
    secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString executable = QCoreApplication::applicationDirPath() + "/aria2c.exe";
    if (!QFileInfo::exists(executable)) executable = QStandardPaths::findExecutable("aria2c");
    if (executable.isEmpty()) { emit error("aria2c was not found. Add it to PATH, then reopen this app."); return; }
    QStringList args {
        "--no-conf=true", "--enable-rpc=true", "--rpc-listen-all=false",
        "--rpc-listen-port=" + QString::number(port), "--rpc-secret=" + secret,
        "--continue=true", "--always-resume=true", "--max-tries=0", "--retry-wait=5",
        "--connect-timeout=30", "--timeout=30", "--lowest-speed-limit=0",
        "--max-connection-per-server=8", "--split=8", "--min-split-size=1M",
        "--max-concurrent-downloads=3", "--file-allocation=none",
        "--auto-file-renaming=false", "--allow-overwrite=false",
        "--auto-save-interval=5", "--save-session-interval=5",
        "--input-file=" + session, "--save-session=" + session,
        "--save-not-found=true", "--keep-unfinished-download-result=true",
        "--max-download-result=1000", "--follow-torrent=false", "--follow-metalink=false",
        "--enable-dht=false", "--enable-dht6=false", "--enable-peer-exchange=false",
        // Retry reasons (including timeouts) are logged at INFO, not WARN.
        "--summary-interval=0", "--show-console-readout=false", "--console-log-level=info", "--enable-color=false",
        "--stop-with-process=" + QString::number(QCoreApplication::applicationPid())
    };
    emit message("Starting aria2: " + executable);
    process.start(executable, args);
    startupAttempts = 0; timer.start();
}

void AriaBackend::rpc(QString method, QJsonArray params, Callback callback) {
    if (method != "system.multicall") params.prepend("token:" + secret);
    QJsonObject request {{"jsonrpc", "2.0"}, {"id", QUuid::createUuid().toString()},
                         {"method", method}, {"params", params}};
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/jsonrpc").arg(port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(4000);
    auto *reply = network.post(req, QJsonDocument(request).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        QJsonParseError parse;
        const auto obj = QJsonDocument::fromJson(reply->readAll(), &parse).object();
        QString error;
        if (obj.contains("error")) error = obj["error"].toObject()["message"].toString();
        else if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (parse.error != QJsonParseError::NoError || !obj.contains("result")) error = "Invalid aria2 RPC response.";
        reply->deleteLater(); callback(obj["result"], error);
    });
}

void AriaBackend::poll() {
    if (polling || stopping || process.state() != QProcess::Running) return;
    polling = true;
    if (!ready) {
        rpc("aria2.getVersion", {}, [this](QJsonValue, QString e) {
            polling = false;
            if (stopping) return;
            if (e.isEmpty()) { ready = true; emit readyChanged(true); poll(); }
            else if (++startupAttempts == 15) { timer.stop(); emit error("Could not connect to the local aria2 backend. Reopen the app and check the activity log."); }
        });
        return;
    }
    // A single multicall avoids overlapping polls and inconsistent UI updates.
    QJsonArray keys {"gid", "status", "totalLength", "completedLength", "downloadSpeed", "files", "dir", "errorCode", "errorMessage"};
    QJsonArray calls;
    for (const auto &method : {"aria2.tellActive", "aria2.tellWaiting", "aria2.tellStopped"}) {
        QJsonArray params {"token:" + secret};
        if (QString(method) != "aria2.tellActive") { params.append(0); params.append(1000); }
        params.append(keys);
        calls.append(QJsonObject {{"methodName", method}, {"params", params}});
    }
    rpc("system.multicall", QJsonArray {QJsonValue(calls)}, [this](QJsonValue result, QString e) {
        polling = false;
        if (stopping) return;
        if (!e.isEmpty()) { emit message("Waiting for local backend: " + e); return; }
        QJsonArray tasks;
        for (const auto &part : result.toArray()) {
            if (!part.isArray()) { emit message("A status query failed."); return; }
            for (const auto &value : part.toArray().first().toArray()) {
                auto task = value.toObject();
                if (task["status"] == "removed") continue;
                const auto gid = task["gid"].toString();
                const auto now = QDateTime::currentMSecsSinceEpoch();
                const auto state = task["status"].toString();
                if ((state == "active" || state == "waiting" || state == "paused") && !configured.contains(gid)) {
                    configured.insert(gid);
                    rpc("aria2.getOption", {gid}, [this, gid](QJsonValue options, QString e) {
                        if (!e.isEmpty()) { configured.remove(gid); return; }
                        // changeOption can restart an active connection, even when
                        // the supplied value is unchanged. Only migrate old sessions.
                        if (options.toObject()["lowest-speed-limit"].toString() == "0" || stopping) return;
                        rpc("aria2.changeOption", {gid, QJsonObject {{"lowest-speed-limit", "0"}}}, [this, gid](QJsonValue, QString e) {
                            if (!e.isEmpty()) { configured.remove(gid); emit message("Could not update transfer settings: " + e); }
                        });
                    });
                }
                if (task["status"] == "active") {
                    const auto bytes = task["completedLength"].toString().toLongLong();
                    if (!lastProgressAt.contains(gid) || lastBytes.value(gid) != bytes) {
                        lastProgressAt[gid] = now; lastBytes[gid] = bytes; stalled.remove(gid);
                    }
                    if (now - lastProgressAt[gid] >= 30000) {
                        task["connectionHint"] = "No data received for 30 seconds. aria2 is still retrying; check the activity log for DNS, connection or server errors.";
                        if (!stalled.contains(gid)) { stalled.insert(gid); emit transferStalled(); }
                    }
                } else {
                    lastProgressAt.remove(gid); lastBytes.remove(gid); stalled.remove(gid);
                }
                if (task["status"] == "error" && transientError(task["errorCode"].toString().toInt())) {
                    if (!retryAt.contains(gid)) retryAt[gid] = now + 5000;
                    if (now >= retryAt[gid] && !busy.contains(gid)) retry(task);
                    task["status"] = "retrying";
                }
                tasks.append(task);
            }
        }
        emit tasksChanged(tasks);
    });
}

bool AriaBackend::transientError(int code) {
    return code == 2 || code == 5 || code == 6 || code == 19 || code == 29;
}
QString AriaBackend::validateUrl(const QString &text) {
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
        return "Enter a direct HTTP or HTTPS file URL.";
    if (!url.userInfo().isEmpty()) return "URLs with embedded usernames or passwords are not supported.";
    return {};
}
QJsonObject AriaBackend::transferOptions(int connections) {
    // Apply the standard retry policy per task as well as globally:
    // restored sessions can otherwise retain the old 10K speed cutoff.
    return {{"continue", "true"}, {"max-tries", "0"}, {"retry-wait", "5"},
            {"connect-timeout", "30"}, {"timeout", "30"}, {"lowest-speed-limit", "0"},
            {"min-split-size", "1M"}, {"split", QString::number(qBound(1, connections, 16))},
            {"max-connection-per-server", QString::number(qBound(1, connections, 16))}};
}
void AriaBackend::save() {
    rpc("aria2.saveSession", {}, [this](QJsonValue, QString e) { if (!e.isEmpty()) emit error("Could not save downloads: " + e); });
}
void AriaBackend::add(const QString &url, const QString &directory, int connections) {
    if (!ready || stopping) return;
    const auto validation = validateUrl(url);
    if (!validation.isEmpty()) { emit error(validation); return; }
    if (directory.trimmed().isEmpty() || !QDir().mkpath(directory)) { emit error("Choose a writable download folder."); return; }
    QJsonObject options = transferOptions(connections);
    options["dir"] = QDir(directory).absolutePath();
    rpc("aria2.addUri", {QJsonArray {url}, options}, [this](QJsonValue, QString e) {
        if (!e.isEmpty()) emit error("Could not add download: " + e); else { save(); poll(); }
    });
}
void AriaBackend::pause(const QString &gid) {
    if (!ready || busy.contains(gid)) return;
    rpc("aria2.pause", {gid}, [this](QJsonValue, QString e) { if (!e.isEmpty()) emit error(e); else { save(); poll(); } });
}
void AriaBackend::resume(const QString &gid) {
    if (!ready || busy.contains(gid)) return;
    rpc("aria2.unpause", {gid}, [this](QJsonValue, QString e) { if (!e.isEmpty()) emit error(e); else { save(); poll(); } });
}
void AriaBackend::remove(const QString &gid) {
    if (!ready || busy.contains(gid)) return;
    busy.insert(gid);
    rpc("aria2.tellStatus", {gid}, [this, gid](QJsonValue value, QString e) {
        if (!e.isEmpty()) { busy.remove(gid); emit error(e); return; }
        const auto status = value.toObject()["status"].toString();
        const auto method = (status == "complete" || status == "error" || status == "removed") ? "aria2.removeDownloadResult" : "aria2.remove";
        rpc(method, {gid}, [this, gid](QJsonValue, QString e) {
            busy.remove(gid); retryAt.remove(gid);
            if (!e.isEmpty()) emit error(e); else { save(); poll(); }
        });
    });
}
void AriaBackend::retry(const QJsonObject &task) {
    const auto gid = task["gid"].toString();
    if (!ready || stopping || busy.contains(gid)) return;
    const auto files = task["files"].toArray();
    if (files.isEmpty()) return;
    QJsonArray uris;
    for (const auto &uri : files.first().toObject()["uris"].toArray()) uris.append(uri.toObject()["uri"]);
    if (uris.isEmpty()) return;
    busy.insert(gid);
    rpc("aria2.getOption", {gid}, [this, task, gid, uris](QJsonValue value, QString e) {
        if (!e.isEmpty() || stopping) { busy.remove(gid); return; }
        auto old = value.toObject();
        QJsonObject options = transferOptions(old["max-connection-per-server"].toString().toInt());
        for (const auto &key : {"dir", "out", "split", "max-connection-per-server"})
            if (old.contains(key)) options[key] = old[key];
        // Pin the actual filename, including Content-Disposition names, when resuming.
        const auto path = task["files"].toArray().first().toObject()["path"].toString();
        if (!path.isEmpty()) { options["dir"] = QFileInfo(path).absolutePath(); options["out"] = QFileInfo(path).fileName(); }
        rpc("aria2.addUri", {uris, options}, [this, gid](QJsonValue, QString e) {
            if (!e.isEmpty()) { busy.remove(gid); retryAt[gid] = QDateTime::currentMSecsSinceEpoch() + 30000; emit message("Retry delayed: " + e); return; }
            rpc("aria2.removeDownloadResult", {gid}, [this, gid](QJsonValue, QString e) {
                busy.remove(gid); retryAt.remove(gid);
                if (!e.isEmpty()) emit error(e);
                save(); poll();
            });
        });
    });
}
void AriaBackend::stop() {
    if (stopping) return;
    stopping = true; timer.stop();
    if (ready && process.state() == QProcess::Running) {
        QEventLoop loop;
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
        rpc("aria2.saveSession", {}, [this](QJsonValue, QString e) {
            if (!e.isEmpty()) emit message("Session save failed: " + e);
            rpc("aria2.shutdown", {}, [](QJsonValue, QString) {});
        });
        deadline.start(7000); loop.exec();
    }
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(2000); }
    ready = false;
}
