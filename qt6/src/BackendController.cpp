#include "BackendController.h"

#include "DeckLinkDiscovery.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <windows.h>

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

// QProcess::suspend()/resume() are Unix-only - use the ntdll process
// primitives to hold the DeckLink playout on the exact frame
using NtProcessControlFunc = LONG(WINAPI*)(HANDLE);

bool suspendProcessById(qint64 pid, bool suspend) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }
    const auto control = reinterpret_cast<NtProcessControlFunc>(GetProcAddress(ntdll, suspend ? "NtSuspendProcess" : "NtResumeProcess"));
    if (!control) {
        return false;
    }
    const HANDLE handle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, static_cast<DWORD>(pid));
    if (!handle) {
        return false;
    }
    const LONG status = control(handle);
    CloseHandle(handle);
    return status == 0;
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
bool BackendController::startFileOutput(const VrecordConfig& config, QString* errorMessage) {
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

    if (s_testMode) {
        s_captured << (ffmpegPath + QLatin1Char(' ') + QStringLiteral("-stream_loop -1 -i %1 -f decklink %2")
                           .arg(playFile, outputDevice));
        m_playbackFile = playFile;
        m_playbackDevice = outputDevice;
        m_playbackDuration = 60.0;
        m_playbackFps = 25.0;
        m_playbackCueSeconds = 0.0;
        m_playbackFrozen = false;
        m_playbackJogged = false;
        m_testPlaybackActive = true;
        return true;
    }

    // the test signal and the file playout share the card output
    stopTrackedProcess(testSignalPidPath(), nullptr);
    // and on this card the input cannot run while the output is open
    stopVideoPreview();

    double duration = 0.0;
    double fps = 25.0;
    if (!probeMediaFile(playFile, &duration, &fps, errorMessage)) {
        return false;
    }
    // tear the previous playout down BEFORE applying the connector (teardown
    // race: SetInt silently fails while the driver is still stopping a stream)
    stopManagedFileOutputInternal(nullptr);
    QThread::msleep(400);
    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }

    m_playbackFile = playFile;
    m_playbackDevice = outputDevice;
    m_playbackDuration = duration;
    m_playbackFps = fps > 0 ? fps : 25.0;
    m_playbackConfig = config;
    m_playbackFrozen = false;
    m_playbackJogged = false;
    // a cued position survives a stop: toggle Play resumes from it
    const double resumeAt = (m_playbackCueSeconds > 0.05 && m_playbackCueSeconds < duration) ? m_playbackCueSeconds : 0.0;
    return launchFileOutputProcess(resumeAt, errorMessage) && startOutputFeed(config, errorMessage);
}

bool BackendController::stopFileOutput(const VrecordConfig& config, QString* errorMessage) {
    Q_UNUSED(config);
    const bool ok = stopManagedFileOutputInternal(errorMessage) && stopOutputFeed();
    m_playbackCueSeconds = 0.0;
    return ok;
}

bool BackendController::stopAllOutputs(const VrecordConfig& config, QString* errorMessage) {
    Q_UNUSED(config);
    if (!stopTrackedProcess(testSignalPidPath(), errorMessage)) {
        return false;
    }
    const bool ok = stopManagedFileOutputInternal(errorMessage) && stopOutputFeed();
    m_playbackCueSeconds = 0.0;
    return ok;
}

