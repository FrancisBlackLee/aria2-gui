#include "MainWindow.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QTableView>
#include <QLineEdit>
#include <QSettings>
#include <QFontDatabase>
class GuiTests : public QObject {
    Q_OBJECT
private slots:
    void window() {
        // The offscreen Windows platform does not enumerate system fonts.
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("SystemRoot") + "/Fonts/segoeui.ttf");
#endif
        QTemporaryDir state;
        QCoreApplication::setOrganizationName("AriaDownloadTests");
        QCoreApplication::setApplicationName("AriaDownloadTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, state.path());
        MainWindow window(state.path()); window.show();
        auto *button = window.findChild<QPushButton *>("primary");
        QVERIFY(button); QVERIFY(!button->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 15000);
        auto *input = window.findChild<QLineEdit *>("urlInput");
        QVERIFY(input);
        QTest::keyClicks(input, "https://example.com/dataset.zip");
        QCOMPARE(input->text(), QString("https://example.com/dataset.zip"));
        auto *table = window.findChild<QTableView *>(); QVERIFY(table);
        QCOMPARE(table->model()->columnCount(), 6);
        QVERIFY(window.grab().save("gui-smoke.png"));
        window.close();
    }
};
QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
