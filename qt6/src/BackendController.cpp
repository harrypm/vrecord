#include "BackendController.h"

#include "DeckLinkDiscovery.h"
#include "VrecordOptions.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstring>

#include <windows.h>

namespace {
bool s_testMode = false;
QStringList s_captured;

// ffmpeg's decklink muxer keeps up to 2 x preroll frames scheduled on the
// card ("frames buffer size" = 2 * preroll) and blocks once that is full, so
// in steady state a frame reaches the wire 2 x preroll after ffmpeg emits it.
// The panes are delayed by that much to show the frame on the wire.
// Playout sets its preroll explicitly (shorter = lower output latency); the
// test signal keeps ffmpeg's default of 0.5 s.
constexpr double kPlayoutPreroll = 0.25;
constexpr double kDefaultPreroll = 0.5;

QString lockedPatternSize(const QString& pattern) {
    if (pattern.contains(QStringLiteral("UHD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("2160p"), Qt::CaseInsensitive)) {
        return QStringLiteral("3840x2160");
    }
    if (pattern.contains(QStringLiteral("HD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("1080p"), Qt::CaseInsensitive)) {
        return QStringLiteral("1920x1080");
    }
    return {};
}

QString testVideoSourceForPattern(const QString& pattern, const VideoMode& mode) {
    const QString size = lockedPatternSize(pattern).isEmpty() ? mode.size() : lockedPatternSize(pattern);
    const QString rate = mode.rate();
    if (pattern.contains(QStringLiteral("black"), Qt::CaseInsensitive)) {
        return QStringLiteral("color=c=black:size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("white"), Qt::CaseInsensitive)) {
        return QStringLiteral("color=c=white:size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("grid"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("test pattern"), Qt::CaseInsensitive)) {
        return QStringLiteral("testsrc2=size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("pluge"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive)) {
        return QStringLiteral("testsrc=size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("rgb"), Qt::CaseInsensitive)) {
        return QStringLiteral("rgbtestsrc=size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("yuv"), Qt::CaseInsensitive)) {
        return QStringLiteral("yuvtestsrc=size=%1:rate=%2").arg(size, rate);
    }
    if (pattern.contains(QStringLiteral("HD"), Qt::CaseInsensitive) || pattern.contains(QStringLiteral("BT.2111"), Qt::CaseInsensitive)) {
        return QStringLiteral("smptehdbars=size=%1:rate=%2").arg(size, rate);
    }
    return QStringLiteral("smptebars=size=%1:rate=%2").arg(size, rate);
}

QStringList videoFilterChainForPattern(const QString& pattern, const VideoMode& mode) {
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

QString testAudioSource(const QString& audioChoice) {
    if (audioChoice.contains(QStringLiteral("silence"), Qt::CaseInsensitive)) {
        return QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000");
    }
    if (audioChoice.contains(QStringLiteral("stereo"), Qt::CaseInsensitive) || audioChoice.contains(QStringLiteral("440"), Qt::CaseInsensitive)) {
        return QStringLiteral("aevalsrc=sin(1000*2*PI*t)|cos(440*2*PI*t):channel_layout=stereo:sample_rate=48000");
    }
    return QStringLiteral("aevalsrc=sin(1000*2*PI*t)|sin(1000*2*PI*t):channel_layout=stereo:sample_rate=48000");
}

// raw BGRA frames on stdout for a FrameFeed
QStringList previewPipeOutput(const QString& filterPrefix, int width, int height) {
    QString chain = filterPrefix;
    if (!chain.isEmpty()) {
        chain += QLatin1Char(',');
    }
    chain += QStringLiteral("scale=%1:%2:flags=fast_bilinear,format=bgra").arg(width).arg(height);
    return {QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-vf"), chain,
            QStringLiteral("-an"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-flush_packets"), QStringLiteral("1"),
            QStringLiteral("pipe:1")};
}

QString describeMode(const QString& code) {
    const VideoMode mode = VideoModes::forCode(code);
    if (!mode.isValid()) {
        return code;
    }
    return QStringLiteral("%1 %2%3 @ %4").arg(code, mode.size(), mode.fieldOrder.isEmpty() ? QStringLiteral("p") : QStringLiteral("i"),
                                                QString::number(mode.fps(), 'f', mode.rateDen == 1 ? 0 : 2));
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

QString lastLogLines(const QString& logPath, int count) {
    QFile logFile(logPath);
    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList lines = QString::fromUtf8(logFile.readAll()).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    // drop ANSI colour codes the shell's _report uses
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    QStringList kept;
    for (auto it = lines.crbegin(); it != lines.crend() && kept.size() < count; ++it) {
        QString line = *it;
        line.remove(ansi);
        line = line.trimmed();
        // skip ffmpeg progress and ffplay's per-frame status ("1.23 A-V: ... aq=")
        if (!line.isEmpty() && !line.startsWith(QStringLiteral("frame=")) && !line.contains(QStringLiteral(" aq="))
            && !line.contains(QStringLiteral("A-V:"))) {
            kept.prepend(line);
        }
    }
    return kept.join(QLatin1Char('\n'));
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

// belt and braces like the shell backend's pkill: a detached launcher can die
// while its ffmpeg child survives and keeps holding the card (every later
// launch then fails with 'Could not enable video output'). Sweep any orphan
// carrying our test-signal metadata marker.
void sweepOrphanedTestStreams() {
    QProcess sweeper;
    sweeper.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-Command"),
                   QStringLiteral("Get-CimInstance Win32_Process -Filter \"Name='ffmpeg-ma.exe'\" | "
                                  "Where-Object { $_.CommandLine -match 'vrecord-test-signal' } | "
                                  "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")});
    sweeper.waitForFinished(5000);
}

// the shell names its player window "mode:record - ..." / "mode:passthrough
// - ..." (WINDOW_NAME in _setup_vrecord_process); post WM_CLOSE to each
int closePlayerWindows() {
    int count = 0;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        wchar_t title[64] = {};
        if (IsWindowVisible(hwnd) && GetWindowTextW(hwnd, title, 64) > 0 && wcsncmp(title, L"mode:", 5) == 0) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ++*reinterpret_cast<int*>(param);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count;
}

QString findBashExecutable() {
    const QString msysBash = QStringLiteral("C:/msys64/usr/bin/bash.exe");
    if (QFileInfo::exists(msysBash)) {
        return msysBash;
    }
    const QString gitBash = QStringLiteral("%1/Git/bin/bash.exe").arg(QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")));
    if (QFileInfo::exists(gitBash)) {
        return gitBash;
    }
    return {};
}

// the POSIX tools directory that belongs to that bash (sed, awk, grep, ...)
QString posixToolsDir(const QString& bash) {
    const QString dir = QFileInfo(bash).absolutePath();
    if (dir.endsWith(QStringLiteral("/Git/bin"), Qt::CaseInsensitive)) {
        return QDir(dir).filePath(QStringLiteral("../usr/bin"));
    }
    return dir;
}

void hideConsole(QProcess* process) {
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
}

int parseRational(const QString& text, int* num, int* den) {
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() == 2) {
        *num = parts.at(0).toInt();
        *den = qMax(1, parts.at(1).toInt());
        return *num;
    }
    if (parts.size() == 1 && text.contains(QLatin1Char(':'))) {
        const QStringList colon = text.split(QLatin1Char(':'));
        *num = colon.at(0).toInt();
        *den = qMax(1, colon.value(1).toInt());
        return *num;
    }
    return 0;
}
}

BackendController::BackendController(const QString& packageRoot, QObject* parent)
    : QObject(parent)
    , m_packageRoot(packageRoot) {}

BackendController::~BackendController() {
    stopFeed(m_previewFeed);
    stopFeed(m_playoutFeed);
    stopFeed(m_testSignalFeed);
}

QString BackendController::packageRoot() const {
    return m_packageRoot;
}

QString BackendController::ffmpegPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
}

QString BackendController::ffprobePath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("bin/ffprobe-ma.exe"));
}

QString BackendController::deviceForOutput(const VrecordConfig& config) {
    for (const QString& key : {QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE"), QStringLiteral("PREVIEW_RELAY_DEV_CHOICE"), QStringLiteral("DECKLINK_INPUT_CHOICE")}) {
        const QString device = config.value(key).trimmed();
        if (!device.isEmpty()) {
            return device;
        }
    }
    return {};
}

bool BackendController::saveConfig(const VrecordConfig& config, QString* errorMessage) const {
    VrecordConfig prepared = config;
    prepared.prepareForBackend();
    if (prepared.save()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to save configuration to %1").arg(QDir::toNativeSeparators(config.configPath));
    }
    return false;
}

// ---------------------------------------------------------------- feeds

FrameFeed* BackendController::startFeed(const QString& kind, const QList<FrameFeed::Attempt>& attempts, int width, int height, int delayFrames) {
    auto* feed = new FrameFeed(ffmpegPath(), attempts, width, height, this);
    feed->setDisplayDelayFrames(delayFrames);
    feed->setLogPath(QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-feeds.log")));
    const bool isInput = kind == QStringLiteral("input");
    // feed as the context object: queued frames of a feed that has been
    // replaced are dropped together with it
    connect(feed, &FrameFeed::frameReady, feed, [this, feed, isInput] {
        const QImage frame = feed->takeFrame();
        // a stopped feed lives on until deleteLater: drop its queued frames so
        // they cannot repaint a pane that was already cleared
        if (feed != m_previewFeed && feed != m_playoutFeed && feed != m_testSignalFeed) {
            return;
        }
        if (isInput) {
            emit inputFrame(frame);
        } else {
            emit outputFrame(frame);
        }
    });
    connect(feed, &FrameFeed::streaming, feed, [this, isInput](const QString& label) {
        QString message = QStringLiteral("streaming (%1)").arg(label);
        if (isInput) {
            // the capture has the input enabled now, so the driver's
            // detection status is live: report what the card actually locked
            const QString detected = DeckLinkDiscovery::detectInputFormatCode(m_previewDevice, nullptr, true);
            message = QStringLiteral("streaming (%1)").arg(detected.isEmpty() ? QStringLiteral("%1 - no signal lock").arg(label) : describeMode(detected));
            emit inputStatus(message);
            followInputMode(detected);
        } else {
            emit outputStatus(message);
        }
    });
    connect(feed, &FrameFeed::attemptFailed, feed, [this, isInput](const QString& label, const QString& message) {
        const QString text = QStringLiteral("%1 failed: %2").arg(label, message);
        if (isInput) {
            emit inputStatus(text);
        } else {
            emit outputStatus(text);
        }
    });
    connect(feed, &FrameFeed::ended, feed, [this, isInput](bool hadFrames, const QString& message) {
        const QString text = hadFrames ? QStringLiteral("stopped: %1").arg(message) : QStringLiteral("could not start: %1").arg(message);
        if (isInput) {
            emit inputStatus(text);
        } else {
            emit outputStatus(text);
        }
    });
    feed->start();
    return feed;
}

void BackendController::stopFeed(QPointer<FrameFeed>& feed) {
    if (feed) {
        feed->stop();
        feed->deleteLater();
    }
    feed = nullptr;
}

void BackendController::releaseDeckLink() {
    // the card's input and output are exclusive between processes: nothing of
    // ours may hold it while a shell session or another feed takes over
    stopVideoPreview();
    stopManagedFileOutputInternal(nullptr);
    stopTestSignalInternal(nullptr);
}

// ---------------------------------------------------------------- shell sessions

bool BackendController::startRecord(const VrecordConfig& config, QString* errorMessage) {
    if (config.value(QStringLiteral("ID")).trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Enter a Name of Recording first.");
        }
        return false;
    }
    if (config.value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0")) == QStringLiteral("0")
        && config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a DeckLink device first.");
        }
        return false;
    }
    return startShellSession(QStringLiteral("record"), {QStringLiteral("-r")}, config, errorMessage);
}

bool BackendController::startPassthrough(const VrecordConfig& config, QString* errorMessage) {
    return startShellSession(QStringLiteral("passthrough"), {QStringLiteral("-p")}, config, errorMessage);
}

bool BackendController::startAudioCheck(const VrecordConfig& config, QString* errorMessage) {
    return startShellSession(QStringLiteral("audio check"), {QStringLiteral("-a")}, config, errorMessage);
}

QString BackendController::shellLogPath() const {
    return QDir(m_packageRoot).filePath(QStringLiteral("VrecordQt6-backend.log"));
}

bool BackendController::isShellRunning() const {
    return m_shellProcess && m_shellProcess->state() != QProcess::NotRunning;
}

bool BackendController::startShellSession(const QString& mode, const QStringList& arguments, const VrecordConfig& config, QString* errorMessage) {
    if (isShellRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("A %1 session is already running - close its player window first.").arg(m_shellMode);
        }
        return false;
    }
    if (!saveConfig(config, errorMessage)) {
        return false;
    }
    const QString script = QDir(m_packageRoot).filePath(QStringLiteral("vrecord"));
    QStringList shellArgs{toBashPath(script)};
    shellArgs << arguments << QStringLiteral("-c") << QDir::fromNativeSeparators(config.configPath);

    if (s_testMode) {
        s_captured << (QStringLiteral("bash ") + shellArgs.join(QLatin1Char(' ')) + QLatin1Char(' '));
        return true;
    }

    const QString bash = findBashExecutable();
    if (bash.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find MSYS2 (C:\\msys64) or Git Bash to run the vrecord backend.");
        }
        return false;
    }
    if (!QFileInfo::exists(script)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find the vrecord backend script at %1").arg(QDir::toNativeSeparators(script));
        }
        return false;
    }

    releaseDeckLink();

    // same environment the native Vrecord.exe launcher gives the GTK
    // frontend: package bin first, the POSIX tools, HOME = the real profile
    // (so ~/.vrecord.conf is the file this frontend writes), resources in MSYS
    // form, and no MSYS argument mangling of the filter graphs
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString nativeRoot = QDir::toNativeSeparators(m_packageRoot);
    env.insert(QStringLiteral("PATH"), QStringLiteral("%1\\bin;%2;%3")
                                           .arg(nativeRoot, QDir::toNativeSeparators(posixToolsDir(bash)), env.value(QStringLiteral("PATH"))));
    env.insert(QStringLiteral("VRECORD_RESOURCES"), toBashPath(QDir(m_packageRoot).filePath(QStringLiteral("Resources"))));
    const QString profile = env.value(QStringLiteral("USERPROFILE"));
    if (!profile.isEmpty()) {
        env.insert(QStringLiteral("HOME"), profile);
    }
    env.insert(QStringLiteral("MSYS2_ARG_CONV_EXCL"), QStringLiteral("*"));
    env.insert(QStringLiteral("MSYS_NO_PATHCONV"), QStringLiteral("1"));

    auto* process = new QProcess(this);
    hideConsole(process);
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(m_packageRoot);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setStandardOutputFile(shellLogPath(), QIODevice::Truncate);
    // EOF instead of a hang if the shell ever falls back to a prompt
    process->setStandardInputFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this, [this, process, mode](int exitCode, QProcess::ExitStatus status) {
        QString message = lastLogLines(shellLogPath(), 6);
        if (status == QProcess::CrashExit && message.isEmpty()) {
            message = QStringLiteral("the backend was terminated");
        }
        if (m_shellProcess == process) {
            m_shellProcess = nullptr;
            m_shellMode.clear();
        }
        process->deleteLater();
        emit shellFinished(mode, status == QProcess::CrashExit ? -1 : exitCode, message);
    });
    process->start(bash, shellArgs);
    if (!process->waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start the vrecord backend (%1).").arg(process->errorString());
        }
        process->deleteLater();
        return false;
    }
    m_shellProcess = process;
    m_shellMode = mode;
    emit shellStarted(mode);
    return true;
}

bool BackendController::stopShell() {
    if (!isShellRunning()) {
        return true;
    }
    // end it the way the user would: close the player window (WM_CLOSE, not a
    // kill) so the tee muxer finishes and the recording gets its trailer.
    // (taskkill's WINDOWTITLE filter does not match these titles reliably.)
    const int closed = closePlayerWindows();
    if (closed > 0 && m_shellProcess && m_shellProcess->waitForFinished(20000)) {
        return true;
    }
    if (m_shellProcess) {
        QProcess killer;
        killer.start(QStringLiteral("taskkill.exe"),
                     {QStringLiteral("/PID"), QString::number(m_shellProcess->processId()), QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(5000);
    }
    return true;
}

bool BackendController::openLegacyUi(QString* errorMessage) const {
    const QString launcher = QDir(m_packageRoot).filePath(QStringLiteral("Vrecord.exe"));
    if (s_testMode) {
        s_captured << (launcher + QStringLiteral(" -e"));
        return true;
    }
    if (!QFileInfo::exists(launcher)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find the GTK launcher at %1").arg(QDir::toNativeSeparators(launcher));
        }
        return false;
    }
    return QProcess::startDetached(launcher, {QStringLiteral("-e")}, m_packageRoot);
}

// ---------------------------------------------------------------- live input preview

bool BackendController::startVideoPreview(const VrecordConfig& config, QString* errorMessage) {
    if (isVideoPreviewActive()) {
        return true;
    }
    if (s_testMode) {
        s_captured << QStringLiteral("ffmpeg-ma -f decklink -i preview");
        return true;
    }
    if (isShellRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 session is using the card.").arg(m_shellMode);
        }
        return false;
    }
    const QString device = config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    if (device.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a DeckLink device first.");
        }
        return false;
    }
    if (!QFileInfo::exists(ffmpegPath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffmpeg backend at %1").arg(QDir::toNativeSeparators(ffmpegPath()));
        }
        return false;
    }

    // half-duplex cards cannot capture while they play out: drop our outputs
    // first. Full-duplex cards (Intensity Pro 4K reports dxfu) run the
    // preview alongside playout / the test signal.
    if (!deviceIsFullDuplex(device)) {
        stopManagedFileOutputInternal(nullptr);
        stopTestSignalInternal(nullptr);
        m_outputClaimed = false;
    }
    stopFeed(m_previewFeed);

    const VideoMode mode = VideoModes::forStandard(config.value(QStringLiteral("STANDARD_CHOICE")).trimmed());
    // native framing: the pane gets the signal's own raster shape. The driver
    // keeps the last detected input mode even while no capture runs; when the
    // live capture reports a different one the preview relaunches at that
    // size (see the streaming handler and the mode poll)
    QString frameCode = DeckLinkDiscovery::detectInputFormatCode(device, nullptr, true);
    VideoMode frameMode = VideoModes::forCode(frameCode);
    if (!frameMode.isValid()) {
        frameMode = mode;
        frameCode = mode.formatCode;
    }
    int width = 0;
    int height = 0;
    VideoModes::previewSize(frameMode.height, VideoModes::displayAspect(frameMode, config.value(QStringLiteral("ASPECT_RATIO_CHOICE"))), &width, &height);
    m_previewFrameCode = frameCode;
    m_previewConfig = config;

    bool autodetect = true;
    bool relayPossible = false;
    QString relayDevice = config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")).trimmed();
    if (relayDevice.isEmpty()) {
        relayDevice = device;
    }
    for (const auto& info : DeckLinkDiscovery::enumerateDevices()) {
        if (info.ffmpegName == device || info.modelName == device) {
            autodetect = info.supportsInputFormatDetection;
            // relaying the input back out of the same card needs full duplex
            relayPossible = (relayDevice != info.ffmpegName && relayDevice != info.modelName) || info.supportsFullDuplex;
        }
    }
    // the relay needs the card output: while playout or the test signal owns
    // it, preview without the relay (it comes back when the output is released)
    const bool relay = config.value(QStringLiteral("PREVIEW_RELAY_CHOICE")) == QStringLiteral("true") && relayPossible && !m_outputClaimed;
    m_previewRelaying = relay;

    // capture straight to a raw pipe: every captured frame is forwarded (no
    // fps filter), probing is cut to the minimum so the first frame arrives
    // as soon as the card locks, and the pipe is flushed per frame
    const QStringList inputHead{QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
                                QStringLiteral("-loglevel"), QStringLiteral("error"),
                                QStringLiteral("-fflags"), QStringLiteral("nobuffer"),
                                QStringLiteral("-probesize"), QStringLiteral("32"),
                                QStringLiteral("-analyzeduration"), QStringLiteral("0")};
    QStringList deviceOptions{QStringLiteral("-video_input"), VrecordOptions::ffmpegVideoInput(config.value(QStringLiteral("VIDEO_INPUT_CHOICE"))),
                              QStringLiteral("-raw_format"), QStringLiteral("uyvy422")};
    // relay: capture the card's native 10-bit 4:2:2 (raw_format yuv422p10 =
    // v210 on the wire) and re-pack it as v210 for the output - no colour or
    // bit-depth conversion, the output carries the input frames at the input rate
    QStringList relayDeviceOptions{QStringLiteral("-video_input"), VrecordOptions::ffmpegVideoInput(config.value(QStringLiteral("VIDEO_INPUT_CHOICE"))),
                                   QStringLiteral("-raw_format"), QStringLiteral("yuv422p10"),
                                   QStringLiteral("-audio_input"), VrecordOptions::ffmpegAudioInput(config.value(QStringLiteral("AUDIO_INPUT_CHOICE")))};
    const QStringList pipeOut{QStringLiteral("-map"), QStringLiteral("0:v:0"),
                              QStringLiteral("-vf"), QStringLiteral("scale=%1:%2:flags=fast_bilinear,setsar=1,format=bgra").arg(width).arg(height),
                              QStringLiteral("-an"),
                              QStringLiteral("-f"), QStringLiteral("rawvideo"),
                              QStringLiteral("-flush_packets"), QStringLiteral("1"),
                              QStringLiteral("pipe:1")};
    const QStringList relayOut{QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a:0"),
                               // re-pack rather than -c:v copy: a copied stream keeps the capture's
                               // 1/1000000 time base and the muxer then cannot derive the mode
                               QStringLiteral("-c:v"), QStringLiteral("v210"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                               QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("2"),
                               QStringLiteral("-f"), QStringLiteral("decklink"), relayDevice};

    const auto makeAttempt = [&](bool detect, bool withRelay) {
        FrameFeed::Attempt attempt;
        attempt.label = detect ? QStringLiteral("input autodetect") : QStringLiteral("format %1").arg(mode.formatCode);
        if (withRelay) {
            attempt.label += QStringLiteral(" + relay to %1").arg(relayDevice);
        }
        attempt.arguments = inputHead;
        if (!detect && !mode.formatCode.isEmpty()) {
            attempt.arguments << QStringLiteral("-format_code") << mode.formatCode;
        }
        attempt.arguments << (withRelay ? relayDeviceOptions : deviceOptions)
                          << QStringLiteral("-f") << QStringLiteral("decklink") << QStringLiteral("-i") << device
                          << pipeOut;
        if (withRelay) {
            attempt.arguments << relayOut;
        }
        return attempt;
    };

    // autodetect first (follows whatever the deck sends, like the macOS/Linux
    // preview), then the selected standard's format code; a relay variant
    // that cannot open the output falls back to the plain preview
    QList<FrameFeed::Attempt> attempts;
    for (const bool withRelay : relay ? QList<bool>{true, false} : QList<bool>{false}) {
        if (autodetect) {
            attempts << makeAttempt(true, withRelay);
        }
        if (!mode.formatCode.isEmpty()) {
            attempts << makeAttempt(false, withRelay);
        }
    }
    if (attempts.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a standard on the Decklink tab first.");
        }
        return false;
    }
    m_previewFeed = startFeed(QStringLiteral("input"), attempts, width, height, 0);
    m_previewDevice = device;
    if (!m_inputModeTimer) {
        m_inputModeTimer = new QTimer(this);
        m_inputModeTimer->setInterval(2000);
        connect(m_inputModeTimer, &QTimer::timeout, this, [this] {
            if (!isVideoPreviewActive() || !m_previewFeed->isStreaming()) {
                return;
            }
            followInputMode(DeckLinkDiscovery::detectInputFormatCode(m_previewDevice, nullptr, true));
        });
    }
    m_inputModeTimer->start();
    emit inputStatus(QStringLiteral("starting (%1) at %2x%3").arg(attempts.first().label).arg(width).arg(height));
    return true;
}

void BackendController::followInputMode(const QString& detected) {
    if (detected.isEmpty() || !VideoModes::forCode(detected).isValid()) {
        return;
    }
    if (detected != m_lastReportedInputMode) {
        m_lastReportedInputMode = detected;
        emit inputModeDetected(detected);
    }
    if (detected != m_previewFrameCode) {
        // the signal changed raster/rate: relaunch so the pane keeps native framing
        m_previewFrameCode = detected;
        QTimer::singleShot(0, this, [this] {
            if (!isVideoPreviewActive() || isShellRunning()) {
                return;
            }
            const VrecordConfig config = m_previewConfig;
            stopFeed(m_previewFeed);
            startVideoPreview(config, nullptr);
        });
    }
}

bool BackendController::deviceIsFullDuplex(const QString& device) const {
    for (const auto& info : DeckLinkDiscovery::enumerateDevices()) {
        if (info.ffmpegName == device || info.modelName == device) {
            return info.supportsFullDuplex;
        }
    }
    return false;
}

// playout / the test signal is about to own the output of outputDevice
void BackendController::claimOutputFromPreview(const QString& outputDevice) {
    m_outputClaimed = true;
    if (s_testMode || !m_previewFeed) {
        return;
    }
    const bool sameCard = outputDevice == m_previewDevice;
    if (sameCard && !deviceIsFullDuplex(m_previewDevice)) {
        stopVideoPreview();
        return;
    }
    if (m_previewRelaying) {
        // the relay holds the output: relaunch the preview without it
        const VrecordConfig config = m_previewConfig;
        stopFeed(m_previewFeed);
        startVideoPreview(config, nullptr);
        QThread::msleep(300);   // let the driver release the output
    }
}

// the user stopped playout / the test signal: give the relay its output back
void BackendController::releaseOutputClaim() {
    m_outputClaimed = false;
    if (s_testMode || !m_previewFeed || m_previewRelaying
        || m_previewConfig.value(QStringLiteral("PREVIEW_RELAY_CHOICE")) != QStringLiteral("true")) {
        return;
    }
    QTimer::singleShot(400, this, [this] {
        if (m_outputClaimed || isShellRunning() || !isVideoPreviewActive() || m_previewRelaying) {
            return;
        }
        const VrecordConfig config = m_previewConfig;
        stopFeed(m_previewFeed);
        startVideoPreview(config, nullptr);
    });
}

bool BackendController::stopVideoPreview() {
    if (m_inputModeTimer) {
        m_inputModeTimer->stop();
    }
    if (m_previewFeed) {
        stopFeed(m_previewFeed);
        emit inputStatus(QStringLiteral("stopped"));
    }
    return true;
}

bool BackendController::isVideoPreviewActive() const {
    return m_previewFeed && m_previewFeed->isRunning();
}

// ---------------------------------------------------------------- file playout

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
    const QString outputDevice = deviceForOutput(config);
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a hardware video output device first.");
        }
        return false;
    }
    if (!QFileInfo::exists(ffmpegPath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffmpeg backend at %1").arg(QDir::toNativeSeparators(ffmpegPath()));
        }
        return false;
    }

    if (s_testMode) {
        s_captured << (ffmpegPath() + QLatin1Char(' ') + QStringLiteral("-stream_loop -1 -i %1 -f decklink %2").arg(playFile, outputDevice));
        m_playbackFile = playFile;
        m_playbackDevice = outputDevice;
        m_playbackDuration = 60.0;
        m_playbackFps = 25.0;
        m_playbackCueFrame = 0;
        m_playbackTotalFrames = qRound64(m_playbackDuration * m_playbackFps);
        m_playbackHolding = false;
        m_testPlaybackActive = true;
        return true;
    }
    if (isShellRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 session is using the card.").arg(m_shellMode);
        }
        return false;
    }

    MediaInfo media;
    if (!probeMediaFile(playFile, &media, errorMessage)) {
        return false;
    }

    // conform to a mode the card accepts (mirrors _start_file_playback):
    // broadcast size/rate combinations play natively when auto-detect is on,
    // everything else is converted to the selected standard
    VideoMode target;
    QString filter;
    QString description;
    // the output has its own standard, independent of the capture Standard:
    // "Auto" plays the file in its native mode; a named standard converts to it.
    // Composite/S-Video only carry SD, so there Auto follows the file's rate
    // (25/50 fps -> PAL, everything else -> NTSC).
    QString nativeNote;
    const QString outputStandard = config.value(QStringLiteral("OUTPUT_STANDARD_CHOICE")).trimmed();
    const bool autoOutput = outputStandard.isEmpty() || outputStandard.startsWith(QStringLiteral("Auto"));
    const QString connection = config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE")).trimmed();
    const bool analogSdOnly = connection == QStringLiteral("Composite") || connection == QStringLiteral("S-Video");
    if (autoOutput && !analogSdOnly) {
        target = nativePlayoutMode(media, outputDevice, &nativeNote);
    }
    const QString fileRate = QString::number(media.rateDen > 0 ? double(media.rateNum) / media.rateDen : 0.0, 'f', media.rateDen == 1 ? 0 : 2);
    if (target.isValid()) {
        QStringList chain;
        if (target.width != media.width || target.height != media.height) {
            const bool sdTarget = target.formatCode == QStringLiteral("ntsc") || target.formatCode == QStringLiteral("pal");
            if (sdTarget || target.height == media.height) {
                chain << QStringLiteral("scale=%1:%2").arg(target.width).arg(target.height);
            } else {
                // downscale into the card's raster at the picture's own aspect
                chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1")
                             .arg(target.width).arg(target.height);
            }
        }
        if (!target.fieldOrder.isEmpty()) {
            chain << QStringLiteral("setfield=%1").arg(target.fieldOrder);
        }
        filter = chain.join(QLatin1Char(','));
        description = QStringLiteral("native %1x%2 @ %3 -> %4").arg(media.width).arg(media.height).arg(fileRate, describeMode(target.formatCode));
        if (!nativeNote.isEmpty()) {
            description += QStringLiteral(" (%1)").arg(nativeNote);
        }
    } else {
        const bool pal = media.rateDen > 0 && (media.rateNum == 25 * media.rateDen || media.rateNum == 50 * media.rateDen);
        const QString standard = !autoOutput    ? outputStandard
                                 : analogSdOnly ? (pal ? QStringLiteral("PAL") : QStringLiteral("NTSC"))
                                                : config.value(QStringLiteral("STANDARD_CHOICE")).trimmed();
        target = VideoModes::forStandard(standard);
        if (!target.isValid()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Select a standard on the Decklink tab first (the file is converted to it).");
            }
            return false;
        }
        filter = QStringLiteral("scale=%1:%2,fps=%3").arg(target.width).arg(target.height).arg(target.rate());
        if (!target.fieldOrder.isEmpty()) {
            filter += QStringLiteral(",setfield=%1").arg(target.fieldOrder);
        }
        description = QStringLiteral("%1x%2 @ %3 converted to %4").arg(media.width).arg(media.height)
                          .arg(QStringLiteral("%1/%2").arg(media.rateNum).arg(media.rateDen), standard);
    }

    // the file playout owns the card output: a relaying preview steps aside,
    // a plain preview keeps running on full-duplex cards
    claimOutputFromPreview(outputDevice);
    stopTestSignalInternal(nullptr);
    // tear the previous playout down BEFORE applying the connector (teardown
    // race: SetInt silently fails while the driver is still stopping a stream)
    stopManagedFileOutputInternal(nullptr);
    QThread::msleep(400);
    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }

    // the pane shows the file at its display aspect
    double aspect = target.height > 0 ? double(target.width) / target.height : 16.0 / 9.0;
    if (media.width > 0 && media.height > 0 && media.sarNum > 0 && media.sarDen > 0) {
        aspect = double(media.width) * media.sarNum / (double(media.height) * media.sarDen);
    }
    if (target.formatCode == QStringLiteral("ntsc") || target.formatCode == QStringLiteral("pal")) {
        if (media.sarNum == media.sarDen) {
            aspect = VideoModes::displayAspect(target, config.value(QStringLiteral("ASPECT_RATIO_CHOICE")));
        }
    }
    VideoModes::previewSize(target.height, aspect, &m_previewWidth, &m_previewHeight);

    if (playFile != m_playbackFile) {
        m_playbackCueFrame = 0;   // another file: start at its first frame
    }
    m_playbackMedia = media;
    m_playbackMode = target;
    m_playbackFilter = filter;
    m_playbackDescription = description;
    m_playbackFile = playFile;
    m_playbackDevice = outputDevice;
    m_playbackDuration = media.duration;
    m_playbackFps = target.fps() > 0 ? target.fps() : 25.0;
    m_playbackDelayFrames = qRound(2.0 * kPlayoutPreroll * m_playbackFps);
    m_playbackTotalFrames = media.duration > 0 ? qMax<qint64>(1, qint64(std::floor(media.duration * m_playbackFps + 1e-6))) : 0;
    m_playbackConfig = config;
    m_playbackHolding = false;
    // a cued / halted frame survives: Play resumes exactly there
    qint64 resumeFrame = m_playbackCueFrame;
    if (resumeFrame < 0 || (m_playbackTotalFrames > 0 && resumeFrame >= m_playbackTotalFrames)) {
        resumeFrame = 0;
    }
    return launchFileOutputProcess(resumeFrame, false, errorMessage);
}