bool BackendController::probeMediaFile(const QString& file, double* durationSeconds, double* fps, QString* errorMessage) const {
    const QString ffprobePath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffprobe-ma.exe"));
    if (!QFileInfo::exists(ffprobePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffprobe backend at %1").arg(QDir::toNativeSeparators(ffprobePath));
        }
        return false;
    }
    QProcess probe;
    probe.start(ffprobePath,
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-show_entries"), QStringLiteral("format=duration:stream=r_frame_rate"),
                 QStringLiteral("-of"), QStringLiteral("json"), file});
    if (!probe.waitForFinished(10000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ffprobe timed out on %1").arg(QDir::toNativeSeparators(file));
        }
        return false;
    }
    const QString out = QString::fromUtf8(probe.readAllStandardOutput());
    bool ok = false;
    const QRegularExpression durationRe(QStringLiteral("\"duration\"\\s*:\\s*\"?([0-9.]+)"));
    const auto durationMatch = durationRe.match(out);
    if (durationMatch.hasMatch()) {
        *durationSeconds = durationMatch.captured(1).toDouble();
        ok = true;
    }
    const QRegularExpression rateRe(QStringLiteral("\"r_frame_rate\"\\s*:\\s*\"?([0-9]+)/([0-9]+)"));
    const auto rateMatch = rateRe.match(out);
    if (rateMatch.hasMatch()) {
        const double numerator = rateMatch.captured(1).toDouble();
        const double denominator = rateMatch.captured(2).toDouble();
        if (numerator > 0 && denominator > 0) {
            *fps = numerator / denominator;
        }
    }
    if (!ok && errorMessage) {
        *errorMessage = QStringLiteral("Could not probe the playout file duration: %1").arg(QDir::toNativeSeparators(file));
    }
    return ok;
}

bool BackendController::launchFileOutputProcess(double atSeconds, QString* errorMessage) {
    if (s_testMode) {
        s_captured << (QStringLiteral("ffmpeg-ma -stream_loop -1 -ss %1 -i %2 -f decklink %3")
                           .arg(QString::number(atSeconds, 'f', 3), m_playbackFile, m_playbackDevice));
        return true;
    }
    if (m_fileOutputProcess) {
        m_fileOutputProcess->kill();
        m_fileOutputProcess->waitForFinished(3000);
        m_fileOutputProcess->deleteLater();
        m_fileOutputProcess = nullptr;
    }
    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    QStringList arguments{
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("info"),
        QStringLiteral("-re"),
        QStringLiteral("-stream_loop"), QStringLiteral("-1")};
    if (atSeconds > 0.05) {
        arguments << QStringLiteral("-ss") << QString::number(atSeconds, 'f', 3);
    }
    arguments << QStringLiteral("-i") << m_playbackFile
              << QStringLiteral("-c:v") << QStringLiteral("v210")
              << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
              << QStringLiteral("-ar") << QStringLiteral("48000")
              << QStringLiteral("-ac") << QStringLiteral("2")
              << QStringLiteral("-f") << QStringLiteral("decklink")
              << m_playbackDevice;
    m_fileOutputProcess = new QProcess(this);
    m_playbackStderrTail.clear();
    connect(m_fileOutputProcess, &QProcess::readyReadStandardError, this, [this] {
        m_playbackStderrTail.append(m_fileOutputProcess->readAllStandardError());
        if (m_playbackStderrTail.size() > 8192) {
            m_playbackStderrTail = m_playbackStderrTail.right(8192);
        }
    });
    m_fileOutputProcess->start(ffmpegPath, arguments);
    if (!m_fileOutputProcess->waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start the playout process.");
        }
        m_fileOutputProcess->deleteLater();
        m_fileOutputProcess = nullptr;
        return false;
    }
    // remember the seek offset: ffmpeg rebases its time= counter after -ss
    m_playbackSeekOffset = atSeconds;
    m_playbackFrozen = false;
    m_playbackJogged = false;
    // keep the output feed following the transport position
    if (m_outputFeedProcess) {
        stopOutputFeed();
        startOutputFeedProcess();
    }
    return true;
}

bool BackendController::stopManagedFileOutputInternal(QString* errorMessage) {
    Q_UNUSED(errorMessage);
    if (m_fileOutputProcess) {
        // thaw a frozen playout first so pending signals are handled promptly
        suspendProcessById(m_fileOutputProcess->processId(), false);
        m_fileOutputProcess->kill();
        m_fileOutputProcess->waitForFinished(3000);
        m_fileOutputProcess->deleteLater();
        m_fileOutputProcess = nullptr;
    }
    m_playbackFrozen = false;
    m_playbackJogged = false;
    m_testPlaybackActive = false;
    m_playbackSeekOffset = 0.0;
    return true;
}

