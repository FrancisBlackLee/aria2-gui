#pragma once
#include <QMainWindow>
#include "AriaBackend.h"
#include "DownloadModel.h"
class QLineEdit;
class QSpinBox;
class QTableView;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &stateDirectory);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void updateActions();
    QJsonObject selected() const;
    AriaBackend backend;
    DownloadModel model;
    QLineEdit *url, *folder;
    QSpinBox *connections;
    QTableView *table;
    QLabel *status, *details;
    QPushButton *add, *pause, *resume, *remove, *open;
    QPlainTextEdit *log;
};
