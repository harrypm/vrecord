#pragma once

// DeckLinkPlatform: the few places where the Blackmagic DeckLink SDK differs
// between Windows (COM: CoCreateInstance, BSTR, LONGLONG, BOOL), macOS
// (CFPlugIn: CFStringRef, int64_t, bool) and Linux (dlopen: const char*,
// int64_t, bool). The interfaces themselves are the same everywhere.

#include <QString>

#include <DeckLinkAPI.h>

#include <cstdint>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <comdef.h>
#endif

namespace DeckLinkPlatform {

#if defined(Q_OS_WIN)
using DLString = BSTR;
using DLInt = LONGLONG;
using DLBool = BOOL;
inline QString toQString(DLString value) { return value ? QString::fromWCharArray(value) : QString(); }
inline void freeString(DLString value) { SysFreeString(value); }
#elif defined(Q_OS_MACOS)
using DLString = CFStringRef;
using DLInt = int64_t;
using DLBool = bool;
inline QString toQString(DLString value) { return value ? QString::fromCFString(value) : QString(); }
inline void freeString(DLString value) {
    if (value) {
        CFRelease(value);
    }
}
#else
using DLString = const char*;
using DLInt = int64_t;
using DLBool = bool;
inline QString toQString(DLString value) { return value ? QString::fromUtf8(value) : QString(); }
inline void freeString(DLString value) { std::free(const_cast<char*>(value)); }
#endif

// call an SDK getter that hands out a string the caller must free:
//   takeString([&](DLString* s) { return deckLink->GetModelName(s); })
template <typename Getter>
QString takeString(Getter getter) {
    DLString value = nullptr;
    QString result;
    if (getter(&value) == S_OK && value) {
        result = toQString(value);
        freeString(value);
    }
    return result;
}

// COM for the calling thread on Windows (balanced only if this call
// initialised it; the Qt GUI thread is already an OLE STA); nothing elsewhere
struct ComScope {
#ifdef Q_OS_WIN
    HRESULT hr;
    explicit ComScope(bool multithreaded = false)
        : hr(CoInitializeEx(nullptr, multithreaded ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
#else
    explicit ComScope(bool = false) {}
    bool ok() const { return true; }
#endif
};

// nullptr when the Desktop Video driver is missing; *result gets the reason
inline IDeckLinkIterator* createIterator(HRESULT* result = nullptr) {
#ifdef Q_OS_WIN
    IDeckLinkIterator* iterator = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator,
                                        reinterpret_cast<void**>(&iterator));
    if (result) {
        *result = hr;
    }
    return SUCCEEDED(hr) ? iterator : nullptr;
#else
    IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
    if (result) {
        *result = iterator ? S_OK : E_FAIL;
    }
    return iterator;
#endif
}

inline IDeckLinkAPIInformation* createApiInformation() {
#ifdef Q_OS_WIN
    IDeckLinkAPIInformation* info = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CDeckLinkAPIInformation, nullptr, CLSCTX_ALL, IID_IDeckLinkAPIInformation,
                                reinterpret_cast<void**>(&info)))) {
        return nullptr;
    }
    return info;
#else
    return CreateDeckLinkAPIInformationInstance();
#endif
}

}  // namespace DeckLinkPlatform