double BackendController::parsePlaybackLogSeconds() const {
    // the managed playout writes its stats (time=...) to stderr; the tail is
    // drained continuously so the pipe never fills and stalls the process
    const QByteArray tail = m_playbackStderrTail.right(4096);
    QRegularExpression timeRe(QStringLiteral("time=([0-9]+):([0-9]+):([0-9.]+)"));
    QRegularExpressionMatchIterator matches = timeRe.globalMatch(QString::fromUtf8(tail));
    QRegularExpressionMatch last;
    while (matches.hasNext()) {
        last = matches.next();
    }
    if (!last.hasMatch()) {
        return -1.0;
    }
    return last.captured(1).toDouble() * 3600.0 + last.captured(2).toDouble() * 60.0 + last.captured(3).toDouble();
}

double BackendController::currentPlaybackSeconds() const {
    if (m_playbackFrozen) {
        return m_playbackFreezeSeconds;
    }
    if (m_fileOutputProcess && m_fileOutputProcess->state() == QProcess::Running) {
        const double statsSeconds = parsePlaybackLogSeconds();
        if (statsSeconds >= 0) {
            return statsSeconds + m_playbackSeekOffset;
        }
    }
    return m_playbackCueSeconds;
}

bool BackendController::showPlayFrame(double seconds, QString* errorMessage) {
    if (s_testMode || m_playbackFile.isEmpty()) {
        return true;
    }
    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    QProcess extractor;
    extractor.start(ffmpegPath,
                    {QStringLiteral("-nostdin"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-ss"), QString::number(seconds, 'f', 3),
                     QStringLiteral("-i"), m_playbackFile,
                     QStringLiteral("-frames:v"), QStringLiteral("1"),
                     QStringLiteral("-update"), QStringLiteral("1"),
                     QStringLiteral("-f"), QStringLiteral("image2"),
                     QStringLiteral("-y"), outputFeedImagePath()});
    if (!extractor.waitForFinished(10000)) {
        extractor.kill();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not render the frame at %1s.").arg(QString::number(seconds, 'f', 2));
        }
        return false;
    }
    return true;
}

bool BackendController::togglePauseFileOutput() {
    if (s_testMode) {
        m_playbackFrozen = !m_playbackFrozen;
        return true;
    }
    if (!isFileOutputActive()) {
        // transport start: fresh playout, or resume from the cued position
        VrecordConfig config = m_playbackConfig;
        return startFileOutput(config, nullptr);
    }
    const qint64 playbackPid = m_fileOutputProcess->processId();
    const qint64 feedPid = m_outputFeedProcess ? m_outputFeedProcess->processId() : 0;
    if (!m_playbackFrozen) {
        // freeze: capture the exact frame from the freshest stats line, then
        // suspend the process so the device holds that exact frame (no reload)
        const double statsSeconds = parsePlaybackLogSeconds();
        m_playbackFreezeSeconds = statsSeconds >= 0 ? statsSeconds + m_playbackSeekOffset : currentPlaybackSeconds();
        if (m_playbackDuration > 0) {
            m_playbackFreezeSeconds = qBound(0.0, m_playbackFreezeSeconds, m_playbackDuration);
        }
        m_playbackCueSeconds = m_playbackFreezeSeconds;
        suspendProcessById(playbackPid, true);
        if (feedPid > 0) {
            suspendProcessById(feedPid, true);
        }
        m_playbackFrozen = true;
        showPlayFrame(m_playbackFreezeSeconds, nullptr);
    } else if (m_playbackJogged) {
        // the cue was jogged elsewhere while frozen: restart at the new position
        const double cue = m_playbackFreezeSeconds;
        stopManagedFileOutputInternal(nullptr);
        m_playbackCueSeconds = cue;
        launchFileOutputProcess(cue, nullptr);
    } else {
        // plain resume from the frozen frame, no reload
        suspendProcessById(playbackPid, false);
        if (feedPid > 0) {
            suspendProcessById(feedPid, false);
        }
        m_playbackFrozen = false;
        m_playbackCueSeconds = m_playbackFreezeSeconds;
    }
    return true;
}

bool BackendController::isFileOutputActive() const {
    return s_testMode ? m_testPlaybackActive
                      : (m_fileOutputProcess != nullptr && m_fileOutputProcess->state() == QProcess::Running);
}