bool BackendController::stopFileOutput(const VrecordConfig& config, QString* errorMessage) {
    Q_UNUSED(config);
    const bool ok = stopManagedFileOutputInternal(errorMessage);
    m_playbackCueFrame = 0;
    releaseOutputClaim();
    return ok;
}

bool BackendController::stopAllOutputs(const VrecordConfig& config, QString* errorMessage) {
    Q_UNUSED(config);
    if (!stopTestSignalInternal(errorMessage)) {
        return false;
    }
    const bool ok = stopManagedFileOutputInternal(errorMessage);
    m_playbackCueFrame = 0;
    releaseOutputClaim();
    return ok;
}

bool BackendController::probeMediaFile(const QString& file, MediaInfo* info, QString* errorMessage) const {
    if (!QFileInfo::exists(ffprobePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffprobe backend at %1").arg(QDir::toNativeSeparators(ffprobePath()));
        }
        return false;
    }
    QProcess probe;
    hideConsole(&probe);
    probe.start(ffprobePath(),
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-show_entries"),
                 QStringLiteral("format=duration:stream=codec_type,width,height,r_frame_rate,sample_aspect_ratio,field_order"),
                 QStringLiteral("-of"), QStringLiteral("json"), file});
    if (!probe.waitForFinished(15000)) {
        probe.kill();
        if (errorMessage) {
            *errorMessage = QStringLiteral("ffprobe timed out on %1").arg(QDir::toNativeSeparators(file));
        }
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    info->duration = root.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    bool haveVideo = false;
    for (const QJsonValue& value : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("audio")) {
            info->hasAudio = true;
        } else if (type == QStringLiteral("video") && !haveVideo) {
            haveVideo = true;
            info->width = stream.value(QStringLiteral("width")).toInt();
            info->height = stream.value(QStringLiteral("height")).toInt();
            parseRational(stream.value(QStringLiteral("r_frame_rate")).toString(), &info->rateNum, &info->rateDen);
            int sarNum = 1;
            int sarDen = 1;
            if (parseRational(stream.value(QStringLiteral("sample_aspect_ratio")).toString(), &sarNum, &sarDen) > 0) {
                info->sarNum = sarNum;
                info->sarDen = sarDen;
            }
            // ffprobe reports tt/tb (top coded first) and bb/bt as well as
            // tff/bff: all are interlaced
            const QString field = stream.value(QStringLiteral("field_order")).toString().toLower();
            if (field == QStringLiteral("tt") || field == QStringLiteral("tb") || field == QStringLiteral("tff")) {
                info->fieldOrder = QStringLiteral("tff");
            } else if (field == QStringLiteral("bb") || field == QStringLiteral("bt") || field == QStringLiteral("bff")) {
                info->fieldOrder = QStringLiteral("bff");
            }
        }
    }
    if (!haveVideo || info->width <= 0 || info->height <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read a video stream from %1").arg(QDir::toNativeSeparators(file));
        }
        return false;
    }
    // an unknown duration is still playable; the scrub bar just stays off
    info->duration = qMax(info->duration, 0.0);
    return true;
}

