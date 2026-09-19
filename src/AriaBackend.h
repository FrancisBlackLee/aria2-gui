#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <functional>

class AriaBackend : public QObject {
    Q_OBJECT
public:
    explicit AriaBackend(QString stateDirectory, QObject *parent = nullptr);
    ~AriaBackend() override;
    void start();
    void stop();
    bool isReady() const { return ready; }
    void add(const QString &url, const QString &directory, int connections = 8);
    void pause(const QString &gid);
    void resume(const QString &gid);
    void remove(const QString &gid);
    void retry(const QJsonObject &task);
    static bool transientError(int code);
    static QString validateUrl(const QString &url);
    static QJsonObject transferOptions(int connections);
signals:
    void readyChanged(bool ready);
    void tasksChanged(QJsonArray tasks);
    void error(QString message);
    void message(QString message);
    void transferStalled();
private:
    using Callback = std::function<void(QJsonValue, QString)>;
    void rpc(QString method, QJsonArray params, Callback callback);
    void poll();
    void save();
    QString stateDir, secret;
    quint16 port = 0;
    QNetworkAccessManager network;
    QProcess process;
    QTimer timer;
    bool ready = false, stopping = false, polling = false;
    int startupAttempts = 0;
    QSet<QString> busy;
    QHash<QString, qint64> retryAt;
    QHash<QString, qint64> lastProgressAt;
    QHash<QString, qint64> lastBytes;
    QSet<QString> stalled;
    QSet<QString> configured;
    QByteArray outputBuffer;
};