bool BackendController::isFileOutputPaused() const {
    return isFileOutputActive() && m_playbackFrozen;
}

bool BackendController::isFileOutputCued() const {
    return !isFileOutputActive() && m_playbackCueSeconds > 0.05;
}

double BackendController::fileOutputPositionSeconds() const {
    double position = currentPlaybackSeconds();
    if (m_playbackDuration > 0) {
        position = qMin(position, m_playbackDuration);
    }
    return qMax(position, 0.0);
}

double BackendController::fileOutputDurationSeconds() const {
    return m_playbackDuration;
}

double BackendController::fileOutputFps() const {
    return m_playbackFps;
}

bool BackendController::stepFileOutput(int frames, QString* errorMessage) {
    if (frames == 0) {
        return true;
    }
    const double delta = frames / (m_playbackFps > 0 ? m_playbackFps : 25.0);
    // while playing: seek by the offset and keep playing; while frozen or
    // stopped: jog the held position and render that frame into the pane
    if (isFileOutputActive() && !m_playbackFrozen) {
        return seekFileOutput(currentPlaybackSeconds() + delta, errorMessage);
    }
    const double base = m_playbackFrozen ? m_playbackFreezeSeconds : m_playbackCueSeconds;
    double target = base + delta;
    if (m_playbackDuration > 0) {
        target = qMin(target, m_playbackDuration);
    }
    target = qMax(target, 0.0);
    if (m_playbackFrozen) {
        m_playbackFreezeSeconds = target;
        m_playbackJogged = true;
    }
    m_playbackCueSeconds = target;
    return showPlayFrame(target, errorMessage);
}

bool BackendController::seekFileOutput(double seconds, QString* errorMessage) {
    double target = seconds;
    if (m_playbackDuration > 0) {
        target = qMin(target, m_playbackDuration);
    }
    target = qMax(target, 0.0);
    if (!isFileOutputActive() && !s_testMode) {
        if (m_playbackFile.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Select a file and play it once first.");
            }
            return false;
        }
        // stopped: cue the position only - toggle Play to start there
        m_playbackCueSeconds = target;
        return showPlayFrame(target, errorMessage);
    }
    m_playbackCueSeconds = target;
    if (s_testMode) {
        s_captured << (QStringLiteral("ffmpeg-ma -stream_loop -1 -ss %1 -i %2 -f decklink %3")
                           .arg(QString::number(target, 'f', 3), m_playbackFile, m_playbackDevice));
        return true;
    }
    // restart at the new position (a frozen playout resumes playing, as on
    // macOS/Linux with the scrub bar release)
    return launchFileOutputProcess(target, errorMessage);
}

