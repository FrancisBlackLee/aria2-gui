#pragma once
#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonObject>

class DownloadModel : public QAbstractTableModel {
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : tasks.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void update(QJsonArray values);
    QJsonObject task(int row) const { return row >= 0 && row < tasks.size() ? tasks[row].toObject() : QJsonObject {}; }
    static QString bytes(qint64 value);
private:
    QJsonArray tasks;
};