VideoMode BackendController::nativePlayoutMode(const MediaInfo& media, const QString& device, QString* note) const {
    const QStringList supported = DeckLinkDiscovery::outputModeCodes(device);
    const VideoMode exact = VideoModes::forNative(media.width, media.height, media.rateNum, media.rateDen, media.fieldOrder);
    // no mode list (driver query failed): trust the broadcast table as before
    if (exact.isValid() && (supported.isEmpty() || supported.contains(exact.formatCode))) {
        return exact;
    }
    if (media.rateNum <= 0 || media.rateDen <= 0) {
        return {};
    }
    // keep the file's frame rate (no frame-rate conversion, the motion stays
    // native): the tallest raster the card offers that is not an upscale, or
    // failing that the smallest upscale
    const bool interlaced = !media.fieldOrder.isEmpty();
    const double fileAspect = media.height > 0 ? double(media.width) / media.height : 16.0 / 9.0;
    const auto shapeError = [&](const VideoMode& m) { return std::abs(double(m.width) / m.height - fileAspect); };
    const auto better = [&](const VideoMode& a, const VideoMode& b) {
        if (!b.isValid()) {
            return true;
        }
        const bool aFits = a.height <= media.height;
        const bool bFits = b.height <= media.height;
        if (aFits != bFits) {
            return aFits;
        }
        if (a.height != b.height) {
            return aFits ? a.height > b.height : a.height < b.height;
        }
        return shapeError(a) < shapeError(b);
    };
    VideoMode best;
    for (const QString& code : supported) {
        const VideoMode mode = VideoModes::forCode(code);
        if (!mode.isValid() || qint64(mode.rateNum) * media.rateDen != qint64(media.rateNum) * mode.rateDen) {
            continue;
        }
        if (mode.fieldOrder.isEmpty() == interlaced) {
            continue;
        }
        if (better(mode, best)) {
            best = mode;
        }
    }
    if (best.isValid() && note) {
        *note = exact.isValid() ? QStringLiteral("%1 is not supported by this card").arg(exact.formatCode)
                                : QStringLiteral("no broadcast mode at %1x%2").arg(media.width).arg(media.height);
    }
    return best;
}

