#include "DownloadModel.h"
#include <QFileInfo>
#include <QUrl>
#include <QColor>

QString DownloadModel::bytes(qint64 value) {
    double number = double(value);
    const QStringList units {"B", "KiB", "MiB", "GiB", "TiB"};
    int unit = 0;
    while (number >= 1024 && unit < units.size() - 1) { number /= 1024; ++unit; }
    return QString::number(number, 'f', unit ? 1 : 0) + " " + units[unit];
}
QVariant DownloadModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= tasks.size()) return {};
    auto t = task(index.row());
    const auto file = t["files"].toArray().first().toObject();
    const auto path = file["path"].toString();
    const auto status = t["status"].toString();
    const qint64 total = t["totalLength"].toString().toLongLong();
    const qint64 done = t["completedLength"].toString().toLongLong();
    const qint64 speed = t["downloadSpeed"].toString().toLongLong();
    if (role == Qt::UserRole) return total > 0 ? int(100.0 * double(done) / double(total)) : 0;
    if (role == Qt::ToolTipRole) return path + "\n" + t["errorMessage"].toString() + "\n" + t["connectionHint"].toString();
    if (role == Qt::ForegroundRole && index.column() == 1) {
        if (status == "complete") return QColor("#16805a");
        if (status == "error") return QColor("#bc3d3d");
        if (status == "retrying") return QColor("#a36813");
    }
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case 0: {
        QString name = QFileInfo(path).fileName();
        if (name.isEmpty()) name = QUrl(file["uris"].toArray().first().toObject()["uri"].toString()).fileName();
        return name.isEmpty() ? "Resolving filename…" : name;
    }
    case 1:
        if (status == "active") {
            if (speed) return "Downloading";
            if (!t["connectionHint"].toString().isEmpty()) return "Stalled — see activity log";
            return "Connecting";
        }
        if (status == "retrying") return "Retrying connection";
        if (status == "error") return "Error — select for details";
        if (status == "complete") return "Complete";
        if (status == "paused") return "Paused";
        return "Queued";
    case 2: return total > 0 ? QString::number(100.0 * double(done) / double(total), 'f', 1) + "%" : "—";
    case 3: return bytes(done) + " / " + (total ? bytes(total) : "unknown");
    case 4: return speed ? bytes(speed) + "/s" : "—";
    case 5: {
        if (!speed || total <= done) return "—";
        auto seconds = (total - done) / speed;
        return seconds >= 3600 ? QString("%1h %2m").arg(seconds / 3600).arg(seconds % 3600 / 60)
            : QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    }
    }
    return {};
}
QVariant DownloadModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return QStringList {"File", "Status", "Progress", "Downloaded", "Speed", "Remaining"}.value(section);
}
void DownloadModel::update(QJsonArray values) {
    // Keep row order stable while aria2 moves tasks between active/waiting/stopped lists.
    QJsonArray ordered;
    for (const auto &old : tasks) {
        for (int i = 0; i < values.size(); ++i) {
            if (old.toObject()["gid"] == values[i].toObject()["gid"]) {
                ordered.append(values.takeAt(i)); break;
            }
        }
    }
    for (const auto &v : values) ordered.append(v);
    bool same = tasks.size() == ordered.size();
    for (int i = 0; same && i < tasks.size(); ++i) same = tasks[i].toObject()["gid"] == ordered[i].toObject()["gid"];
    if (same) { tasks = ordered; if (!tasks.isEmpty()) emit dataChanged(index(0, 0), index(tasks.size() - 1, 5)); }
    else { beginResetModel(); tasks = ordered; endResetModel(); }
}
