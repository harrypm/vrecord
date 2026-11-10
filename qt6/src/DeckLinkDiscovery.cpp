#include "DeckLinkDiscovery.h"

#include <QScopeGuard>
#include <QStringList>

#include <DeckLinkAPI.h>

#include <comdef.h>

namespace {
QString bstrToQString(BSTR value) {
    return value ? QString::fromWCharArray(value) : QString();
}

QString deviceDisplayName(IDeckLink* deckLink) {
    BSTR name = nullptr;
    QString result;
    if (deckLink->GetDisplayName(&name) == S_OK) {
        result = bstrToQString(name);
        SysFreeString(name);
    }
    return result;
}

QString deviceModelName(IDeckLink* deckLink) {
    BSTR name = nullptr;
    QString result;
    if (deckLink->GetModelName(&name) == S_OK) {
        result = bstrToQString(name);
        SysFreeString(name);
    }
    return result;
}

// display modes are fourCCs ('ntsc', 'Hp25', '4k30', ...): decode the four
// bytes and trim the padding to get the ffmpeg format_code name
QString fourCC(qint64 value) {
    QString code;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const char character = static_cast<char>((value >> shift) & 0xFF);
        if (character != ' ' && character != '\0') {
            code += QLatin1Char(character);
        }
    }
    return code;
}

// COM for the calling thread; balanced only if this call initialised it (the
// Qt GUI thread is already an OLE STA, so there it is a no-op)
struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

IDeckLinkIterator* createIterator(QString* errorMessage) {
    IDeckLinkIterator* iterator = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator,
                                        reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DeckLink drivers not found (Blackmagic Desktop Video is not installed or the iterator could not be created, 0x%1).")
                                .arg(QString::number(static_cast<uint>(hr), 16));
        }
        return nullptr;
    }
    return iterator;
}

int64_t connectionMaskForName(const QString& name) {
    if (name == QStringLiteral("SDI")) return bmdVideoConnectionSDI;
    if (name == QStringLiteral("HDMI")) return bmdVideoConnectionHDMI;
    if (name == QStringLiteral("Optical SDI")) return bmdVideoConnectionOpticalSDI;
    if (name == QStringLiteral("Component")) return bmdVideoConnectionComponent;
    if (name == QStringLiteral("Composite")) return bmdVideoConnectionComposite;
    if (name == QStringLiteral("S-Video")) return bmdVideoConnectionSVideo;
    return 0;
}
}

QVector<DeckLinkDeviceInfo> DeckLinkDiscovery::enumerateDevices(QString* errorMessage) {
    QVector<DeckLinkDeviceInfo> devices;
    ComScope com;
    if (!com.ok()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for DeckLink discovery.");
        }
        return devices;
    }
    IDeckLinkIterator* iterator = createIterator(errorMessage);
    if (!iterator) {
        return devices;
    }

    IDeckLink* deckLink = nullptr;
    int index = 1;
    while (iterator->Next(&deckLink) == S_OK && deckLink) {
        DeckLinkDeviceInfo info;
        info.modelName = deviceModelName(deckLink);
        if (info.modelName.isEmpty()) {
            info.modelName = QStringLiteral("Unknown DeckLink");
        }
        info.ffmpegName = deviceDisplayName(deckLink);
        if (info.ffmpegName.isEmpty()) {
            info.ffmpegName = info.modelName;
        }

        IDeckLinkProfileAttributes* attributes = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(&attributes)) == S_OK && attributes) {
            LONGLONG value = 0;
            if (attributes->GetInt(BMDDeckLinkPersistentID, &value) == S_OK) {
                info.persistentId = value;
            }
            if (attributes->GetInt(BMDDeckLinkVideoInputConnections, &value) == S_OK) {
                info.videoInputConnections = value;
            }
            if (attributes->GetInt(BMDDeckLinkVideoOutputConnections, &value) == S_OK) {
                info.videoOutputConnections = value;
            }
            if (attributes->GetInt(BMDDeckLinkDuplex, &value) == S_OK) {
                info.supportsFullDuplex = (value == bmdDuplexFull);
            }
            BOOL flag = FALSE;
            if (attributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &flag) == S_OK) {
                info.supportsInputFormatDetection = flag != FALSE;
            }
            attributes->Release();
        }

        info.displayName = info.ffmpegName == info.modelName
            ? QStringLiteral("%1 [%2]").arg(info.ffmpegName, QString::number(index))
            : QStringLiteral("%1 (%2) [%3]").arg(info.ffmpegName, info.modelName, QString::number(index));
        devices.push_back(info);
        deckLink->Release();
        deckLink = nullptr;
        ++index;
    }
    iterator->Release();
    if (devices.isEmpty() && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("No DeckLink devices found - check the card is seated and Desktop Video Setup can see it.");
    }
    return devices;
}

bool DeckLinkDiscovery::withDevice(const QString& deviceName, const std::function<bool(IDeckLink*)>& action, QString* errorMessage) {
    ComScope com;
    if (!com.ok()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for DeckLink access.");
        }
        return false;
    }
    // exact display-name match first (distinguishes "DeckLink Duo (1)" from
    // "(2)"), then the model name older configs stored
    for (int pass = 0; pass < 2; ++pass) {
        IDeckLinkIterator* iterator = createIterator(errorMessage);
        if (!iterator) {
            return false;
        }
        auto releaseIterator = qScopeGuard([iterator] { iterator->Release(); });
        IDeckLink* deckLink = nullptr;
        while (iterator->Next(&deckLink) == S_OK && deckLink) {
            auto releaseDevice = qScopeGuard([deckLink] { deckLink->Release(); });
            const QString candidate = pass == 0 ? deviceDisplayName(deckLink) : deviceModelName(deckLink);
            if (candidate == deviceName) {
                return action(deckLink);
            }
            deckLink = nullptr;
        }
    }
    if (errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("DeckLink device not found: %1").arg(deviceName);
    }
    return false;
}

