#include "DeckLinkDiscovery.h"

#include <QScopeGuard>
#include <QStringList>

#include <DeckLinkAPI.h>

#include <comdef.h>

namespace {
QString bstrToQString(BSTR value) {
    return value ? QString::fromWCharArray(value) : QString();
}
}

QVector<DeckLinkDeviceInfo> DeckLinkDiscovery::enumerateDevices(QString* errorMessage) {
    QVector<DeckLinkDeviceInfo> devices;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    });

    if (!comInitialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for DeckLink discovery.");
        }
        return devices;
    }

    IDeckLinkIterator* iterator = nullptr;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DeckLink drivers not found or iterator creation failed.");
        }
        return devices;
    }

    IDeckLink* deckLink = nullptr;
    int index = 1;
    while (iterator->Next(&deckLink) == S_OK && deckLink) {
        DeckLinkDeviceInfo info;
        BSTR modelName = nullptr;
        if (deckLink->GetModelName(&modelName) == S_OK) {
            info.modelName = bstrToQString(modelName);
            SysFreeString(modelName);
        } else {
            info.modelName = QStringLiteral("Unknown DeckLink");
        }

        IDeckLinkProfileAttributes* attributes = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(&attributes)) == S_OK && attributes) {
            int64_t persistentId = 0;
            if (attributes->GetInt(BMDDeckLinkPersistentID, &persistentId) == S_OK) {
                info.persistentId = persistentId;
            }
            int64_t inputMask = 0;
            if (attributes->GetInt(BMDDeckLinkVideoInputConnections, &inputMask) == S_OK) {
                info.videoInputConnections = inputMask;
            }
            int64_t outputMask = 0;
            if (attributes->GetInt(BMDDeckLinkVideoOutputConnections, &outputMask) == S_OK) {
                info.videoOutputConnections = outputMask;
            }
            attributes->Release();
        }

        info.displayName = QStringLiteral("%1 [%2] (%3)")
            .arg(info.modelName, QString::number(index), QString::number(info.persistentId));
        devices.push_back(info);
        deckLink->Release();
        deckLink = nullptr;
        ++index;
    }
    iterator->Release();
    return devices;
}

bool DeckLinkDiscovery::applyConnection(const QString& modelName, int configurationId, const QString& connectionName, QString* errorMessage)
{
    int64_t connectionMask = 0;
    if (connectionName == QStringLiteral("SDI")) {
        connectionMask = bmdVideoConnectionSDI;
    } else if (connectionName == QStringLiteral("HDMI")) {
        connectionMask = bmdVideoConnectionHDMI;
    } else if (connectionName == QStringLiteral("Component")) {
        connectionMask = bmdVideoConnectionComponent;
    } else if (connectionName == QStringLiteral("Composite")) {
        connectionMask = bmdVideoConnectionComposite;
    } else if (connectionName == QStringLiteral("S-Video")) {
        connectionMask = bmdVideoConnectionSVideo;
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unknown video connection: %1").arg(connectionName);
        }
        return false;
    }
    const auto configId = static_cast<BMDDeckLinkConfigurationID>(configurationId);
    return applyConnectionInternal(modelName, configId, connectionName, connectionMask, errorMessage);
}

QStringList DeckLinkDiscovery::connectionNamesFromMask(qint64 mask)
{
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

QString DeckLinkDiscovery::detectInputFormatCode(const QString& modelName, QString* errorMessage)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    });
    if (!comInitialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for input detection.");
        }
        return {};
    }

    IDeckLinkIterator* iterator = nullptr;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DeckLink drivers not found or iterator creation failed.");
        }
        return {};
    }

    QString detectedCode;
    IDeckLink* deckLink = nullptr;
    while (iterator->Next(&deckLink) == S_OK && deckLink) {
        BSTR modelNameBstr = nullptr;
        QString deviceModel;
        if (deckLink->GetModelName(&modelNameBstr) == S_OK) {
            deviceModel = bstrToQString(modelNameBstr);
            SysFreeString(modelNameBstr);
        }
        if (deviceModel != modelName) {
            deckLink->Release();
            continue;
        }

        IDeckLinkStatus* status = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) != S_OK || !status) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the status interface.").arg(modelName);
            }
            deckLink->Release();
            break;
        }

        LONGLONG locked = 0;
        LONGLONG detectedMode = 0;
        const bool signalLocked = status->GetInt(bmdDeckLinkStatusVideoInputSignalLocked, &locked) == S_OK && locked != 0;
        const bool modeDetected = status->GetInt(bmdDeckLinkStatusDetectedVideoInputMode, &detectedMode) == S_OK;
        status->Release();
        deckLink->Release();

        if (!signalLocked || !modeDetected) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("No input signal detected on %1.").arg(modelName);
            }
            break;
        }

        // display modes are fourCCs ('ntsc ', 'Hp25', '4k30', ...): decode the
        // four bytes and trim the padding to get the ffmpeg format_code name
        QString code;
        for (int shift = 24; shift >= 0; shift -= 8) {
            const char character = static_cast<char>((detectedMode >> shift) & 0xFF);
            if (character != ' ') {
                code += QLatin1Char(character);
            }
        }
        detectedCode = code;
        break;
    }

    iterator->Release();
    if (detectedCode.isEmpty() && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("DeckLink device not found: %1").arg(modelName);
    }
    return detectedCode;
}

