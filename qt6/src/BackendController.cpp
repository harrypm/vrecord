#include "BackendController.h"

#include "DeckLinkDiscovery.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

namespace {
bool s_testMode = false;
QStringList s_captured;

struct SignalMode {
    QString size;
    QString rate;
    QString fieldOrder;
};

QString rateForFamily(const QString& standardChoice) {
    if (standardChoice.endsWith(QStringLiteral("23.98"))) return QStringLiteral("24000/1001");
    if (standardChoice.endsWith(QStringLiteral("24"))) return QStringLiteral("24/1");
    if (standardChoice.endsWith(QStringLiteral("25"))) return QStringLiteral("25/1");
    if (standardChoice.endsWith(QStringLiteral("29.97"))) return QStringLiteral("30000/1001");
    if (standardChoice.endsWith(QStringLiteral("30"))) return QStringLiteral("30/1");
    if (standardChoice.endsWith(QStringLiteral("50"))) return QStringLiteral("50/1");
    if (standardChoice.endsWith(QStringLiteral("59.94"))) return QStringLiteral("60000/1001");
    if (standardChoice.endsWith(QStringLiteral("60"))) return QStringLiteral("60/1");
    return QStringLiteral("25/1");
}

SignalMode resolveSignalMode(const QString& standardChoice) {
    if (standardChoice == QStringLiteral("PAL")) return {QStringLiteral("720x576"), QStringLiteral("25/1"), QStringLiteral("tff")};
    if (standardChoice == QStringLiteral("23ps - 1080p23.98")) return {QStringLiteral("1920x1080"), QStringLiteral("24000/1001"), {}};
    if (standardChoice == QStringLiteral("24ps - 1080p24")) return {QStringLiteral("1920x1080"), QStringLiteral("24/1"), {}};
    if (standardChoice == QStringLiteral("Hp25") || standardChoice == QStringLiteral("Hp25 - 1080p25")) return {QStringLiteral("1920x1080"), QStringLiteral("25/1"), {}};
    if (standardChoice == QStringLiteral("Hp29 - 1080p29.97")) return {QStringLiteral("1920x1080"), QStringLiteral("30000/1001"), {}};
    if (standardChoice == QStringLiteral("Hp30") || standardChoice == QStringLiteral("Hp30 - 1080p30")) return {QStringLiteral("1920x1080"), QStringLiteral("30/1"), {}};
    if (standardChoice == QStringLiteral("Hi50 - 1080i50")) return {QStringLiteral("1920x1080"), QStringLiteral("25/1"), QStringLiteral("tff")};
    if (standardChoice == QStringLiteral("Hi59") || standardChoice == QStringLiteral("Hi59 - 1080i59.94")) return {QStringLiteral("1920x1080"), QStringLiteral("30000/1001"), QStringLiteral("tff")};
    if (standardChoice == QStringLiteral("Hi60 - 1080i60")) return {QStringLiteral("1920x1080"), QStringLiteral("30/1"), QStringLiteral("tff")};
    if (standardChoice == QStringLiteral("hp50 - 720p50")) return {QStringLiteral("1280x720"), QStringLiteral("50/1"), {}};
    if (standardChoice == QStringLiteral("hp59 - 720p59.94")) return {QStringLiteral("1280x720"), QStringLiteral("60000/1001"), {}};
    if (standardChoice == QStringLiteral("hp60 - 720p60")) return {QStringLiteral("1280x720"), QStringLiteral("60/1"), {}};
    if (standardChoice == QStringLiteral("Hp50 - 1080p50")) return {QStringLiteral("1920x1080"), QStringLiteral("50/1"), {}};
    if (standardChoice == QStringLiteral("Hp59 - 1080p59.94")) return {QStringLiteral("1920x1080"), QStringLiteral("60000/1001"), {}};
    if (standardChoice == QStringLiteral("Hp60 - 1080p60")) return {QStringLiteral("1920x1080"), QStringLiteral("60/1"), {}};
    if (standardChoice.startsWith(QStringLiteral("2d"))) return {QStringLiteral("2048x1080"), rateForFamily(standardChoice), {}};
    if (standardChoice.startsWith(QStringLiteral("4k"))) return {QStringLiteral("3840x2160"), rateForFamily(standardChoice), {}};
    if (standardChoice.startsWith(QStringLiteral("4d"))) return {QStringLiteral("4096x2160"), rateForFamily(standardChoice), {}};
    return {QStringLiteral("720x486"), QStringLiteral("30000/1001"), QStringLiteral("bff")};
}

QString lockedPatternSize(const QString& pattern) {
    if (pattern.contains(QStringLiteral("UHD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("2160p"), Qt::CaseInsensitive)) {
        return QStringLiteral("3840x2160");
    }
    if (pattern.contains(QStringLiteral("HD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("1080p"), Qt::CaseInsensitive)) {
        return QStringLiteral("1920x1080");
    }
    return {};
}

QString testVideoSourceForPattern(const QString& pattern, const SignalMode& mode) {
    const QString size = lockedPatternSize(pattern).isEmpty() ? mode.size : lockedPatternSize(pattern);
    if (pattern.contains(QStringLiteral("black"), Qt::CaseInsensitive)) {
        return QStringLiteral("color=c=black:size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("white"), Qt::CaseInsensitive)) {
        return QStringLiteral("color=c=white:size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("grid"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("test pattern"), Qt::CaseInsensitive)) {
        return QStringLiteral("testsrc2=size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("pluge"), Qt::CaseInsensitive)) {
        return QStringLiteral("testsrc=size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("rgb"), Qt::CaseInsensitive)) {
        return QStringLiteral("rgbtestsrc=size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("yuv"), Qt::CaseInsensitive)) {
        return QStringLiteral("yuvtestsrc=size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive)) {
        return QStringLiteral("testsrc=size=%1:rate=%2").arg(size, mode.rate);
    }
    if (pattern.contains(QStringLiteral("HD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("UHD"), Qt::CaseInsensitive)
        || pattern.contains(QStringLiteral("BT.2111"), Qt::CaseInsensitive)) {
        return QStringLiteral("smptehdbars=size=%1:rate=%2").arg(size, mode.rate);
    }
    return QStringLiteral("smptebars=size=%1:rate=%2").arg(size, mode.rate);
}

QStringList videoFilterChainForPattern(const QString& pattern, const SignalMode& mode) {
    QStringList filters;
    // locked HD/UHD patterns output progressive at their specified raster:
    // the shell backend forces TS_FIELD empty for them, so must we
    const bool patternLocked = !lockedPatternSize(pattern).isEmpty();
    if (!patternLocked && !mode.fieldOrder.isEmpty()) {
        filters << QStringLiteral("setfield=%1").arg(mode.fieldOrder);
    }
    if (pattern.contains(QStringLiteral("UHD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("BT.2020"), Qt::CaseInsensitive)
        || pattern.contains(QStringLiteral("BT.2111"), Qt::CaseInsensitive)) {
        filters << QStringLiteral("scale=out_color_matrix=bt2020nc");
    } else if (pattern.contains(QStringLiteral("HD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("1080p"), Qt::CaseInsensitive)) {
        filters << QStringLiteral("scale=out_color_matrix=bt709");
    }
    return filters;
}

// tasklist prints "INFO: No tasks are running..." when nothing matches.
bool pidAlive(qint64 pid) {
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

QString lastLogFileLine(const QString& logPath) {
    QFile logFile(logPath);
    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList lines = QString::fromUtf8(logFile.readAll()).trimmed().split('\n');
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

QString testAudioSource(const QString& audioChoice) {
    if (audioChoice.contains(QStringLiteral("silence"), Qt::CaseInsensitive)) {
        return QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000");
    }
    if (audioChoice.contains(QStringLiteral("stereo"), Qt::CaseInsensitive) || audioChoice.contains(QStringLiteral("440"), Qt::CaseInsensitive)) {
        return QStringLiteral("aevalsrc=sin(1000*2*PI*t)|cos(440*2*PI*t):channel_layout=stereo:sample_rate=48000");
    }
    return QStringLiteral("aevalsrc=sin(1000*2*PI*t)|sin(1000*2*PI*t):channel_layout=stereo:sample_rate=48000");
}

QString quoteForBash(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(value);
}

QString toBashPath(QString value) {
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (value.size() >= 3 && value[1] == QLatin1Char(':') && value[2] == QLatin1Char('/')) {
        const QChar drive = value[0].toLower();
        return QStringLiteral("/%1/%2").arg(drive, value.mid(3));
    }
    return value;
}

QString findBashExecutable() {
    const QString msysBash = QStringLiteral("C:/msys64/usr/bin/bash.exe");
    if (QFileInfo::exists(msysBash)) {
        return msysBash;
    }
    const QString gitBash = QDir::toNativeSeparators(QStringLiteral("%1/Git/bin/bash.exe").arg(qEnvironmentVariable("ProgramFiles")));
    if (QFileInfo::exists(gitBash)) {
        return gitBash;
    }
    return {};
}
}

BackendController::BackendController(const QString& packageRoot, QObject* parent)
    : QObject(parent)
    , m_packageRoot(packageRoot) {}

QString BackendController::packageRoot() const {
    return m_packageRoot;
}

bool BackendController::saveConfig(const VrecordConfig& config, QString* errorMessage) const {
    if (config.save()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to save configuration to %1").arg(QDir::toNativeSeparators(config.configPath));
    }
    return false;
}

bool BackendController::startRecord(const VrecordConfig& config, QString* errorMessage) const {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }
    return startProcess({QStringLiteral("-r"), QStringLiteral("-c"), config.configPath, config.value(QStringLiteral("ID"))}, errorMessage);
}

bool BackendController::startPassthrough(const VrecordConfig& config, QString* errorMessage) const {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }
    return startProcess({QStringLiteral("-p"), QStringLiteral("-c"), config.configPath}, errorMessage);
}

bool BackendController::startAudioCheck(const VrecordConfig& config, QString* errorMessage) const {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }
    return startProcess({QStringLiteral("-a"), QStringLiteral("-c"), config.configPath}, errorMessage);
}
bool BackendController::startFileOutput(const VrecordConfig& config, QString* errorMessage) const {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }

    const QString playFile = config.value(QStringLiteral("VRECORD_PLAY_FILE")).trimmed();
    if (playFile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a file to play to the output device first.");
        }
        return false;
    }
    if (!QFileInfo::exists(playFile)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected playout file does not exist: %1").arg(QDir::toNativeSeparators(playFile));
        }
        return false;
    }

    QString outputDevice = config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")).trimmed();
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a hardware video output device first.");
        }
        return false;
    }

    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    if (!QFileInfo::exists(ffmpegPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffmpeg backend at %1").arg(QDir::toNativeSeparators(ffmpegPath));
        }
        return false;
    }

    QStringList arguments{
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-stream_loop"), QStringLiteral("-1"),
        QStringLiteral("-i"), playFile,
        QStringLiteral("-c:v"), QStringLiteral("v210"),
        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
        QStringLiteral("-ar"), QStringLiteral("48000"),
        QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-f"), QStringLiteral("decklink"),
        outputDevice
    };
    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }
    stopTrackedProcess(filePlayPidPath(), nullptr);
    qint64 pid = 0;
    if (!startDetachedProgramWithLog(ffmpegPath, arguments, filePlayLogPath(), &pid, errorMessage)) {
        return false;
    }
    QFile pidFile(filePlayPidPath());
    if (pidFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&pidFile);
        out << pid << '\n';
    }
    return true;
}

bool BackendController::stopFileOutput(const VrecordConfig& config, QString* errorMessage) const {
    Q_UNUSED(config);
    return stopTrackedProcess(filePlayPidPath(), errorMessage);
}

bool BackendController::stopAllOutputs(const VrecordConfig& config, QString* errorMessage) const {
    Q_UNUSED(config);
    if (!stopTrackedProcess(testSignalPidPath(), errorMessage)) {
        return false;
    }
    return stopTrackedProcess(filePlayPidPath(), errorMessage);
}

bool BackendController::applyVideoOutputConnection(const VrecordConfig& config, QString* errorMessage) const {
    const QString connection = config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE")).trimmed();
    if (connection.isEmpty() || connection == QStringLiteral("Auto")) {
        return true;
    }
    QString outputDevice = config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")).trimmed();
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a hardware video output device first.");
        }
        return false;
    }
    return DeckLinkDiscovery::applyOutputConnection(outputDevice, connection, errorMessage);
}

bool BackendController::setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage) const {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }

    const QString pidPath = testSignalPidPath();
    if (!enabled) {
        return stopTrackedProcess(pidPath, errorMessage);
    }

    QString outputDevice = config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")).trimmed();
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        outputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    }
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a hardware video output device first.");
        }
        return false;
    }

    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    if (!QFileInfo::exists(ffmpegPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffmpeg backend at %1").arg(QDir::toNativeSeparators(ffmpegPath));
        }
        return false;
    }

    const SignalMode signalMode = resolveSignalMode(config.value(QStringLiteral("STANDARD_CHOICE")).trimmed());
    const QString pattern = config.value(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE")).trimmed();
    const QString audio = config.value(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE")).trimmed();
    const QString videoSource = testVideoSourceForPattern(pattern, signalMode);
    const QString audioSource = testAudioSource(audio);
    const QStringList videoFilters = videoFilterChainForPattern(pattern, signalMode);

    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }

    stopTrackedProcess(pidPath, nullptr);

    QStringList arguments;
    arguments << QStringLiteral("-nostdin")
              << QStringLiteral("-loglevel") << QStringLiteral("info")
              << QStringLiteral("-re")
              << QStringLiteral("-f") << QStringLiteral("lavfi")
              << QStringLiteral("-i") << videoSource
              << QStringLiteral("-f") << QStringLiteral("lavfi")
              << QStringLiteral("-i") << audioSource
              << QStringLiteral("-map") << QStringLiteral("0:v")
              << QStringLiteral("-map") << QStringLiteral("1:a");
    if (!videoFilters.isEmpty()) {
        arguments << QStringLiteral("-vf") << videoFilters.join(QLatin1Char(','));
    }
    arguments << QStringLiteral("-metadata") << QStringLiteral("comment=vrecord-test-signal")
              << QStringLiteral("-c:v") << QStringLiteral("v210")
              << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
              << QStringLiteral("-ar") << QStringLiteral("48000")
              << QStringLiteral("-ac") << QStringLiteral("2")
              << QStringLiteral("-f") << QStringLiteral("decklink")
              << outputDevice;

    qint64 pid = 0;
    if (!startDetachedProgramWithLog(ffmpegPath, arguments, testSignalLogPath(), &pid, errorMessage)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start test signal output.");
        }
        return false;
    }

    if (s_testMode) {
        return true;
    }

    QFile pidFile(pidPath);
    if (pidFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&pidFile);
        out << pid << '\n';
    }

