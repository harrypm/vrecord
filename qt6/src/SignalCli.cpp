#include "BackendController.h"
#include "DeckLinkDiscovery.h"
#include "VrecordConfig.h"

#include <QCoreApplication>
#include <QScopeGuard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include <DeckLinkAPI.h>

#include <comdef.h>

#include <iostream>

namespace {

// tasklist prints "INFO: No tasks are running..." when nothing matches.
int ffmpegProcessCount() {
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist.exe"),
                   {QStringLiteral("/NH"), QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq ffmpeg-ma.exe")});
    tasklist.waitForFinished(5000);
    const QString out = QString::fromUtf8(tasklist.readAllStandardOutput());
    return out.count(QStringLiteral("ffmpeg-ma.exe"));
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

qint64 readPidFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    return QString::fromUtf8(file.readAll()).trimmed().toLongLong();
}

// the launch redirects ffmpeg stderr into the log at info level; the ffmpeg
// status line carries frame=<n> progress - a rising count proves the card's
// output pipeline is actively consuming and scheduling frames
long long frameProgressCount(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    const QString content = QString::fromUtf8(file.readAll());
    long long last = -1;
    QRegularExpression re(QStringLiteral("frame=\\s*(\\d+)"));
    QRegularExpressionMatchIterator it = re.globalMatch(content);
    while (it.hasNext()) {
        last = it.next().captured(1).toLongLong();
    }
    return last;
}

QString lastLogLine(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).trimmed().split('\n');
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

QString describeConnection(LONGLONG value) {
    switch (value) {
        case bmdVideoConnectionSDI: return QStringLiteral("SDI");
        case bmdVideoConnectionHDMI: return QStringLiteral("HDMI");
        case bmdVideoConnectionComponent: return QStringLiteral("Component");
        case bmdVideoConnectionComposite: return QStringLiteral("Composite");
        case bmdVideoConnectionSVideo: return QStringLiteral("S-Video");
        default: return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
    }
}

int runProbe() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    });
    if (!comInitialized) {
        std::cout << "probe: COM init failed" << std::endl;
        return 1;
    }

    IDeckLinkIterator* iterator = nullptr;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        std::cout << "probe: DeckLink iterator unavailable" << std::endl;
        return 1;
    }
    auto iteratorGuard = qScopeGuard([&] { iterator->Release(); });

    IDeckLink* deckLink = nullptr;
    while (iterator->Next(&deckLink) == S_OK && deckLink) {
        auto deviceGuard = qScopeGuard([&] { deckLink->Release(); });
        BSTR nameBstr = nullptr;
        QString name;
        if (deckLink->GetModelName(&nameBstr) == S_OK) {
            name = QString::fromWCharArray(nameBstr);
            SysFreeString(nameBstr);
        }
        std::cout << "Device: " << name.toStdString() << std::endl;

        IDeckLinkConfiguration* configuration = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(&configuration)) == S_OK && configuration) {
            LONGLONG connection = 0;
            if (configuration->GetInt(bmdDeckLinkConfigVideoOutputConnection, &connection) == S_OK) {
                std::cout << "  configured output connection: " << describeConnection(connection).toStdString() << std::endl;
            } else {
                std::cout << "  configured output connection: <unreadable>" << std::endl;
            }
            if (configuration->GetInt(bmdDeckLinkConfigVideoInputConnection, &connection) == S_OK) {
                std::cout << "  configured input connection: " << describeConnection(connection).toStdString() << std::endl;
            } else {
                std::cout << "  configured input connection: <unreadable>" << std::endl;
            }
            configuration->Release();
        }

        IDeckLinkProfileAttributes* attributes = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(&attributes)) == S_OK && attributes) {
            LONGLONG profileId = 0;
            LONGLONG duplex = 0;
            LONGLONG outputConnections = 0;
            if (attributes->GetInt(BMDDeckLinkProfileID, &profileId) == S_OK) {
                std::cout << "  device profile: 0x" << std::hex << profileId << std::dec << std::endl;
            }
            if (attributes->GetInt(BMDDeckLinkDuplex, &duplex) == S_OK) {
                std::cout << "  duplex mode: 0x" << std::hex << duplex << std::dec << std::endl;
            }
            if (attributes->GetInt(BMDDeckLinkVideoOutputConnections, &outputConnections) == S_OK) {
                std::cout << "  supported output connections: 0x" << std::hex << outputConnections << std::dec << std::endl;
            }
            LONGLONG inputConnections = 0;
            if (attributes->GetInt(BMDDeckLinkVideoInputConnections, &inputConnections) == S_OK) {
                std::cout << "  supported input connections: 0x" << std::hex << inputConnections << std::dec << std::endl;
            }
            attributes->Release();
        }

        IDeckLinkStatus* status = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) == S_OK && status) {
            LONGLONG outputMode = 0;
            LONGLONG busy = 0;
            LONGLONG lastPixelFormat = 0;
            if (status->GetInt(bmdDeckLinkStatusCurrentVideoOutputMode, &outputMode) == S_OK) {
                std::cout << "  active output mode: 0x" << std::hex << outputMode << std::dec << std::endl;
            }
            if (status->GetInt(bmdDeckLinkStatusLastVideoOutputPixelFormat, &lastPixelFormat) == S_OK) {
                const char* pix = reinterpret_cast<const char*>(&lastPixelFormat);
                std::cout << "  last output pixel format: " << std::string(pix, 4) << " (0x" << std::hex
                          << lastPixelFormat << ")" << std::dec << std::endl;
            }
            if (status->GetInt(bmdDeckLinkStatusBusy, &busy) == S_OK) {
                std::cout << "  device busy state: " << busy << std::endl;
            }
            LONGLONG inputLocked = 0;
            LONGLONG detectedInputMode = 0;
            if (status->GetInt(bmdDeckLinkStatusVideoInputSignalLocked, &inputLocked) == S_OK) {
                std::cout << "  input signal locked: " << (inputLocked != 0 ? "YES" : "NO") << std::endl;
            }
            if (status->GetInt(bmdDeckLinkStatusDetectedVideoInputMode, &detectedInputMode) == S_OK && detectedInputMode != 0) {
                QString modeCode;
                for (int shift = 24; shift >= 0; shift -= 8) {
                    const char character = static_cast<char>((detectedInputMode >> shift) & 0xFF);
                    if (character != ' ') {
                        modeCode += QLatin1Char(character);
                    }
                }
                std::cout << "  detected input mode: " << modeCode.toStdString() << std::endl;
            }
            status->Release();
        }
    }
    return 0;
}