bool BackendController::startVideoPreview(const VrecordConfig& config, QString* errorMessage) {
    if (isVideoPreviewActive()) {
        return true;
    }
    if (s_testMode) {
        s_captured << QStringLiteral("ffmpeg-ma -f decklink -i preview");
        return true;
    }
    // the input and the output cannot be open at the same time on this card
    stopManagedFileOutputInternal(nullptr);
    stopOutputFeed();
    stopTrackedProcess(testSignalPidPath(), nullptr);

    QString outputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a DeckLink device first.");
        }
        return false;
    }
    // route the card's input to the connector the user selected before
    // sampling it (sdi/hdmi/component/... from the Video Input combo)
    const QString inputConnection = config.value(QStringLiteral("VIDEO_INPUT_CHOICE")).trimmed();
    if (!inputConnection.isEmpty()) {
        QString inputConnectionName;
        if (inputConnection == QStringLiteral("sdi")) inputConnectionName = QStringLiteral("SDI");
        else if (inputConnection == QStringLiteral("hdmi")) inputConnectionName = QStringLiteral("HDMI");
        else if (inputConnection == QStringLiteral("component")) inputConnectionName = QStringLiteral("Component");
        else if (inputConnection == QStringLiteral("composite")) inputConnectionName = QStringLiteral("Composite");
        else if (inputConnection == QStringLiteral("s-video")) inputConnectionName = QStringLiteral("S-Video");
        if (!inputConnectionName.isEmpty()) {
            DeckLinkDiscovery::applyInputConnection(outputDevice, inputConnectionName, nullptr);
        }
    }

    // prefer the mode the incoming signal is actually using (the user may be
    // feeding any format over HDMI/SDI); fall back to the selected standard
    QString formatCode = DeckLinkDiscovery::detectInputFormatCode(outputDevice, nullptr);
    QString detectedNote;
    if (formatCode.isEmpty()) {
        formatCode = formatCodeForStandard(config.value(QStringLiteral("STANDARD_CHOICE")).trimmed());
    } else {
        detectedNote = QStringLiteral(" (input mode %1)").arg(formatCode);
    }
    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    QStringList arguments{
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("decklink"),
        QStringLiteral("-format_code"), formatCode,
        QStringLiteral("-raw_format"), QStringLiteral("uyvy422"),
        QStringLiteral("-i"), outputDevice,
        QStringLiteral("-an"),
        QStringLiteral("-vf"), QStringLiteral("fps=4,scale=480:-1,format=yuv420p"),
        QStringLiteral("-c:v"), QStringLiteral("mjpeg"),
        QStringLiteral("-q:v"), QStringLiteral("5"),
        QStringLiteral("-update"), QStringLiteral("1"),
        QStringLiteral("-f"), QStringLiteral("image2"),
        QStringLiteral("-y"), previewImagePath()};
    m_previewProcess = new QProcess(this);
    connect(m_previewProcess, &QProcess::readyReadStandardError, m_previewProcess, &QProcess::readAllStandardError);
    m_previewProcess->start(ffmpegPath, arguments);
    if (!m_previewProcess->waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start the live preview.");
        }
        m_previewProcess->deleteLater();
        m_previewProcess = nullptr;
        return false;
    }
    if (errorMessage) {
        // success path: carry the detected input mode back to the caller
        *errorMessage = detectedNote;
    }
    return true;
}

bool BackendController::stopVideoPreview() {
    if (m_previewProcess) {
        m_previewProcess->kill();
        m_previewProcess->waitForFinished(3000);
        m_previewProcess->deleteLater();
        m_previewProcess = nullptr;
        QFile::remove(previewImagePath());
    }
    return true;
}

bool BackendController::isVideoPreviewActive() const {
    return m_previewProcess != nullptr && m_previewProcess->state() == QProcess::Running;
}

bool BackendController::startOutputFeed(const VrecordConfig& config, QString* errorMessage) {
    Q_UNUSED(config);
    if (startOutputFeedProcess()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to start the output feed.");
    }
    return false;
}

bool BackendController::startOutputFeedProcess() {
    if (s_testMode || m_playbackFile.isEmpty()) {
        return true;
    }
    stopOutputFeed();
    const QString ffmpegPath = QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    QStringList arguments{
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-stream_loop"), QStringLiteral("-1")};
    if (m_playbackSeekOffset > 0.05) {
        arguments << QStringLiteral("-ss") << QString::number(m_playbackSeekOffset, 'f', 3);
    }
    arguments << QStringLiteral("-i") << m_playbackFile
              << QStringLiteral("-vf") << QStringLiteral("fps=4,scale=480:-1,format=yuv420p")
              << QStringLiteral("-c:v") << QStringLiteral("mjpeg")
              << QStringLiteral("-q:v") << QStringLiteral("5")
              << QStringLiteral("-update") << QStringLiteral("1")
              << QStringLiteral("-f") << QStringLiteral("image2")
              << QStringLiteral("-y") << outputFeedImagePath();
    m_outputFeedProcess = new QProcess(this);
    connect(m_outputFeedProcess, &QProcess::readyReadStandardError, m_outputFeedProcess, &QProcess::readAllStandardError);
    m_outputFeedProcess->start(ffmpegPath, arguments);
    if (!m_outputFeedProcess->waitForStarted(5000)) {
        m_outputFeedProcess->deleteLater();
        m_outputFeedProcess = nullptr;
        return false;
    }
    if (m_playbackFrozen) {
        suspendProcessById(m_outputFeedProcess->processId(), true);
    }
    return true;
}

bool BackendController::stopOutputFeed() {
    if (m_outputFeedProcess) {
        m_outputFeedProcess->kill();
        m_outputFeedProcess->waitForFinished(3000);
        m_outputFeedProcess->deleteLater();
        m_outputFeedProcess = nullptr;
        QFile::remove(outputFeedImagePath());
    }
    return true;
}

QString BackendController::previewImagePath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-preview.jpg"));
}