// Positions are frame indices on the output timeline (the playout rate).
// Seeking to frame N uses the midpoint before it, so ffmpeg's accurate seek
// (which keeps frames with pts >= the seek time) starts exactly on frame N
// whatever the float rounding.
double BackendController::seekSecondsForFrame(qint64 frame) const {
    if (frame <= 0) {
        return 0.0;
    }
    const int num = m_playbackMode.rateNum > 0 ? m_playbackMode.rateNum : 25;
    const int den = m_playbackMode.rateNum > 0 ? m_playbackMode.rateDen : 1;
    return (double(frame) - 0.5) * den / num;
}

qint64 BackendController::wrapFrame(qint64 frame) const {
    if (m_playbackTotalFrames > 0) {
        frame %= m_playbackTotalFrames;
        if (frame < 0) {
            frame += m_playbackTotalFrames;
        }
    }
    return qMax<qint64>(frame, 0);
}

qint64 BackendController::clampFrame(qint64 frame) const {
    if (m_playbackTotalFrames > 0) {
        frame = qMin(frame, m_playbackTotalFrames - 1);
    }
    return qMax<qint64>(frame, 0);
}

// hold = pause: one frame (N) is repeated to the card with silent audio, so
// the card and the pane both show exactly the frame the transport is on.
// (Suspending the playout instead lets the card play out its 0.5 s preroll
// buffer first and stop ~25 frames past the frame on screen.)
bool BackendController::launchFileOutputProcess(qint64 frame, bool hold, QString* errorMessage) {
    frame = hold ? clampFrame(frame) : wrapFrame(frame);
    const double seekSeconds = seekSecondsForFrame(frame);
    if (s_testMode) {
        s_captured << (QStringLiteral("ffmpeg-ma %1-ss %2 -i %3 -f decklink %4")
                           .arg(hold ? QStringLiteral("(hold) ") : QStringLiteral("-stream_loop -1 "),
                                QString::number(seekSeconds, 'f', 6), m_playbackFile, m_playbackDevice));
        m_playbackStartFrame = frame;
        m_playbackCueFrame = frame;
        m_playbackHolding = hold;
        return true;
    }
    stopFeed(m_playoutFeed);

    // one process drives the card and the output pane: both outputs come
    // from the same decoded frames, so the pane cannot drift from the wire
    // No -re: the DeckLink muxer blocks once the card's buffer is full, so the
    // card's own clock paces the playout. -re adds a second (file) clock that
    // fights it - a 4K50 HEVC file then only reached ~34 fps on the wire
    // instead of 50.
    QStringList arguments{QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
                          QStringLiteral("-loglevel"), QStringLiteral("error")};
    if (!hold) {
        arguments << QStringLiteral("-stream_loop") << QStringLiteral("-1");
    }
    if (seekSeconds > 0.0) {
        arguments << QStringLiteral("-ss") << QString::number(seekSeconds, 'f', 6);
    }
    arguments << QStringLiteral("-i") << m_playbackFile;
    if (hold) {
        arguments << QStringLiteral("-f") << QStringLiteral("lavfi")
                  << QStringLiteral("-i") << QStringLiteral("anullsrc=r=48000:cl=stereo");
    }
    // card: conform once, straight into the card's 10-bit 4:2:2 (v210) in one
    // swscale pass, then split - the pane is scaled from that same frame. Field
    // marking is frame-level via setfield; no explicit format_code (the muxer
    // would then validate the container field order and reject the output).
    const int num = m_playbackMode.rateNum > 0 ? m_playbackMode.rateNum : 25;
    const int den = m_playbackMode.rateNum > 0 ? m_playbackMode.rateDen : 1;
    QString cardChain;
    if (hold) {
        // the first decoded frame (= frame N), repeated at the output rate
        cardChain = QStringLiteral("trim=end_frame=1,loop=loop=-1:size=1,setpts=N*%1/%2/TB").arg(den).arg(num);
    }
    if (!m_playbackFilter.isEmpty()) {
        cardChain += (cardChain.isEmpty() ? QString() : QStringLiteral(",")) + m_playbackFilter;
    }
    cardChain += (cardChain.isEmpty() ? QString() : QStringLiteral(",")) + QStringLiteral("format=yuv422p10le");
    const QString graph = QStringLiteral("[0:v:0]%1,split=2[card][pane];[pane]scale=%2:%3:flags=fast_bilinear,format=bgra[view]")
                              .arg(cardChain).arg(m_previewWidth).arg(m_previewHeight);
    arguments << QStringLiteral("-filter_complex") << graph
              << QStringLiteral("-map") << QStringLiteral("[card]")
              << QStringLiteral("-map") << (hold ? QStringLiteral("1:a:0") : QStringLiteral("0:a:0?"));
    if (hold) {
        arguments << QStringLiteral("-r") << QStringLiteral("%1/%2").arg(num).arg(den);
    }
    arguments << QStringLiteral("-c:v") << QStringLiteral("v210")
              << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
              << QStringLiteral("-ar") << QStringLiteral("48000")
              << QStringLiteral("-ac") << QStringLiteral("2")
              << QStringLiteral("-preroll") << QString::number(kPlayoutPreroll, 'f', 2)
              << QStringLiteral("-f") << QStringLiteral("decklink") << m_playbackDevice
              << QStringLiteral("-map") << QStringLiteral("[view]")
              << QStringLiteral("-an")
              << QStringLiteral("-f") << QStringLiteral("rawvideo")
              << QStringLiteral("-flush_packets") << QStringLiteral("1")
              << QStringLiteral("pipe:1");

    FrameFeed::Attempt attempt;
    attempt.label = QStringLiteral("%1 %2 frame %3").arg(hold ? QStringLiteral("hold") : QStringLiteral("playout"), m_playbackMode.formatCode).arg(frame);
    attempt.arguments = arguments;
    // a held frame is the same on the wire and in the pane: no preroll delay
    m_playoutFeed = startFeed(QStringLiteral("output"), {attempt}, m_previewWidth, m_previewHeight, hold ? 0 : m_playbackDelayFrames);

    // the card refuses a mode within the first second; report it instead of
    // leaving a dead transport
    QElapsedTimer startup;
    startup.start();
    while (startup.elapsed() < 1500 && m_playoutFeed && m_playoutFeed->isRunning() && !m_playoutFeed->isStreaming()) {
        QThread::msleep(25);
    }
    if (!m_playoutFeed || !m_playoutFeed->isRunning()) {
        const QString tail = m_playoutFeed ? m_playoutFeed->stderrTail().trimmed() : QString();
        stopFeed(m_playoutFeed);
        if (errorMessage) {
            *errorMessage = tail.isEmpty() ? QStringLiteral("The playout exited immediately.")
                                           : QStringLiteral("The playout failed: %1").arg(tail.section(QLatin1Char('\n'), -3));
        }
        return false;
    }
    m_playbackStartFrame = frame;
    m_playbackCueFrame = frame;
    m_playbackHolding = hold;
    if (hold) {
        emit outputStatus(QStringLiteral("holding %1 at frame %2").arg(QFileInfo(m_playbackFile).fileName()).arg(frame));
    } else {
        emit outputStatus(QStringLiteral("playing %1 (%2)").arg(QFileInfo(m_playbackFile).fileName(), m_playbackDescription));
    }
    return true;
}