void printHelp() {
    std::cout << "vsignal - vrecord hardware test signal CLI\n\n"
              << "  vsignal list                             list DeckLink devices and output connections\n"
              << "  vsignal start [--device NAME] [--standard NAME] [--pattern NAME]\n"
              << "                 [--audio NAME] [--connector NAME]\n"
              << "                                           start test signal (uses saved config by default)\n"
              << "  vsignal stop                             stop test signal and file playout\n"
              << "  vsignal status                           show whether a signal is running\n"
              << "  vsignal probe                            read the driver's active output state\n"
              << "  vsignal sweep                            verify every pattern under SD and HD standards\n"
              << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();

    const QString command = arguments.size() > 1 ? arguments.at(1) : QStringLiteral("help");

    if (command == QStringLiteral("probe")) {
        return runProbe();
    }

    const QString packageRoot = QCoreApplication::applicationDirPath();
    const QString ffmpegPath = QDir(packageRoot).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    if (!QFileInfo::exists(ffmpegPath)) {
        std::cout << "ERROR: run vsignal from the vrecord package root (missing "
                  << QDir::toNativeSeparators(ffmpegPath).toStdString() << ")" << std::endl;
        return 2;
    }

    if (command == QStringLiteral("help") || command == QStringLiteral("--help") || command == QStringLiteral("-h")) {
        printHelp();
        return 0;
    }

    BackendController backend(packageRoot);
    VrecordConfig config = VrecordConfig::load(QStringLiteral("vrecord"));

    if (command == QStringLiteral("list")) {
        QString error;
        const QVector<DeckLinkDeviceInfo> devices = DeckLinkDiscovery::enumerateDevices(&error);
        if (devices.isEmpty()) {
            std::cout << "No DeckLink devices found." << std::endl;
            if (!error.isEmpty()) {
                std::cout << error.toStdString() << std::endl;
            }
            return 1;
        }
        for (const auto& device : devices) {
            std::cout << "Device: " << device.ffmpegName.toStdString()
                      << (device.ffmpegName != device.modelName ? QStringLiteral(" (%1)").arg(device.modelName).toStdString() : std::string())
                      << "  (index " << device.persistentId << ")" << std::endl;
        }
        return 0;
    }

    if (command == QStringLiteral("stop")) {
        QString error;
        const bool ok = backend.stopAllOutputs(config, &error);
        std::cout << (ok ? "Stopped all hardware output." : QStringLiteral("Stop failed: %1").arg(error).toStdString())
                  << std::endl;
        return ok ? 0 : 1;
    }

    if (command == QStringLiteral("status")) {
        const qint64 pid = readPidFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.pid")));
        const int ffmpegCount = ffmpegProcessCount();
        std::cout << "Test signal: " << (isPidAlive(pid) || ffmpegCount > 0 ? "RUNNING" : "STOPPED")
                  << " (pid file: " << (pid > 0 ? QString::number(pid) : QString("none")).toStdString()
                  << ", ffmpeg-ma processes: " << ffmpegCount << ")" << std::endl;
        return 0;
    }

    if (command == QStringLiteral("start")) {
        for (int i = 2; i + 1 < arguments.size(); i += 2) {
            const QString key = arguments.at(i);
            const QString value = arguments.at(i + 1);
            if (key == QStringLiteral("--device")) {
                config.setValue(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE"), value);
            } else if (key == QStringLiteral("--standard")) {
                config.setValue(QStringLiteral("STANDARD_CHOICE"), value);
            } else if (key == QStringLiteral("--pattern")) {
                config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), value);
            } else if (key == QStringLiteral("--audio")) {
                config.setValue(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE"), value);
            } else if (key == QStringLiteral("--connector")) {
                config.setValue(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), value);
            } else {
                std::cout << "Unknown option: " << key.toStdString() << std::endl;
                return 2;
            }
        }

        QString error;
        if (!backend.setTestSignalOutput(config, true, &error)) {
            std::cout << "FAILED: " << error.toStdString() << std::endl;
            return 1;
        }
        const qint64 pid = readPidFile(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.pid")));
        std::cout << "Started. Verifying hardware output is live..." << std::endl;
        QThread::msleep(1500);
        const bool live = isPidAlive(pid) || ffmpegProcessCount() > 0;
        if (!live) {
            const QString logTail = lastLogLine(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.log")));
            std::cout << "FAILED: output died immediately. "
                      << (logTail.isEmpty() ? "See VrecordQt6-testsignal.log" : logTail.toStdString()) << std::endl;
            return 1;
        }
        const QString signalLogPath = QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.log"));
        const long long frames1 = frameProgressCount(signalLogPath);
        QThread::msleep(1500);
        const long long frames2 = frameProgressCount(signalLogPath);
        if (frames2 > frames1) {
            std::cout << "VERIFIED: card is consuming and scheduling frames (" << frames1 << " -> " << frames2
                      << "). Signal is going out. Stop with: vsignal stop" << std::endl;
            return 0;
        }
        std::cout << "WARNING: process alive but no frame progress detected (" << frames1 << " -> " << frames2
                  << ") - the card may not be transmitting." << std::endl;
        return 1;
    }

    if (command == QStringLiteral("sweep")) {
        const QString originalStandard = config.value(QStringLiteral("STANDARD_CHOICE"), QStringLiteral("NTSC"));
        const QString originalPattern = config.value(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), QStringLiteral("SMPTE SD Bars (75%)"));
        const QString originalAudio = config.value(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE"), QStringLiteral("1 kHz Tone"));
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
        const QStringList standards{QStringLiteral("NTSC"), QStringLiteral("Hp29 - 1080p29.97")};
        int failures = 0;
        for (const QString& standard : standards) {
            for (const QString& pattern : patterns) {
                config.setValue(QStringLiteral("STANDARD_CHOICE"), standard);
                config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), pattern);
                QString error;
                const bool started = backend.setTestSignalOutput(config, true, &error);
                bool streaming = false;
                QString detail;
                if (started) {
                    QThread::msleep(2000);
                    streaming = ffmpegProcessCount() > 0;
                }
                if (!streaming) {
                    detail = error.isEmpty() ? lastLogLine(QDir(packageRoot).filePath(QStringLiteral("VrecordQt6-testsignal.log")))
                                             : error;
                }
                if (!started || !streaming) {
                    ++failures;
                }
                std::cout << (started && streaming ? "[PASS] " : "[FAIL] ") << "Mode " << standard.toStdString()
                          << " + " << pattern.toStdString() << " - "
                          << (started && streaming ? "streaming" : detail.toStdString()) << std::endl;
                QString stopError;
                backend.setTestSignalOutput(config, false, &stopError);
                QThread::msleep(600);
            }
        }
        config.setValue(QStringLiteral("STANDARD_CHOICE"), originalStandard);
        config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), originalPattern);
        config.setValue(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE"), originalAudio);
        config.save();
        std::cout << "Sweep complete: " << (standards.size() * patterns.size()) << " modes, " << failures
                  << " failures. Saved settings restored." << std::endl;
        return failures == 0 ? 0 : 1;
    }

    std::cout << "Unknown command: " << command.toStdString() << std::endl;
    printHelp();
    return 2;
}