QStringList DeckLinkDiscovery::connectionNamesFromMask(qint64 mask) {
    QStringList names;
    const struct { qint64 bit; const char* name; } mapping[] = {
        {bmdVideoConnectionSDI, "SDI"},
        {bmdVideoConnectionHDMI, "HDMI"},
        {bmdVideoConnectionOpticalSDI, "Optical SDI"},
        {bmdVideoConnectionComponent, "Component"},
        {bmdVideoConnectionComposite, "Composite"},
        {bmdVideoConnectionSVideo, "S-Video"},
    };
    for (const auto& entry : mapping) {
        if (mask & entry.bit) {
            names << QLatin1String(entry.name);
        }
    }
    return names;
}

QStringList DeckLinkDiscovery::outputModeCodes(const QString& deviceName, QString* errorMessage) {
    QStringList codes;
    withDevice(deviceName, [&](IDeckLink* deckLink) {
        IDeckLinkOutput* output = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&output)) != S_OK || !output) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 has no video output.").arg(deviceName);
            }
            return false;
        }
        auto releaseOutput = qScopeGuard([output] { output->Release(); });
        IDeckLinkDisplayModeIterator* modes = nullptr;
        if (output->GetDisplayModeIterator(&modes) != S_OK || !modes) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 did not list its output modes.").arg(deviceName);
            }
            return false;
        }
        IDeckLinkDisplayMode* mode = nullptr;
        while (modes->Next(&mode) == S_OK && mode) {
            codes << fourCC(mode->GetDisplayMode());
            mode->Release();
            mode = nullptr;
        }
        modes->Release();
        return true;
    }, errorMessage);
    return codes;
}

QString DeckLinkDiscovery::detectInputFormatCode(const QString& deviceName, QString* errorMessage, bool allowStale) {
    QString detected;
    withDevice(deviceName, [&](IDeckLink* deckLink) {
        IDeckLinkStatus* status = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) != S_OK || !status) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the status interface.").arg(deviceName);
            }
            return false;
        }
        LONGLONG locked = 0;
        LONGLONG mode = 0;
        const bool signalLocked = status->GetInt(bmdDeckLinkStatusVideoInputSignalLocked, &locked) == S_OK && locked != 0;
        const bool modeDetected = status->GetInt(bmdDeckLinkStatusDetectedVideoInputMode, &mode) == S_OK;
        status->Release();
        if ((!signalLocked && !allowStale) || !modeDetected || mode == 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("No input signal detected on %1.").arg(deviceName);
            }
            return false;
        }
        detected = fourCC(mode);
        return true;
    }, errorMessage);
    return detected;
}

QString DeckLinkDiscovery::activeOutputModeFourCC(const QString& deviceName, QString* errorMessage) {
    QString modeCode;
    withDevice(deviceName, [&](IDeckLink* deckLink) {
        IDeckLinkStatus* status = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) != S_OK || !status) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the status interface.").arg(deviceName);
            }
            return false;
        }
        LONGLONG activeMode = 0;
        const bool ok = status->GetInt(bmdDeckLinkStatusCurrentVideoOutputMode, &activeMode) == S_OK;
        status->Release();
        if (!ok) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 did not report its active output mode.").arg(deviceName);
            }
            return false;
        }
        modeCode = fourCC(activeMode);
        return true;
    }, errorMessage);
    return modeCode;
}

bool DeckLinkDiscovery::applyOutputConnection(const QString& deviceName, const QString& connectionName, QString* errorMessage) {
    return applyConnection(deviceName, bmdDeckLinkConfigVideoOutputConnection, connectionName, errorMessage);
}

bool DeckLinkDiscovery::applyInputConnection(const QString& deviceName, const QString& connectionName, QString* errorMessage) {
    return applyConnection(deviceName, bmdDeckLinkConfigVideoInputConnection, connectionName, errorMessage);
}

bool DeckLinkDiscovery::applyConnection(const QString& deviceName, std::int64_t configurationId, const QString& connectionName, QString* errorMessage) {
    const int64_t connectionMask = connectionMaskForName(connectionName);
    if (connectionMask == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unknown video connection: %1").arg(connectionName);
        }
        return false;
    }
    const auto configId = static_cast<BMDDeckLinkConfigurationID>(configurationId);
    const QString direction = configId == bmdDeckLinkConfigVideoInputConnection ? QStringLiteral("input") : QStringLiteral("output");
    return withDevice(deviceName, [&](IDeckLink* deckLink) {
        IDeckLinkConfiguration* configuration = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(&configuration)) != S_OK || !configuration) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the DeckLink configuration interface.").arg(deviceName);
            }
            return false;
        }
        auto release = qScopeGuard([configuration] { configuration->Release(); });
        const HRESULT setResult = configuration->SetInt(configId, connectionMask);
        if (FAILED(setResult)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("DeckLink SDK rejected video %1 connection %2 on %3 (0x%4) - the connector may not exist on this device.")
                                    .arg(direction, connectionName, deviceName, QString::number(static_cast<uint>(setResult), 16));
            }
            return false;
        }
        LONGLONG readback = 0;
        if (configuration->GetInt(configId, &readback) != S_OK || (readback & connectionMask) != connectionMask) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Video %1 connection %2 on %3 failed SDK readback verification (device reports 0x%4).")
                                    .arg(direction, connectionName, deviceName, QString::number(static_cast<qulonglong>(readback), 16));
            }
            return false;
        }
        configuration->WriteConfigurationToPreferences();
        return true;
    }, errorMessage);
}
