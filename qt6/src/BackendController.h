#pragma once

#include "VrecordConfig.h"

#include <QObject>
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
    bool startFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool stopFileOutput(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool stopAllOutputs(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool setTestSignalOutput(const VrecordConfig& config, bool enabled, QString* errorMessage = nullptr) const;
    bool applyVideoOutputConnection(const VrecordConfig& config, QString* errorMessage = nullptr) const;
    bool openLegacyUi(QString* errorMessage = nullptr) const;

private:
    bool startProcess(const QStringList& arguments, QString* errorMessage) const;
    bool startDetachedProgram(const QString& program, const QStringList& arguments, QString* errorMessage) const;
    bool startDetachedProgramWithLog(const QString& program, const QStringList& arguments, const QString& logPath, qint64* pid, QString* errorMessage) const;
    QString testSignalPidPath() const;
    QString testSignalLogPath() const;
    QString filePlayPidPath() const;
    QString filePlayLogPath() const;
    bool stopTrackedProcess(const QString& pidFilePath, QString* errorMessage) const;

    QString m_packageRoot;
};