    QEventLoop waitLoop;
    QTimer::singleShot(1500, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();
    if (!pidAlive(pid)) {
        const QString logTail = lastLogFileLine(testSignalLogPath());
        QFile::remove(pidPath);
        if (errorMessage) {
            *errorMessage = logTail.isEmpty()
                ? QStringLiteral("Test signal output exited immediately - see VrecordQt6-testsignal.log")
                : QStringLiteral("Test signal output failed: %1").arg(logTail);
        }
        return false;
    }
    return true;
}

bool BackendController::openLegacyUi(QString* errorMessage) const {
    return startProcess({QStringLiteral("-e")}, errorMessage);
}

bool BackendController::startProcess(const QStringList& arguments, QString* errorMessage) const {
    const QString cmdPath = QDir(m_packageRoot).filePath(QStringLiteral("vrecord.cmd"));
    const QString shPath = QDir(m_packageRoot).filePath(QStringLiteral("vrecord.sh"));
    QString program;
    QStringList finalArgs;

    if (QFileInfo::exists(cmdPath)) {
        program = QStringLiteral("cmd.exe");
        finalArgs << QStringLiteral("/c") << cmdPath;
    } else if (QFileInfo::exists(shPath)) {
        program = shPath;
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find vrecord backend entrypoint beside the Qt frontend.");
        }
        return false;
    }
    finalArgs << arguments;
    return startDetachedProgram(program, finalArgs, errorMessage);
}

