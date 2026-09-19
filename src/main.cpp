#include "MainWindow.h"
#include <QApplication>
#include <QLockFile>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("AriaDownload"); app.setApplicationName("AriaDownload");
    const auto state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(state);
    QLockFile lock(state + "/app.lock");
    if (!lock.tryLock(100)) { QMessageBox::information(nullptr, "Aria Download", "Aria Download is already running, or its data folder is unavailable."); return 1; }
    MainWindow window(state); window.show();
    return app.exec();
}
