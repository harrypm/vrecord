#include "FrameFeed.h"

#include <QDateTime>
#include <QFile>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>

#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
// every feed process joins one kill-on-close job: if the GUI dies, Windows
// tears the ffmpeg children down with it instead of leaving an orphan that
// keeps holding the DeckLink (the next launch would fail to open the card)
HANDLE feedJob() {
    static HANDLE job = [] {
        HANDLE handle = CreateJobObjectW(nullptr, nullptr);
        if (handle) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return handle;
    }();
    return job;
}

void joinFeedJob(qint64 pid) {
    HANDLE job = feedJob();
    if (!job || pid <= 0) {
        return;
    }
    HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process) {
        AssignProcessToJobObject(job, process);
        CloseHandle(process);
    }
}
#endif

QString lastMeaningfulLine(const QByteArray& tail) {
    const QStringList lines = QString::fromUtf8(tail).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty() && !line.startsWith(QStringLiteral("frame="))) {
            return line;
        }
    }
    return {};
}
}

FrameFeed::FrameFeed(const QString& program, const QList<Attempt>& attempts, int width, int height, QObject* parent)
    : QThread(parent)
    , m_program(program)
    , m_attempts(attempts)
    , m_width(width)
    , m_height(height) {}

FrameFeed::~FrameFeed() {
    stop();
}

void FrameFeed::stop() {
    m_stopRequested = true;
    if (isRunning()) {
        wait(5000);
    }
}

QImage FrameFeed::takeFrame() {
    QMutexLocker lock(&m_mutex);
    m_pending = false;
    return m_latest;
}

QString FrameFeed::activeLabel() const {
    QMutexLocker lock(&m_mutex);
    return m_activeLabel;
}

QString FrameFeed::stderrTail() const {
    QMutexLocker lock(&m_mutex);
    return QString::fromUtf8(m_stderrTail);
}

void FrameFeed::appendStderr(const QByteArray& data) {
    if (data.isEmpty()) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    m_stderrTail.append(data);
    if (m_stderrTail.size() > 16384) {
        m_stderrTail = m_stderrTail.right(16384);
    }
}

void FrameFeed::log(const QString& line) const {
    if (m_logPath.isEmpty()) {
        return;
    }
    QFile file(m_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(QStringLiteral("%1 %2\n").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), line).toUtf8());
    }
}

void FrameFeed::deliver(const QImage& frame) {
    QImage shown = frame;
    ++m_framesReceived;
    if (m_delayFrames > 0) {
        m_delayQueue.push_back(frame);
        if (static_cast<int>(m_delayQueue.size()) <= m_delayFrames) {
            return;
        }
        shown = m_delayQueue.front();
        m_delayQueue.pop_front();
    }
    {
        QMutexLocker lock(&m_mutex);
        m_latest = shown;
    }
    ++m_framesDelivered;
    // coalesce: if the GUI has not picked up the previous frame yet it just
    // gets the newer one - a slow paint never backs the pipe (or the card) up
    if (!m_pending.exchange(true)) {
        emit frameReady();
    }
}

void FrameFeed::run() {
    const qsizetype frameBytes = qsizetype(m_width) * m_height * 4;
    QString lastMessage;
    for (const Attempt& attempt : m_attempts) {
        if (m_stopRequested) {
            return;
        }
        {
            QMutexLocker lock(&m_mutex);
            m_activeLabel = attempt.label;
            m_stderrTail.clear();
        }
        m_delayQueue.clear();

        QProcess process;
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.setStandardInputFile(QProcess::nullDevice());
        QStringList quoted;
        for (const QString& argument : attempt.arguments) {
            quoted << (argument.contains(QLatin1Char(' ')) || argument.isEmpty() ? QStringLiteral("\"%1\"").arg(argument) : argument);
        }
        log(QStringLiteral("[%1] start: %2 %3").arg(attempt.label, m_program, quoted.join(QLatin1Char(' '))));
        process.start(m_program, attempt.arguments);
        if (!process.waitForStarted(5000)) {
            lastMessage = QStringLiteral("could not start %1").arg(m_program);
            emit attemptFailed(attempt.label, lastMessage);
            continue;
        }
        m_pid = process.processId();
#ifdef Q_OS_WIN
        joinFeedJob(m_pid);
#endif

        QByteArray buffer;
        bool hadFrames = false;
        while (!m_stopRequested) {
            if (m_flushRequested.exchange(false) && !m_delayQueue.empty()) {
                const QImage newest = m_delayQueue.back();
                m_delayQueue.clear();
                {
                    QMutexLocker lock(&m_mutex);
                    m_latest = newest;
                }
                if (!m_pending.exchange(true)) {
                    emit frameReady();
                }
            }
            if (!process.waitForReadyRead(50)) {
                appendStderr(process.readAllStandardError());
                if (process.state() == QProcess::NotRunning) {
                    break;
                }
                continue;
            }
            buffer.append(process.readAllStandardOutput());
            appendStderr(process.readAllStandardError());
            if (buffer.size() < frameBytes) {
                continue;
            }
            // only the newest complete frame matters (unless frames are being
            // delayed to match the output preroll, then keep them all in order)
            qsizetype offset = 0;
            while (buffer.size() - offset >= frameBytes) {
                const bool newest = buffer.size() - offset < 2 * frameBytes;
                if (newest || m_delayFrames > 0) {
                    QImage frame(m_width, m_height, QImage::Format_RGB32);
                    std::memcpy(frame.bits(), buffer.constData() + offset, size_t(frameBytes));
                    deliver(frame);
                    if (!hadFrames) {
                        hadFrames = true;
                        m_streaming = true;
                        log(QStringLiteral("[%1] first frame %2x%3").arg(attempt.label).arg(m_width).arg(m_height));
                        emit streaming(attempt.label);
                    }
                }
                offset += frameBytes;
            }
            buffer.remove(0, offset);
        }

        if (m_stopRequested) {
            log(QStringLiteral("[%1] stopped after %2 frames").arg(attempt.label).arg(m_framesReceived.load()));
            process.kill();
            process.waitForFinished(3000);
            m_pid = 0;
            m_streaming = false;
            return;
        }
        process.waitForFinished(1000);
        appendStderr(process.readAllStandardError());
        m_pid = 0;
        m_streaming = false;
        {
            QMutexLocker lock(&m_mutex);
            lastMessage = lastMeaningfulLine(m_stderrTail);
        }
        if (lastMessage.isEmpty()) {
            lastMessage = QStringLiteral("ffmpeg exited with code %1").arg(process.exitCode());
        }
        log(QStringLiteral("[%1] exited (%2 frames): %3").arg(attempt.label).arg(m_framesReceived.load()).arg(lastMessage));
        if (hadFrames) {
            // a stream that was running and then stopped is a real end, not a
            // reason to try the next launch variant
            emit ended(true, lastMessage);
            return;
        }
        emit attemptFailed(attempt.label, lastMessage);
    }
    if (!m_stopRequested) {
        emit ended(false, lastMessage);
    }
}