void BackendController::setTestMode(bool enabled) {
    s_testMode = enabled;
}

bool BackendController::testMode() {
    return s_testMode;
}

QStringList BackendController::capturedCommands() {
    return s_captured;
}

void BackendController::clearCaptured() {
    s_captured.clear();
}

bool BackendController::capturedAnyContaining(const QString& needle) {
    for (const QString& command : s_captured) {
        if (command.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool BackendController::startDetachedProgram(const QString& program, const QStringList& arguments, QString* errorMessage) const {
    if (s_testMode) {
        s_captured << (program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
        return true;
    }
    Q_UNUSED(errorMessage);
    return QProcess::startDetached(program, arguments, m_packageRoot);
}
bool BackendController::startDetachedProgramWithLog(const QString& program, const QStringList& arguments, const QString& logPath, qint64* pid, QString* errorMessage) const {
    if (s_testMode) {
        s_captured << (program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
        if (pid) {
            *pid = -1;
        }
        return true;
    }
    const QString bashPath = findBashExecutable();
    if (bashPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find MSYS2 or Git Bash for backend launch.");
        }
        return false;
    }
    QStringList commandParts;
    commandParts << quoteForBash(toBashPath(program));
    for (const QString& argument : arguments) {
        commandParts << quoteForBash(argument);
    }
    const QString commandLine = QStringLiteral("export PATH='/usr/bin:/ucrt64/bin:$PATH'; %1 > %2 2>&1")
                                    .arg(commandParts.join(QLatin1Char(' ')),
                                         quoteForBash(toBashPath(logPath)));
    if (!QProcess::startDetached(QDir::toNativeSeparators(bashPath),
                                 {QStringLiteral("-lc"), commandLine},
                                 m_packageRoot,
                                 pid)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to launch Bash runner for %1").arg(QDir::toNativeSeparators(program));
        }
        return false;
    }
    return true;
}

QString BackendController::testSignalPidPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.pid"));
}

QString BackendController::filePlayPidPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-fileplay.pid"));
}

QString BackendController::filePlayLogPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-fileplay.log"));
}
QString BackendController::testSignalLogPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.log"));
}

bool BackendController::stopTrackedProcess(const QString& pidFilePath, QString* errorMessage) const {
    if (s_testMode) {
        s_captured << QStringLiteral("taskkill /PID <tracked:%1> /T /F").arg(QDir::toNativeSeparators(pidFilePath));
        return true;
    }
    QFile pidFile(pidFilePath);
    if (!pidFile.exists()) {
        return true;
    }
    if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read PID file %1").arg(QDir::toNativeSeparators(pidFilePath));
        }
        return false;
    }
    const QString pid = QString::fromUtf8(pidFile.readAll()).trimmed();
    pidFile.close();
    if (!pid.isEmpty()) {
        QProcess killer;
        killer.start(QStringLiteral("taskkill.exe"), {QStringLiteral("/PID"), pid, QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(5000);
    }
    QFile::remove(pidFilePath);
    return true;
}