bool BackendController::stopManagedFileOutputInternal(QString* errorMessage) {
    Q_UNUSED(errorMessage);
    if (m_playoutFeed) {
        stopFeed(m_playoutFeed);
        emit outputStatus(QStringLiteral("stopped"));
    }
    m_playbackHolding = false;
    m_testPlaybackActive = false;
    return true;
}

// the frame on the wire: frames the pane has shown (it is held back by the
// card preroll, so it shows what the card is outputting) from the start frame
qint64 BackendController::currentPlaybackFrame() const {
    if (m_playbackHolding || s_testMode) {
        return m_playbackCueFrame;
    }
    if (m_playoutFeed && m_playoutFeed->isRunning()) {
        return wrapFrame(m_playbackStartFrame + m_playoutFeed->framesDelivered());
    }
    return m_playbackCueFrame;
}

double BackendController::currentPlaybackSeconds() const {
    return currentPlaybackFrame() / (m_playbackFps > 0 ? m_playbackFps : 25.0);
}

bool BackendController::showPlayFrame(qint64 frame, QString* errorMessage) {
    if (s_testMode || m_playbackFile.isEmpty() || m_previewWidth <= 0 || m_previewHeight <= 0) {
        return true;
    }
    QStringList arguments{QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
                          QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-ss"), QString::number(seekSecondsForFrame(frame), 'f', 6),
                          QStringLiteral("-i"), m_playbackFile,
                          QStringLiteral("-frames:v"), QStringLiteral("1")};
    arguments << previewPipeOutput(m_playbackFilter, m_previewWidth, m_previewHeight);
    QProcess extractor;
    hideConsole(&extractor);
    extractor.start(ffmpegPath(), arguments);
    if (!extractor.waitForFinished(10000)) {
        extractor.kill();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not render frame %1.").arg(frame);
        }
        return false;
    }
    const QByteArray data = extractor.readAllStandardOutput();
    const qsizetype frameBytes = qsizetype(m_previewWidth) * m_previewHeight * 4;
    if (data.size() >= frameBytes) {
        QImage image(m_previewWidth, m_previewHeight, QImage::Format_RGB32);
        std::memcpy(image.bits(), data.constData(), size_t(frameBytes));
        emit outputFrame(image);
    }
    return true;
}