QString BackendController::outputFeedImagePath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-outputfeed.jpg"));
}

QString BackendController::formatCodeForStandard(const QString& standardChoice) const {
    if (standardChoice == QStringLiteral("NTSC")) return QStringLiteral("ntsc");
    if (standardChoice == QStringLiteral("PAL")) return QStringLiteral("pal");
    if (standardChoice.startsWith(QStringLiteral("4k")) || standardChoice.startsWith(QStringLiteral("2d")) || standardChoice.startsWith(QStringLiteral("4d"))) {
        return standardChoice.left(4);
    }
    return standardChoice.left(4);
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

bool BackendController::applyVideoInputConnection(const VrecordConfig& config, QString* errorMessage) const {
    const QString inputDevice = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    if (inputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a DeckLink device first.");
        }
        return false;
    }
    const QString inputConnection = config.value(QStringLiteral("VIDEO_INPUT_CHOICE")).trimmed();
    QString connectionName;
    if (inputConnection == QStringLiteral("sdi")) connectionName = QStringLiteral("SDI");
    else if (inputConnection == QStringLiteral("hdmi")) connectionName = QStringLiteral("HDMI");
    else if (inputConnection == QStringLiteral("component")) connectionName = QStringLiteral("Component");
    else if (inputConnection == QStringLiteral("composite")) connectionName = QStringLiteral("Composite");
    else if (inputConnection == QStringLiteral("s-video")) connectionName = QStringLiteral("S-Video");
    if (connectionName.isEmpty()) {
        return true;
    }
    return DeckLinkDiscovery::applyInputConnection(inputDevice, connectionName, errorMessage);
}

bool BackendController::setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage) {
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

    // the test signal owns the card output exclusively - tear the other
    // outputs down FIRST and let the driver settle, otherwise the connector
    // apply below hits the teardown race and silently fails (the new stream
    // then starts on a stale analog connector and the HDMI monitor stays dark)
    stopManagedFileOutputInternal(nullptr);
    stopOutputFeed();
    stopVideoPreview();
    stopTrackedProcess(pidPath, nullptr);
    QThread::msleep(400);

    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }

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

    if (s_testMode) {
        s_captured << (ffmpegPath + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
        return true;
    }

    // launch, verify liveness, then verify the card actually locked the
    // requested raster: a silent connector revert (teardown race) would
    // stream the bars into a stale connector while the HDMI monitor stays dark
    const auto launchSignal = [&]() -> bool {
        qint64 newPid = 0;
        if (!startDetachedProgramWithLog(ffmpegPath, arguments, testSignalLogPath(), &newPid, errorMessage)) {
            return false;
        }
        QFile pidFile(pidPath);
        if (pidFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&pidFile);
            out << newPid << '\n';
        }
        QThread::msleep(1500);
        if (!pidAlive(newPid)) {
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
    };

    if (!launchSignal()) {
        return false;
    }

    const QString requestedSize = signalMode.size;
    const bool requestedHD = !lockedPatternSize(pattern).isEmpty()
        || requestedSize.startsWith(QStringLiteral("1920"))
        || requestedSize.startsWith(QStringLiteral("3840"))
        || requestedSize.startsWith(QStringLiteral("4096"))
        || requestedSize.startsWith(QStringLiteral("2048"))
        || requestedSize.startsWith(QStringLiteral("1280"));
    if (requestedHD) {
        const QString activeMode = DeckLinkDiscovery::activeOutputModeFourCC(outputDevice, nullptr);
        if (activeMode == QStringLiteral("ntsc") || activeMode == QStringLiteral("pal")) {
            // teardown race hit: re-apply the connector and relaunch once
            stopTrackedProcess(pidPath, nullptr);
            QThread::msleep(400);
            if (!applyVideoOutputConnection(config, errorMessage)) {
                return false;
            }
            if (!launchSignal()) {
                return false;
            }
        }
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
