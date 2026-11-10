#pragma once

#include "VrecordConfig.h"

#include <QObject>
#include <QProcess>
#include <QString>

class BackendController : public QObject {
    Q_OBJECT
public:
    explicit BackendController(const QString& packageRoot, QObject* parent = nullptr);

    QString packageRoot() const;
    bool saveConfig(const VrecordConfig& config, QString* errorMessage = nullptr) const;

    static void setTestMode(bool enabled);
    static bool testMode();
    static QStringList capturedCommands();
    static void clearCaptured();
    static bool capturedAnyContaining(const QString& needle);

public slots:
    bool startRecord(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool startPassthrough(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool startAudioCheck(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool startFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopAllOutputs(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage = nullptr);
    bool applyVideoOutputConnection(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool openLegacyUi(QString* errorMessage = nullptr) const;

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

    // preview feeds
    bool startVideoPreview(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopVideoPreview();
    bool isVideoPreviewActive() const;
    bool startOutputFeed(const VrecordConfig& config, QString* errorMessage = nullptr);
    bool stopOutputFeed();
    QString previewImagePath() const;
    QString outputFeedImagePath() const;

private:
    bool startProcess(const QStringList& arguments, QString* errorMessage) const;
    bool startDetachedProgram(const QString& program, const QStringList& arguments, QString* errorMessage) const;
    bool startDetachedProgramWithLog(const QString& program, const QStringList& arguments, const QString& logPath, qint64* pid, QString* errorMessage) const;
    QString testSignalPidPath() const;
    QString testSignalLogPath() const;
    bool stopTrackedProcess(const QString& pidFilePath, QString* errorMessage) const;
    bool launchFileOutputProcess(double atSeconds, QString* errorMessage);
    bool stopManagedFileOutputInternal(QString* errorMessage);
    bool probeMediaFile(const QString& file, double* durationSeconds, double* fps, QString* errorMessage) const;
    bool startOutputFeedProcess();
    QString formatCodeForStandard(const QString& standardChoice) const;
    double parsePlaybackLogSeconds() const;
    double currentPlaybackSeconds() const;
    bool showPlayFrame(double seconds, QString* errorMessage);

    QString m_packageRoot;
    QProcess* m_fileOutputProcess = nullptr;
    QProcess* m_previewProcess = nullptr;
    QProcess* m_outputFeedProcess = nullptr;
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
    VrecordConfig m_playbackConfig;
    QByteArray m_playbackStderrTail;
};
