#include "MainWindow.h"
#include <QtWidgets>
#include <QDesktopServices>

MainWindow::MainWindow(const QString &stateDirectory) : backend(stateDirectory, this), model(this) {
    setWindowTitle("Aria Download"); resize(1080, 680); setMinimumSize(820, 520);
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(28, 24, 28, 20); layout->setSpacing(16);
    auto *title = new QLabel("Aria Download"); title->setStyleSheet("font-size: 26px; font-weight: 600;"); layout->addWidget(title);
    auto *subtitle = new QLabel("Large files. Steady downloads. Automatic reconnection."); subtitle->setStyleSheet("color: #64748b;"); layout->addWidget(subtitle);
    auto *form = new QGridLayout; form->setHorizontalSpacing(12); form->setVerticalSpacing(10);
    url = new QLineEdit; url->setPlaceholderText("Paste a direct file link — https://zenodo.org/records/…/files/…"); url->setClearButtonEnabled(true); url->setObjectName("urlInput");
    folder = new QLineEdit; folder->setObjectName("folderInput");
    QSettings settings;
    folder->setText(settings.value("folder", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString());
    auto *browse = new QPushButton("Browse…");
    form->addWidget(new QLabel("File URL"), 0, 0); form->addWidget(url, 0, 1, 1, 3);
    form->addWidget(new QLabel("Save to"), 1, 0); form->addWidget(folder, 1, 1, 1, 2); form->addWidget(browse, 1, 3);
    auto *options = new QHBoxLayout;
    connections = new QSpinBox; connections->setRange(1, 16); connections->setValue(settings.value("connections", 8).toInt());
    connections->setToolTip("Connections per file. Try 1–4 if a server limits parallel requests.");
    options->addWidget(new QLabel("Connections per file")); options->addWidget(connections); options->addStretch();
    add = new QPushButton("+  Add download"); add->setObjectName("primary"); add->setEnabled(false); options->addWidget(add);
    form->addLayout(options, 2, 1, 1, 3); layout->addLayout(form);
    auto *toolbar = new QHBoxLayout;
    pause = new QPushButton("Pause"); resume = new QPushButton("Resume / Retry"); remove = new QPushButton("Remove"); open = new QPushButton("Open folder");
    remove->setToolTip("Remove the task; keep downloaded files on disk.");
    for (auto *b : {pause, resume, remove, open}) toolbar->addWidget(b);
    toolbar->addStretch(); layout->addLayout(toolbar);
    table = new QTableView; table->setModel(&model); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setShowGrid(false); table->setAlternatingRowColors(true);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(46);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    details = new QLabel("Paste a file URL to start. Up to 3 files download at once."); details->setWordWrap(true); details->setTextFormat(Qt::PlainText); details->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(details);
    auto *footer = new QHBoxLayout; status = new QLabel("Starting aria2…"); footer->addWidget(status); footer->addStretch();
    auto *showLog = new QPushButton("Activity log"); showLog->setCheckable(true); footer->addWidget(showLog); layout->addLayout(footer);
    log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(300); log->setMaximumHeight(130); log->hide(); layout->addWidget(log);
    setCentralWidget(root);
    setStyleSheet("QMainWindow { background: #f5f7fb; } QWidget { font-family: 'Segoe UI'; font-size: 13px; color: #25334a; } QLineEdit, QSpinBox { background: white; border: 1px solid #cbd5e1; border-radius: 5px; padding: 8px; } QPushButton { background: white; border: 1px solid #cbd5e1; border-radius: 5px; padding: 8px 14px; } QPushButton:hover { background: #eaf0fa; } QPushButton:disabled { color: #94a3b8; background: #f1f5f9; } QPushButton#primary { background: #2563eb; color: white; border: none; } QPushButton#primary:disabled { background: #94a3b8; } QTableView { background: white; alternate-background-color: #f8fafc; border: 1px solid #dbe2ea; border-radius: 6px; selection-background-color: #dbeafe; selection-color: #173c70; } QHeaderView::section { background: #edf2f8; border: none; padding: 10px; font-weight: 600; }");
    connect(showLog, &QPushButton::toggled, log, &QWidget::setVisible);
    connect(browse, &QPushButton::clicked, this, [this] { auto path = QFileDialog::getExistingDirectory(this, "Download folder", folder->text()); if (!path.isEmpty()) folder->setText(path); });
    connect(add, &QPushButton::clicked, this, [this] {
        auto validation = AriaBackend::validateUrl(url->text().trimmed());
        if (!validation.isEmpty()) { QMessageBox::information(this, "File URL", validation); return; }
        QSettings s; s.setValue("folder", folder->text()); s.setValue("connections", connections->value());
        backend.add(url->text().trimmed(), folder->text(), connections->value());
    });
    connect(url, &QLineEdit::returnPressed, add, &QPushButton::click);
    connect(pause, &QPushButton::clicked, this, [this] { backend.pause(selected()["gid"].toString()); });
    connect(resume, &QPushButton::clicked, this, [this] {
        auto task = selected(); if (task["status"] == "error") backend.retry(task); else backend.resume(task["gid"].toString());
    });
    connect(remove, &QPushButton::clicked, this, [this] { backend.remove(selected()["gid"].toString()); });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(selected()["dir"].toString())); });
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(&backend, &AriaBackend::tasksChanged, this, [this](QJsonArray tasks) {
        const auto gid = selected()["gid"].toString(); model.update(tasks);
        for (int i = 0; i < model.rowCount(); ++i) if (model.task(i)["gid"] == gid) { table->selectRow(i); break; }
        updateActions();
    });
    connect(&backend, &AriaBackend::readyChanged, this, [this](bool ready) { add->setEnabled(ready); status->setText(ready ? "●  Ready  ·  Auto retry on  ·  Downloads saved every 5 seconds" : "Backend offline"); updateActions(); });
    connect(&backend, &AriaBackend::message, log, &QPlainTextEdit::appendPlainText);
    connect(&backend, &AriaBackend::transferStalled, showLog, [showLog] { showLog->setChecked(true); });
    connect(&backend, &AriaBackend::error, this, [this](QString text) { log->appendPlainText(text); details->setText(text); status->setText("Action needed — see details / activity log"); });
    if (settings.contains("geometry")) restoreGeometry(settings.value("geometry").toByteArray());
    updateActions(); QTimer::singleShot(0, &backend, &AriaBackend::start);
}
QJsonObject MainWindow::selected() const { return model.task(table->currentIndex().row()); }
void MainWindow::updateActions() {
    const auto task = selected(); auto state = task["status"].toString(); bool ready = backend.isReady();
    pause->setEnabled(ready && (state == "active" || state == "waiting"));
    resume->setEnabled(ready && (state == "paused" || state == "error"));
    remove->setEnabled(ready && !task.isEmpty()); open->setEnabled(!task.isEmpty());
    if (!task.isEmpty()) {
        auto path = task["files"].toArray().first().toObject()["path"].toString();
        auto error = task["errorMessage"].toString();
        const auto hint = task["connectionHint"].toString();
        details->setText(path + (error.isEmpty() ? "" : "\n" + error) + (hint.isEmpty() ? "" : "\n" + hint));
    }
}
void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings s; s.setValue("geometry", saveGeometry()); s.setValue("folder", folder->text()); s.setValue("connections", connections->value());
    setEnabled(false); status->setText("Saving downloads…"); backend.stop(); event->accept();
}
