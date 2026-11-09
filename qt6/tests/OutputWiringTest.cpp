#include "BackendController.h"
#include "DeckLinkDiscovery.h"
#include "VrecordConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

#include <iostream>

namespace {

int g_failures = 0;
int g_checks = 0;

void report(const QString& name, bool passed, const QString& detail = QString()) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
    }
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name.toStdString();
    if (!detail.isEmpty()) {
        std::cout << " - " << detail.toStdString();
    }
    std::cout << std::endl;
}

void note(const QString& text) {
    std::cout << "[INFO] " << text.toStdString() << std::endl;
}

// tasklist prints "INFO: No tasks are running..." when nothing matches.
bool isImageRunning(const QString& imageName) {
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist.exe"),
                   {QStringLiteral("/NH"), QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq %1").arg(imageName)});
    tasklist.waitForFinished(5000);
    const QString out = QString::fromUtf8(tasklist.readAllStandardOutput());
    return !out.contains(QStringLiteral("INFO:"));
}

int ffmpegProcessCount() {
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist.exe"),
                   {QStringLiteral("/NH"), QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq ffmpeg-ma.exe")});
    tasklist.waitForFinished(5000);
    const QString out = QString::fromUtf8(tasklist.readAllStandardOutput());
    return out.count(QStringLiteral("ffmpeg-ma.exe"));
}

qint64 readPidFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    return QString::fromUtf8(file.readAll()).trimmed().toLongLong();
}

bool isPidAlive(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist.exe"),
                   {QStringLiteral("/NH"), QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid)});
    tasklist.waitForFinished(5000);
    const QString out = QString::fromUtf8(tasklist.readAllStandardOutput());
    return !out.contains(QStringLiteral("INFO:"));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    note(QStringLiteral("vrecord Qt6 output wiring test - this will briefly show test bars on the hardware output"));
    note(QStringLiteral("Expect: SD bars for ~3 seconds on the selected output connector"));

    QString packageRoot = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (packageRoot.isEmpty()) {
        const QString defaultRoot = QStringLiteral("C:/Users/Harry/vrecord/Release/artifacts/vrecord_windows_2026-03-03");
        if (QFileInfo::exists(QDir(defaultRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe")))) {
            packageRoot = defaultRoot;
        }
    }
    report(QStringLiteral("Package root resolved"), !packageRoot.isEmpty(), packageRoot);
    if (packageRoot.isEmpty()) {
        note(QStringLiteral("Pass the package root (containing bin/ffmpeg-ma.exe) as argument 1."));
        return 1;
    }

    BackendController backend(packageRoot);

    // 1. config load and save roundtrip
    VrecordConfig config = VrecordConfig::load(QStringLiteral("vrecord"));
    report(QStringLiteral("Config loaded"), !config.configPath.isEmpty(), config.configPath);
    report(QStringLiteral("Config save roundtrip"), config.save());
    const VrecordConfig reloaded = VrecordConfig::load(QStringLiteral("vrecord"));
    report(QStringLiteral("Config reload matches"),
           reloaded.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), QStringLiteral("Auto"))
               == config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), QStringLiteral("Auto")));

    // 2. ffmpeg backend binary present
    const QString ffmpegPath = QDir(packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    report(QStringLiteral("ffmpeg backend present"), QFileInfo::exists(ffmpegPath), ffmpegPath);

    // 3. DeckLink device enumeration and output target presence
    QString discoveryError;
    const QVector<DeckLinkDeviceInfo> devices = DeckLinkDiscovery::enumerateDevices(&discoveryError);
    report(QStringLiteral("DeckLink enumeration"), !devices.isEmpty(), discoveryError);
    QString outputDevice = config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")).trimmed();
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    }
    bool targetFound = false;
    for (const auto& device : devices) {
        if (device.modelName == outputDevice) {
            targetFound = true;
        }
    }
    report(QStringLiteral("Output device present: %1").arg(outputDevice), targetFound);

    // 4. connector selection through the DeckLink SDK with readback verification
    bool anyConnectorApplied = false;
    const QStringList connectors{QStringLiteral("SDI"),
                                 QStringLiteral("HDMI"),
                                 QStringLiteral("Component"),
                                 QStringLiteral("Composite"),
                                 QStringLiteral("S-Video")};
    for (const QString& connector : connectors) {
        QString connectionError;
        const bool applied = DeckLinkDiscovery::applyOutputConnection(outputDevice, connector, &connectionError);
        if (applied) {
            anyConnectorApplied = true;
            report(QStringLiteral("Connector %1 applied + SDK readback").arg(connector), true);
        } else if (connectionError.contains(QStringLiteral("rejected"))) {
            note(QStringLiteral("Connector %1 not supported by device - skipped").arg(connector));
        } else {
            report(QStringLiteral("Connector %1 applied + SDK readback").arg(connector), false, connectionError);
        }
    }
    report(QStringLiteral("At least one connector applied through the SDK"), anyConnectorApplied);

    // restore the configured connector before the signal cycle - an analog
    // output cannot carry the HD/SD mode the signal cycle uses
    QString restoreConnector = config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE")).trimmed();
    if (restoreConnector.isEmpty() || restoreConnector == QStringLiteral("Auto")) {
        restoreConnector = QStringLiteral("HDMI");
    }
    QString restoreError;
    const bool restored = DeckLinkDiscovery::applyOutputConnection(outputDevice, restoreConnector, &restoreError);
    note(restored
             ? QStringLiteral("Restored output connector to %1").arg(restoreConnector)
             : QStringLiteral("Connector restore to %1 failed: %2").arg(restoreConnector, restoreError));

    // 5. test signal start / liveness / stop cycle
    const int ffmpegBaseline = ffmpegProcessCount();
    note(QStringLiteral("ffmpeg-ma processes before cycle: %1").arg(ffmpegBaseline));

    QString signalError;
    const bool started = backend.setTestSignalOutput(config, true, &signalError);
    report(QStringLiteral("Test signal start"), started, signalError);

    const qint64 signalPid = readPidFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.pid")));
    report(QStringLiteral("Test signal PID file written"), signalPid > 0, QString::number(signalPid));

    QThread::msleep(3000);
    const bool launcherAlive = isPidAlive(signalPid);
    const int ffmpegDuring = ffmpegProcessCount();
    report(QStringLiteral("Test signal streaming"),
           launcherAlive || ffmpegDuring > ffmpegBaseline,
           QStringLiteral("launcher alive=%1 ffmpeg count=%2").arg(launcherAlive ? "yes" : "no").arg(ffmpegDuring));

    QString stopError;
    const bool stopped = backend.setTestSignalOutput(config, false, &stopError);
    report(QStringLiteral("Test signal stop call"), stopped, stopError);
    QThread::msleep(1500);
    const bool launcherGone = !isPidAlive(signalPid);
    const int ffmpegAfter = ffmpegProcessCount();
    report(QStringLiteral("Test signal fully stopped"),
           launcherGone && ffmpegAfter <= ffmpegBaseline,
           QStringLiteral("launcher gone=%1 ffmpeg count=%2").arg(launcherGone ? "yes" : "no").arg(ffmpegAfter));

    // 5b. sweep every test pattern under an SD and an HD standard - each mode
    // must actually stream (ffmpeg stays alive; the decklink muxer rejects bad
    // modes within about a second) and stop cleanly afterwards
    const QString originalStandard = config.value(QStringLiteral("STANDARD_CHOICE"), QStringLiteral("NTSC"));
    const QString originalPattern = config.value(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), QStringLiteral("SMPTE SD Bars (75%)"));
    const QStringList patterns{QStringLiteral("SMPTE SD Bars (75%)"),
                               QStringLiteral("SMPTE SD Bars (100%)"),
                               QStringLiteral("EBU Bars (75%)"),
                               QStringLiteral("EBU Bars (100%)"),
                               QStringLiteral("SMPTE RP 219 \"HDTV\" Bars (75%)"),
                               QStringLiteral("SMPTE RP 219 \"UHDTV\" Bars (75%, BT.2020)"),
                               QStringLiteral("BT.2111 HDR Bars (HLG 1080p)"),
                               QStringLiteral("BT.2111 HDR Bars (PQ 1080p)"),
                               QStringLiteral("BT.2111 HDR Bars (HLG 2160p)"),
                               QStringLiteral("BT.2111 HDR Bars (PQ 2160p)"),
                               QStringLiteral("FFmpeg Bars"),
                               QStringLiteral("Test Pattern"),
                               QStringLiteral("RGB Bars"),
                               QStringLiteral("YUV Bars"),
                               QStringLiteral("Black"),
                               QStringLiteral("White"),
                               QStringLiteral("PLUGE"),
                               QStringLiteral("Grid")};
    const QStringList sweepStandards{QStringLiteral("NTSC"), QStringLiteral("Hp29 - 1080p29.97")};
    int sweepFailures = 0;
    for (const QString& sweepStandard : sweepStandards) {
        for (const QString& pattern : patterns) {
            config.setValue(QStringLiteral("STANDARD_CHOICE"), sweepStandard);
            config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), pattern);
            QString sweepError;
            const bool sweepStarted = backend.setTestSignalOutput(config, true, &sweepError);
            bool streaming = false;
            QString failureDetail;
            if (sweepStarted) {
                QThread::msleep(2000);
                streaming = ffmpegProcessCount() > 0;
            }
            if (!streaming) {
                QFile sweepLogFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.log")));
                if (sweepLogFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    const QStringList logLines = QString::fromUtf8(sweepLogFile.readAll()).trimmed().split('\n');
                    if (!logLines.isEmpty() && !logLines.last().trimmed().isEmpty()) {
                        failureDetail = logLines.last().trimmed();
                    }
                }
            }
            const bool modeOk = sweepStarted && streaming;
            if (!modeOk) {
                ++sweepFailures;
            }
            report(QStringLiteral("Mode %1 + %2").arg(sweepStandard, pattern),
                   modeOk,
                   modeOk ? QStringLiteral("streaming")
                          : (failureDetail.isEmpty() ? sweepError : failureDetail));
            QString stopSweepError;
            backend.setTestSignalOutput(config, false, &stopSweepError);
            QThread::msleep(600);
        }
    }
    report(QStringLiteral("Every pattern streamed under SD and HD standards"),
           sweepFailures == 0,
           QStringLiteral("%1 mode failures").arg(sweepFailures));
    config.setValue(QStringLiteral("STANDARD_CHOICE"), originalStandard);
    config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), originalPattern);

    // 6. file playout error path (no file selected must fail with a clear message)
    QString filePlayError;
    const bool filePlayStarted = backend.startFileOutput(config, &filePlayError);
    report(QStringLiteral("File playout rejects empty play file"),
           !filePlayStarted && !filePlayError.isEmpty(), filePlayError);

    // 7. stop-all path
    QString stopAllError;
    report(QStringLiteral("Stop-all outputs"), backend.stopAllOutputs(config, &stopAllError), stopAllError);

    note(QStringLiteral("%1 checks, %2 failures").arg(g_checks).arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
