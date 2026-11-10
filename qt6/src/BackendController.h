#pragma once

#include "FrameFeed.h"
#include "VideoModes.h"
#include "VrecordConfig.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

class QTimer;

class BackendController : public QObject {
    Q_OBJECT
public:
    explicit BackendController(const QString& packageRoot, QObject* parent = nullptr);
    ~BackendController() override;

    QString packageRoot() const;
    bool saveConfig(const VrecordConfig& config, QString* errorMessage = nullptr) const;

    // GUI mode: outputs run as managed processes that also feed the output
    // pane. CLI tools (vsignal, the wiring test) leave the test signal
    // detached so it outlives the tool, tracked through a PID file.
    void setManagedOutputs(bool managed) { m_managedOutputs = managed; }

    static void setTestMode(bool enabled);
    static bool testMode();
    static QStringList capturedCommands();
    static void clearCaptured();
    static bool capturedAnyContaining(const QString& needle);

    // shell backend sessions (vrecord -r / -p / -a): the card is released
    // first, the session runs with the same environment Vrecord.exe gives the
    // GTK frontend, and shellFinished reports the outcome
    bool startRecord(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool startPassthrough(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool startAudioCheck(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopShell();
    bool isShellRunning() const;
    QString shellMode() const { return m_shellMode; }
    QString shellLogPath() const;
    bool openLegacyUi(QString* errorMessage = nullptr) const;

    // hardware output
    bool startFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopAllOutputs(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage = nullptr);
    bool isTestSignalActive() const;
    bool applyVideoOutputConnection(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool applyVideoInputConnection(const VrecordConfig& config, QString* errorMessage = nullptr) const;

    // managed file playout transport (pause holds the card on the exact frame)
    bool togglePauseFileOutput();
    bool stepFileOutput(int frames, QString* errorMessage = nullptr);
    bool seekFileOutput(double seconds, QString* errorMessage = nullptr);
    bool isFileOutputActive() const;
    bool isFileOutputPaused() const;
    bool isFileOutputCued() const;
    double fileOutputPositionSeconds() const;
    double fileOutputDurationSeconds() const;
    double fileOutputFps() const;
    QString fileOutputDescription() const { return m_playbackDescription; }

    // live input preview: native frame rate straight from the capture
    bool startVideoPreview(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopVideoPreview();
    bool isVideoPreviewActive() const;

    static QString deviceForOutput(const VrecordConfig& config);

signals:
    void inputFrame(const QImage& frame);
    void outputFrame(const QImage& frame);
    void inputStatus(const QString& message);
    void outputStatus(const QString& message);
    void shellStarted(const QString& mode);
    void shellFinished(const QString& mode, int exitCode, const QString& message);
    // the driver reports a (new) mode on the live input, e.g. "Hp50"
    void inputModeDetected(const QString& formatCode);

private:
    struct MediaInfo {
        double duration = 0.0;
        int width = 0;
        int height = 0;
        int rateNum = 0;
        int rateDen = 1;
        int sarNum = 1;
        int sarDen = 1;
        QString fieldOrder;
        bool hasAudio = false;
    };

    bool startShellSession(const QString& mode, const QStringList& arguments, const VrecordConfig& config, QString* errorMessage);
    void releaseDeckLink();
    QString ffmpegPath() const;
    QString ffprobePath() const;
    bool startDetachedProgramWithLog(const QString& program, const QStringList& arguments, const QString& logPath, qint64* pid, QString* errorMessage) const;
    QString testSignalPidPath() const;
    QString testSignalLogPath() const;
    bool stopTrackedProcess(const QString& pidFilePath, QString* errorMessage) const;
    bool stopTestSignalInternal(QString* errorMessage);
    bool launchFileOutputProcess(double atSeconds, QString* errorMessage);
    bool stopManagedFileOutputInternal(QString* errorMessage);
    bool probeMediaFile(const QString& file, MediaInfo* info, QString* errorMessage) const;
    // the mode a file plays out in when auto-detect is on: its own mode if the
    // card has it, else the card's largest raster at the file's frame rate
    VideoMode nativePlayoutMode(const MediaInfo& media, const QString& device, QString* note) const;
    double currentPlaybackSeconds() const;
    bool showPlayFrame(double seconds, QString* errorMessage);
    FrameFeed* startFeed(const QString& kind, const QList<FrameFeed::Attempt>& attempts, int width, int height, int delayFrames);
    void stopFeed(QPointer<FrameFeed>& feed);
    void followInputMode(const QString& detected);
    VideoMode outputModeForTestSignal(const VrecordConfig& config) const;

    QString m_packageRoot;
    bool m_managedOutputs = false;

    // shell session
    QProcess* m_shellProcess = nullptr;
    QString m_shellMode;

    // input preview
    QPointer<FrameFeed> m_previewFeed;

    QString m_previewDevice;
    QString m_previewFrameCode;       // the mode the pane raster was sized for
    QString m_lastReportedInputMode;
    VrecordConfig m_previewConfig;
    QTimer* m_inputModeTimer = nullptr;

    // test signal (managed mode)
    QPointer<FrameFeed> m_testSignalFeed;

    // file playout
    QPointer<FrameFeed> m_playoutFeed;
    MediaInfo m_playbackMedia;
    VideoMode m_playbackMode;         // what goes to the card
    QString m_playbackFilter;         // conform chain (may be empty)
    int m_previewWidth = 0;
    int m_previewHeight = 0;
    int m_playbackDelayFrames = 0;
    double m_playbackDuration = 0.0;
    double m_playbackFps = 25.0;
    double m_playbackSeekOffset = 0.0;
    double m_playbackFreezeSeconds = 0.0;
    double m_playbackCueSeconds = 0.0;
    bool m_playbackFrozen = false;
    bool m_playbackJogged = false;
    bool m_testPlaybackActive = false;
    QString m_playbackFile;
    QString m_playbackDevice;
    QString m_playbackDescription;
    VrecordConfig m_playbackConfig;
};
