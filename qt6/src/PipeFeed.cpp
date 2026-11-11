#include "PipeFeed.h"

#include <QMutexLocker>

#include <cstring>

#include <windows.h>

PipeFeed::PipeFeed(const QString& pipeName, int width, int height, QObject* parent)
    : QThread(parent)
    , m_name(pipeName)
    , m_width(width)
    , m_height(height) {}

PipeFeed::~PipeFeed() {
    stop();
}

void PipeFeed::stop() {
    m_stop = true;
    // unblock ConnectNamedPipe (nobody connected yet) by connecting ourselves,
    // and a pending ReadFile by cancelling the worker's synchronous I/O
    if (HANDLE thread = static_cast<HANDLE>(m_thread.load())) {
        CancelSynchronousIo(thread);
    }
    const std::wstring path = (QStringLiteral("\\\\.\\pipe\\") + m_name).toStdWString();
    HANDLE client = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (client != INVALID_HANDLE_VALUE) {
        CloseHandle(client);
    }
    if (isRunning()) {
        wait(3000);
    }
}

QImage PipeFeed::takeFrame() {
    QMutexLocker lock(&m_mutex);
    m_pending = false;
    return m_latest;
}

void PipeFeed::run() {
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
    m_thread = self;
    const qsizetype frameBytes = qsizetype(m_width) * m_height * 4;
    const std::wstring path = (QStringLiteral("\\\\.\\pipe\\") + m_name).toStdWString();
    HANDLE pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                   0, DWORD(qMin<qsizetype>(frameBytes * 4, 64 * 1024 * 1024)), 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        m_thread = nullptr;
        CloseHandle(self);
        emit ended();
        return;
    }
    m_pipe = pipe;
    const BOOL ok = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (ok && !m_stop) {
        emit connected();
        QByteArray buffer;
        buffer.reserve(frameBytes * 2);
        std::vector<char> chunk(size_t(qMax<qsizetype>(frameBytes, 1 << 20)));
        while (!m_stop) {
            DWORD got = 0;
            if (!ReadFile(pipe, chunk.data(), DWORD(chunk.size()), &got, nullptr) || got == 0) {
                break;   // writer gone, or cancelled by stop()
            }
            buffer.append(chunk.data(), got);
            if (buffer.size() < frameBytes) {
                continue;
            }
            // newest complete frame only
            const qsizetype whole = (buffer.size() / frameBytes) * frameBytes;
            QImage frame(m_width, m_height, QImage::Format_RGB32);
            std::memcpy(frame.bits(), buffer.constData() + whole - frameBytes, size_t(frameBytes));
            buffer.remove(0, whole);
            m_frames += whole / frameBytes;
            {
                QMutexLocker lock(&m_mutex);
                m_latest = frame;
            }
            if (!m_pending.exchange(true)) {
                emit frameReady();
            }
        }
    }
    m_pipe = nullptr;
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    m_thread = nullptr;
    CloseHandle(self);
    emit ended();
}