bool BackendController::togglePauseFileOutput() {
    if (s_testMode) {
        m_playbackHolding = !m_playbackHolding;
        return true;
    }
    if (!isFileOutputActive()) {
        // transport start: fresh playout, or resume from the cued / halted frame
        VrecordConfig config = m_playbackConfig;
        return startFileOutput(config, nullptr);
    }
    if (!m_playbackHolding) {
        // pause on the frame that is on the wire right now
        return launchFileOutputProcess(currentPlaybackFrame(), true, nullptr);
    }
    // resume with the frame after the held one
    return launchFileOutputProcess(m_playbackCueFrame + 1, false, nullptr);
}

bool BackendController::haltFileOutputForRelay() {
    if (!isFileOutputActive() || s_testMode) {
        return false;
    }
    const qint64 frame = currentPlaybackFrame();
    stopManagedFileOutputInternal(nullptr);
    m_playbackCueFrame = frame;
    m_playbackHolding = false;
    emit outputStatus(QStringLiteral("halted %1 at frame %2 - relay has the output").arg(QFileInfo(m_playbackFile).fileName()).arg(frame));
    releaseOutputClaim();
    return true;
}

bool BackendController::isFileOutputActive() const {
    return s_testMode ? m_testPlaybackActive : (m_playoutFeed && m_playoutFeed->isRunning());
}