bool DeckLinkDiscovery::applyOutputConnection(const QString& modelName, const QString& connectionName, QString* errorMessage)
{
    return applyConnection(modelName, static_cast<int>(bmdDeckLinkConfigVideoOutputConnection), connectionName, errorMessage);
}

bool DeckLinkDiscovery::applyInputConnection(const QString& modelName, const QString& connectionName, QString* errorMessage)
{
    return applyConnection(modelName, static_cast<int>(bmdDeckLinkConfigVideoInputConnection), connectionName, errorMessage);
}

QString DeckLinkDiscovery::activeOutputModeFourCC(const QString& modelName, QString* errorMessage)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    });
    if (!comInitialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for output mode probe.");
        }
        return {};
    }

    IDeckLinkIterator* iterator = nullptr;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DeckLink drivers not found or iterator creation failed.");
        }
        return {};
    }

    QString modeCode;
    IDeckLink* deckLink = nullptr;
    while (iterator->Next(&deckLink) == S_OK && deckLink) {
        BSTR modelNameBstr = nullptr;
        QString deviceModel;
        if (deckLink->GetModelName(&modelNameBstr) == S_OK) {
            deviceModel = bstrToQString(modelNameBstr);
            SysFreeString(modelNameBstr);
        }
        if (deviceModel != modelName) {
            deckLink->Release();
            continue;
        }

        IDeckLinkStatus* status = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) != S_OK || !status) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the status interface.").arg(modelName);
            }
            deckLink->Release();
            break;
        }

        LONGLONG activeMode = 0;
        if (status->GetInt(bmdDeckLinkStatusCurrentVideoOutputMode, &activeMode) != S_OK) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 did not report its active output mode.").arg(modelName);
            }
            status->Release();
            deckLink->Release();
            break;
        }
        status->Release();
        deckLink->Release();

        for (int shift = 24; shift >= 0; shift -= 8) {
            const char character = static_cast<char>((activeMode >> shift) & 0xFF);
            if (character != ' ') {
                modeCode += QLatin1Char(character);
            }
        }
        break;
    }

    iterator->Release();
    return modeCode;
}

bool DeckLinkDiscovery::applyConnectionInternal(const QString& modelName, std::int64_t configurationId, const QString& connectionName, std::int64_t connectionMask, QString* errorMessage)
{
    const auto configId = static_cast<BMDDeckLinkConfigurationID>(configurationId);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    });
    if (!comInitialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize COM for DeckLink configuration.");
        }
        return false;
    }

    IDeckLinkIterator* iterator = nullptr;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    if (FAILED(hr) || !iterator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DeckLink drivers not found or iterator creation failed.");
        }
        return false;
    }

    IDeckLink* deckLink = nullptr;
    bool matchedDevice = false;
    while (!matchedDevice && iterator->Next(&deckLink) == S_OK && deckLink) {
        BSTR modelNameBstr = nullptr;
        QString deviceModel;
        if (deckLink->GetModelName(&modelNameBstr) == S_OK) {
            deviceModel = bstrToQString(modelNameBstr);
            SysFreeString(modelNameBstr);
        }
        if (deviceModel != modelName) {
            deckLink->Release();
            deckLink = nullptr;
            continue;
        }
        matchedDevice = true;

        IDeckLinkConfiguration* configuration = nullptr;
        if (deckLink->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(&configuration)) != S_OK || !configuration) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device %1 does not expose the DeckLink configuration interface.").arg(modelName);
            }
            deckLink->Release();
            break;
        }

        const HRESULT setResult = configuration->SetInt(configId, connectionMask);
        if (FAILED(setResult)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("DeckLink SDK rejected output connection %1 on %2 (0x%3) - the connector may not exist on this device.")
                                    .arg(connectionName, modelName, QString::number(static_cast<uint>(setResult), 16));
            }
            configuration->Release();
            deckLink->Release();
            break;
        }

        LONGLONG readback = 0;
        if (configuration->GetInt(configId, &readback) != S_OK || (readback & connectionMask) != connectionMask) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Video output connection %1 on %2 failed SDK readback verification (device reports 0x%3).")
                                    .arg(connectionName, modelName, QString::number(static_cast<qulonglong>(readback), 16));
            }
            configuration->Release();
            deckLink->Release();
            break;
        }

        configuration->WriteConfigurationToPreferences();
        configuration->Release();
        deckLink->Release();
        iterator->Release();
        return true;
    }

    if (!matchedDevice && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("DeckLink device not found: %1").arg(modelName);
    }
    iterator->Release();
    return false;
}
