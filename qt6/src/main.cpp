#include "MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
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
    QString startTab;
    QString updateTestApi;
    bool updateTestExpectFail = false;
    bool updateTestApply = false;
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == QByteArray("--self-test")) {
            selfTestMode = true;
        } else if (QByteArray(argv[i]) == QByteArray("--tab") && i + 1 < argc) {
            startTab = QString::fromLocal8Bit(argv[++i]);
        } else if (QByteArray(argv[i]) == QByteArray("--update-test") && i + 1 < argc) {
            updateTestApi = QString::fromLocal8Bit(argv[++i]);
        } else if (QByteArray(argv[i]) == QByteArray("--expect-fail")) {
            updateTestExpectFail = true;
        } else if (QByteArray(argv[i]) == QByteArray("--apply")) {
            updateTestApply = true;
        }
    }
    // --update-test <release JSON url> [--expect-fail] [--apply]: the updater
    // end to end without a window - check, download, SHA256 verify, unpack
    // (portable); --apply then starts the real install helper. Report:
    // VrecordQt6-update-test.log; exit 0 when the outcome is the expected one.
    if (!updateTestApi.isEmpty()) {
        qputenv("VRECORD_UPDATE_API", updateTestApi.toUtf8());
        QFile reportFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-update-test.log")));
        reportFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
        QTextStream report(&reportFile);
        const auto say = [&report](const QString& line) { report << line << '\n'; report.flush(); };
        UpdateManager updates(packageRoot);
        say(QStringLiteral("version=%1 kind=%2 api=%3").arg(UpdateManager::currentVersion(), updates.installKind(), updateTestApi));
        bool succeeded = false;
        QEventLoop loop;
        QObject::connect(&updates, &UpdateManager::checkFinished, &loop,
                         [&](bool ok, bool available, const UpdateInfo& info, const QString& message) {
            say(QStringLiteral("check ok=%1 available=%2 tag=%3 asset=%4: %5")
                    .arg(int(ok)).arg(int(available)).arg(info.tag, info.assetName, message));
            if (ok && available) {
                updates.download(info);
            } else {
                loop.quit();
            }
        });
        QObject::connect(&updates, &UpdateManager::downloadFinished, &loop, [&](bool ok, const QString& message) {
            say(QStringLiteral("download ok=%1: %2").arg(int(ok)).arg(message));
            succeeded = ok;
            if (ok && updateTestApply) {
                QString error;
                const bool started = updates.launchInstall(QCoreApplication::applicationPid(), false, &error);
                say(QStringLiteral("apply started=%1 %2").arg(int(started)).arg(error));
                say(QStringLiteral("apply-log=%1").arg(QDir::fromNativeSeparators(updates.applyLogPath())));
                succeeded = started;
            }
            loop.quit();
        });
        QTimer::singleShot(0, &updates, [&updates] { updates.check(true); });
        QTimer::singleShot(600000, &loop, [&] { say(QStringLiteral("timed out")); loop.quit(); });
        loop.exec();
        const bool expected = updateTestExpectFail ? !succeeded : succeeded;
        say(QStringLiteral("%1: update %2 (expected %3)")
                .arg(expected ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                     succeeded ? QStringLiteral("succeeded") : QStringLiteral("refused/failed"),
                     updateTestExpectFail ? QStringLiteral("refusal") : QStringLiteral("success")));
        return expected ? 0 : 1;
    }
    if (selfTestMode) {
        // test mode BEFORE the window exists: its constructor must not start
        // the live preview (CI: no card) or anything the self-test drives
        BackendController::setTestMode(true);
        QFile::remove(QDir(appDir).filePath(QStringLiteral("VrecordQt6-selftest-trace.log")));
        writeLog(QStringLiteral("self-test: constructing window"));
        MainWindow window(packageRoot);
        writeLog(QStringLiteral("self-test: running checks"));
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
    if (!startTab.isEmpty()) {
        window.selectTab(startTab);
    }
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