bool BackendController::isFileOutputPaused() const {
    return isFileOutputActive() && m_playbackHolding;
}

bool BackendController::isFileOutputCued() const {
    return !isFileOutputActive() && !m_playbackFile.isEmpty() && m_playbackCueFrame > 0;
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
    if (isFileOutputActive() && !m_playbackHolding) {
        // playing: jump by the frames and keep playing
        return launchFileOutputProcess(currentPlaybackFrame() + frames, false, errorMessage);
    }
    const qint64 target = clampFrame(m_playbackCueFrame + frames);
    if (isFileOutputActive() && m_playbackHolding) {
        // paused: the card and the pane move to the new frame
        return launchFileOutputProcess(target, true, errorMessage);
    }
    // cued / halted (relay may own the output): move the cue, show it in the pane
    m_playbackCueFrame = target;
    return showPlayFrame(target, errorMessage);
}

bool BackendController::seekFileOutput(double seconds, QString* errorMessage) {
    const qint64 target = clampFrame(qRound64(qMax(seconds, 0.0) * (m_playbackFps > 0 ? m_playbackFps : 25.0)));
    if (!isFileOutputActive() && !s_testMode) {
        if (m_playbackFile.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Select a file and play it once first.");
            }
            return false;
        }
        // stopped / halted: cue the frame only - Play starts there
        m_playbackCueFrame = target;
        return showPlayFrame(target, errorMessage);
    }
    // playing keeps playing from there, paused stays paused on that frame
    return launchFileOutputProcess(target, m_playbackHolding, errorMessage);
}

