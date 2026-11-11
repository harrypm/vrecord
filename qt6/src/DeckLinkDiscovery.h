#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <functional>

struct IDeckLink;

struct DeckLinkDeviceInfo {
    QString displayName;      // label for the GUI combos
    QString ffmpegName;       // IDeckLink::GetDisplayName - what ffmpeg's -i / -f decklink matches
    QString modelName;        // IDeckLink::GetModelName
    qint64 persistentId = 0;
    qint64 videoInputConnections = 0;
    qint64 videoOutputConnections = 0;
    bool supportsInputFormatDetection = false;
    bool supportsFullDuplex = false;   // capture and playout at the same time
};

class DeckLinkDiscovery {
public:
    static QVector<DeckLinkDeviceInfo> enumerateDevices(QString* errorMessage = nullptr);
    static QStringList connectionNamesFromMask(qint64 mask);
    // deviceName: the ffmpeg/display name stored in the config (model names
    // from older configs still match)
    static bool applyOutputConnection(const QString& deviceName, const QString& connectionName, QString* errorMessage = nullptr);
    static bool applyInputConnection(const QString& deviceName, const QString& connectionName, QString* errorMessage = nullptr);
    static QString activeOutputModeFourCC(const QString& deviceName, QString* errorMessage = nullptr);
    // display modes the device can play out, as ffmpeg format codes
    // ('ntsc', 'Hp50', '4k30', ...)
    static QStringList outputModeCodes(const QString& deviceName, QString* errorMessage = nullptr);
    // allowStale: return the detected mode without requiring the lock flag.
    // The Intensity Pro 4K reports 'locked: NO' even mid-capture while the
    // detected mode is live; idle, the driver keeps the last detected mode.
    static QString detectInputFormatCode(const QString& deviceName, QString* errorMessage = nullptr, bool allowStale = false);
    // Desktop Video driver / API version ("15.3"), empty if not installed
    static QString driverVersion();

private:
    static bool withDevice(const QString& deviceName, const std::function<bool(IDeckLink*)>& action, QString* errorMessage);
    static bool applyConnection(const QString& deviceName, std::int64_t configurationId, const QString& connectionName, QString* errorMessage);
};
