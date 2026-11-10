#include "MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vrecord-qt6"));
    QApplication::setOrganizationName(QStringLiteral("vrecord"));
    // the vrecord icon of the GTK launcher (also the exe icon via vrecord.rc)
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/vrecord.ico")));

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString startupLogPath = QDir(appDir).filePath(QStringLiteral("VrecordQt6-startup.log"));
    QFile startupLog(startupLogPath);
    startupLog.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    QTextStream log(&startupLog);
    const auto writeLog = [&log](const QString& message) {
        log << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << message << '\n';
        log.flush();
    };
    writeLog(QStringLiteral("startup"));
    writeLog(QStringLiteral("appDir=%1").arg(QDir::toNativeSeparators(appDir)));
    QString packageRoot = appDir;
    // the package root holds the vrecord shell backend and bin/ffmpeg-ma.exe
    if (QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("vrecord")))
        || QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("vrecord.cmd")))) {
        packageRoot = appDir;
    } else if (QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("../vrecord")))
               || QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("../vrecord.cmd")))) {
        packageRoot = QFileInfo(QDir(appDir).filePath(QStringLiteral(".."))).absoluteFilePath();
    }
    writeLog(QStringLiteral("packageRoot=%1").arg(QDir::toNativeSeparators(packageRoot)));

    bool selfTestMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == QByteArray("--self-test")) {
            selfTestMode = true;
        }
    }
    if (selfTestMode) {
        MainWindow window(packageRoot);
        const QStringList results = window.runSelfTest();
        int failures = 0;
        QFile reportFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-selftest.log")));
        reportFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
        QTextStream report(&reportFile);
        for (const QString& line : results) {
            report << line << '\n';
            if (line.startsWith(QStringLiteral("FAIL"))) {
                ++failures;
            }
        }
        writeLog(QStringLiteral("self-test complete: %1 failures").arg(failures));
        return failures == 0 ? 0 : 1;
    }

    MainWindow window(packageRoot);
    writeLog(QStringLiteral("window_constructed"));
    window.show();
    window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    window.raise();
    window.activateWindow();
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QRect frame = window.frameGeometry();
        window.move(available.center() - frame.center());
    }
    QTimer::singleShot(0, [&window, writeLog] {
        window.showNormal();
        window.raise();
        window.activateWindow();
        if (const QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect available = screen->availableGeometry();
            const QRect frame = window.frameGeometry();
            window.move(available.center() - frame.center());
        }
        writeLog(QStringLiteral("window_shown"));
    });
    writeLog(QStringLiteral("enter_event_loop"));
    return app.exec();
}