// ---------------------------------------------------------------- connectors

bool BackendController::applyVideoOutputConnection(const VrecordConfig& config, QString* errorMessage) const {
    const QString connection = config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE")).trimmed();
    if (connection.isEmpty() || connection == QStringLiteral("Auto")) {
        return true;
    }
    const QString outputDevice = deviceForOutput(config);
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
    const QString connection = config.value(QStringLiteral("VIDEO_INPUT_CHOICE")).trimmed();
    if (!VrecordOptions::videoInputs().contains(connection)) {
        return true;
    }
    return DeckLinkDiscovery::applyInputConnection(inputDevice, connection, errorMessage);
}

// ---------------------------------------------------------------- test signal

bool BackendController::isTestSignalActive() const {
    if (m_testSignalFeed && m_testSignalFeed->isRunning()) {
        return true;
    }
    QFile pidFile(testSignalPidPath());
    if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return pidAlive(QString::fromUtf8(pidFile.readAll()).trimmed().toLongLong());
}

bool BackendController::stopTestSignalInternal(QString* errorMessage) {
    if (m_testSignalFeed) {
        stopFeed(m_testSignalFeed);
        emit outputStatus(QStringLiteral("stopped"));
    }
    if (!stopTrackedProcess(testSignalPidPath(), errorMessage)) {
        return false;
    }
    if (!s_testMode) {
        sweepOrphanedTestStreams();
    }
    return true;
}

// the test signal goes out in the output standard; on Auto it follows the
// capture Standard (what the deck/monitor chain is set up for)
VideoMode BackendController::outputModeForTestSignal(const VrecordConfig& config) const {
    const QString outputStandard = config.value(QStringLiteral("OUTPUT_STANDARD_CHOICE")).trimmed();
    VideoMode mode;
    if (!outputStandard.isEmpty() && !outputStandard.startsWith(QStringLiteral("Auto"))) {
        mode = VideoModes::forStandard(outputStandard);
    }
    if (!mode.isValid()) {
        mode = VideoModes::forStandard(config.value(QStringLiteral("STANDARD_CHOICE")).trimmed());
    }
    return mode.isValid() ? mode : VideoModes::forStandard(QStringLiteral("NTSC"));
}

bool BackendController::setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage) {
    if (!saveConfig(config, errorMessage)) {
        return false;
    }
    if (!enabled) {
        const bool ok = stopTestSignalInternal(errorMessage);
        releaseOutputClaim();
        return ok;
    }

    const QString outputDevice = deviceForOutput(config);
    if (outputDevice.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Select a hardware video output device first.");
        }
        return false;
    }
    if (!QFileInfo::exists(ffmpegPath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not find ffmpeg backend at %1").arg(QDir::toNativeSeparators(ffmpegPath()));
        }
        return false;
    }
    if (!s_testMode && isShellRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 session is using the card.").arg(m_shellMode);
        }
        return false;
    }

    VideoMode signalMode = outputModeForTestSignal(config);
    const QString pattern = config.value(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE")).trimmed();
    const QString audio = config.value(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE")).trimmed();
    const QString videoSource = testVideoSourceForPattern(pattern, signalMode);
    const QString audioSource = testAudioSource(audio);
    const QStringList videoFilters = videoFilterChainForPattern(pattern, signalMode);

    // the test signal owns the card output exclusively - tear the other
    // outputs down FIRST and let the driver settle, otherwise the connector
    // apply below hits the teardown race and silently fails (the new stream
    // then starts on a stale analog connector and the HDMI monitor stays dark)
    if (!s_testMode) {
        claimOutputFromPreview(outputDevice);
        stopManagedFileOutputInternal(nullptr);
    }
    stopTestSignalInternal(nullptr);
    if (!s_testMode) {
        QThread::msleep(400);
    }

    if (!applyVideoOutputConnection(config, errorMessage)) {
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("-nostdin")
              << QStringLiteral("-hide_banner")
              << QStringLiteral("-loglevel") << QStringLiteral("error")
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
        s_captured << (ffmpegPath() + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
        return true;
    }

    const QString pidPath = testSignalPidPath();
    const QString requestedSize = lockedPatternSize(pattern).isEmpty() ? signalMode.size() : lockedPatternSize(pattern);
    // launch, verify liveness, then verify the card actually locked the
    // requested raster: a silent connector revert (teardown race) would
    // stream the bars into a stale connector while the HDMI monitor stays dark
    const auto launchSignal = [&]() -> bool {
        if (m_managedOutputs) {
            // GUI: one process for the card and the output pane
            const QStringList sizeParts = requestedSize.split(QLatin1Char('x'));
            const int srcW = sizeParts.value(0).toInt();
            const int srcH = sizeParts.value(1).toInt();
            const double aspect = (signalMode.formatCode == QStringLiteral("ntsc") || signalMode.formatCode == QStringLiteral("pal"))
                                          && lockedPatternSize(pattern).isEmpty()
                                      ? VideoModes::displayAspect(signalMode, config.value(QStringLiteral("ASPECT_RATIO_CHOICE")))
                                      : (srcH > 0 ? double(srcW) / srcH : 16.0 / 9.0);
            int width = 0;
            int height = 0;
            VideoModes::previewSize(srcH, aspect, &width, &height);
            FrameFeed::Attempt attempt;
            attempt.label = QStringLiteral("test signal %1").arg(requestedSize);
            attempt.arguments = arguments;
            attempt.arguments << previewPipeOutput(videoFilters.join(QLatin1Char(',')), width, height);
            m_testSignalFeed = startFeed(QStringLiteral("output"), {attempt}, width, height, qRound(2.0 * kDefaultPreroll * signalMode.fps()));
            QThread::msleep(1500);
            if (!m_testSignalFeed || !m_testSignalFeed->isRunning()) {
                const QString tail = m_testSignalFeed ? m_testSignalFeed->stderrTail().trimmed().section(QLatin1Char('\n'), -1) : QString();
                stopFeed(m_testSignalFeed);
                if (errorMessage) {
                    *errorMessage = tail.isEmpty() ? QStringLiteral("Test signal output exited immediately.")
                                                   : QStringLiteral("Test signal output failed: %1").arg(tail);
                }
                return false;
            }
            return true;
        }
        qint64 newPid = 0;
        if (!startDetachedProgramWithLog(ffmpegPath(), arguments, testSignalLogPath(), &newPid, errorMessage)) {
            return false;
        }
        QFile pidFile(pidPath);
        if (pidFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&pidFile);
            out << newPid << '\n';
        }
        QThread::msleep(1500);
        if (!pidAlive(newPid)) {
            const QString logTail = lastLogLines(testSignalLogPath(), 1);
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

    const bool requestedHD = !requestedSize.startsWith(QStringLiteral("720x"));
    if (requestedHD) {
        const QString activeMode = DeckLinkDiscovery::activeOutputModeFourCC(outputDevice, nullptr);
        if (activeMode == QStringLiteral("ntsc") || activeMode == QStringLiteral("pal")) {
            // teardown race hit: re-apply the connector and relaunch once
            stopFeed(m_testSignalFeed);
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
    emit outputStatus(QStringLiteral("test signal: %1, %2").arg(pattern, requestedSize));
    return true;
}

// ---------------------------------------------------------------- helpers

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
