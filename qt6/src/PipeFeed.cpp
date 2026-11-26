#include "PipeFeed.h"

#include <QMutexLocker>

#include <QFile>

#include <cstring>
#include <vector>

#ifdef Q_OS_WIN

#include <windows.h>

PipeFeed::PipeFeed(const QString& pipeName, int width, int height, QObject* parent)
    : QThread(parent)
    , m_name(pipeName)
    , m_width(width)
    , m_height(height) {}

PipeFeed::~PipeFeed() {
    stop();
}

QString PipeFeed::clientPath() const {
    return QStringLiteral("//./pipe/%1").arg(m_name);
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

#else   // macOS / Linux: a FIFO

#include <QDir>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

PipeFeed::PipeFeed(const QString& pipeName, int width, int height, QObject* parent)
    : QThread(parent)
    , m_name(pipeName)
    , m_width(width)
    , m_height(height) {
    // created now, before the session starts, so the writer can never open
    // the path first and make a plain file of it
    const QByteArray path = QFile::encodeName(clientPath());
    ::unlink(path.constData());
    ::mkfifo(path.constData(), 0600);
}

PipeFeed::~PipeFeed() {
    stop();
    QFile::remove(clientPath());
}

QString PipeFeed::clientPath() const {
    return QDir(QDir::tempPath()).filePath(m_name + QStringLiteral(".fifo"));
}

void PipeFeed::stop() {
    m_stop = true;
    // a reader still waiting in open() for its writer is released by
    // opening the write end ourselves; a running read loop polls m_stop
    const QByteArray path = QFile::encodeName(clientPath());
    const int client = ::open(path.constData(), O_WRONLY | O_NONBLOCK);
    if (client >= 0) {
        ::close(client);
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
    const qsizetype frameBytes = qsizetype(m_width) * m_height * 4;
    const QByteArray path = QFile::encodeName(clientPath());
    struct stat info {};
    if (::stat(path.constData(), &info) != 0 || !S_ISFIFO(info.st_mode)) {
        emit ended();
        return;
    }
    // blocks until the writer (or stop()) opens the other end
    const int fd = ::open(path.constData(), O_RDONLY);
    if (fd < 0 || m_stop) {
        if (fd >= 0) {
            ::close(fd);
        }
        ::unlink(path.constData());
        emit ended();
        return;
    }
    emit connected();
    QByteArray buffer;
    buffer.reserve(frameBytes * 2);
    std::vector<char> chunk(size_t(qMax<qsizetype>(frameBytes, 1 << 20)));
    while (!m_stop) {
        pollfd waitFor{fd, POLLIN, 0};
        const int ready = ::poll(&waitFor, 1, 100);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            if (ready < 0) {
                break;
            }
            continue;   // no data yet: check m_stop again
        }
        const ssize_t got = ::read(fd, chunk.data(), chunk.size());
        if (got <= 0) {
            if (got < 0 && errno == EINTR) {
                continue;
            }
            break;   // writer gone
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
    // closing the read end makes the writer fail and exit (SIGPIPE / EPIPE),
    // which ends a record session like closing the ffplay window does
    ::close(fd);
    ::unlink(path.constData());
    emit ended();
}

#endif
