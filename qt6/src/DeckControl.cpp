#include "DeckControl.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

#include <windows.h>

namespace {
void prepare(QProcess* process, const QString& packageRoot) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), QStringLiteral("%1;%2").arg(QDir::toNativeSeparators(QDir(packageRoot).filePath(QStringLiteral("bin"))),
                                                               env.value(QStringLiteral("PATH"))));
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
}
}  // namespace

DeckControl::DeckControl(const QString& packageRoot, QObject* parent)
    : QObject(parent), m_packageRoot(packageRoot) {
    const QString packaged = QDir(packageRoot).filePath(QStringLiteral("bin/deckcontrol.exe"));
    if (QFileInfo::exists(packaged)) {
        m_program = packaged;
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &DeckControl::poll);
}

void DeckControl::setEnabled(bool enabled) {
    m_enabled = enabled && isAvailable();
    if (m_enabled) {
        poll();
        m_timer->start();
    } else {
        m_timer->stop();
        emit stateChanged(isAvailable() ? QStringLiteral("disabled") : QStringLiteral("deckcontrol not installed"));
        emit timecodeChanged(QStringLiteral("--:--:--:--"));
    }
}

// deckcontrol prints "Could not open serial port (...)" when the card has no
// RS-422 port / no deck answers; otherwise "VTR control state: <state>" etc.
QString DeckControl::describe(const QString& output) {
    if (output.contains(QStringLiteral("Could not open serial port"))) {
        return QStringLiteral("No deck (this card has no RS-422 port, or no deck is connected)");
    }
    if (output.contains(QStringLiteral("Could not"), Qt::CaseInsensitive) || output.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
        for (const QString& line : output.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("Could not"), Qt::CaseInsensitive) || line.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
                return line.trimmed();
            }
        }
    }
    QStringList parts;
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("state:")) || line.contains(QStringLiteral("detect"))) {
            parts << QString(line).remove(QStringLiteral("VTR control state: ")).trimmed();
        }
    }
    return parts.isEmpty() ? QStringLiteral("no reply") : parts.join(QStringLiteral(" - "));
}

void DeckControl::poll() {
    if (!m_enabled || m_pollBusy) {
        return;
    }
    m_pollBusy = true;
    auto* process = new QProcess(this);
    prepare(process, m_packageRoot);
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAll());
        process->deleteLater();
        if (!m_enabled) {
            m_pollBusy = false;
            return;
        }
        emit stateChanged(describe(output));
        if (output.contains(QStringLiteral("Could not open serial port"))) {
            emit timecodeChanged(QStringLiteral("--:--:--:--"));
            m_pollBusy = false;
            return;
        }
        pollTimecode();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit stateChanged(QStringLiteral("deckcontrol could not start"));
            process->deleteLater();
            m_pollBusy = false;
        }
    });
    process->start(m_program, {QStringLiteral("getcurrentstate")});
}

void DeckControl::pollTimecode() {
    auto* process = new QProcess(this);
    prepare(process, m_packageRoot);
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAll());
        process->deleteLater();
        m_pollBusy = false;
        if (!m_enabled) {
            return;
        }
        static const QRegularExpression tc(QStringLiteral("TC=\\s*(\\S+)"));
        const QRegularExpressionMatch match = tc.match(output);
        emit timecodeChanged(match.hasMatch() ? match.captured(1) : QStringLiteral("--:--:--:--"));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_pollBusy = false;
        }
    });
    process->start(m_program, {QStringLiteral("gettimecode")});
}

void DeckControl::command(const QString& verb, const QString& parameter) {
    if (!isAvailable()) {
        emit commandFinished(verb, QStringLiteral("deckcontrol is not installed"));
        return;
    }
    auto* process = new QProcess(this);
    prepare(process, m_packageRoot);
    connect(process, &QProcess::finished, this, [this, process, verb](int, QProcess::ExitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAll());
        process->deleteLater();
        // a command only echoes "Issued command ..." when it went out
        const QString result = describe(output);
        emit commandFinished(verb, result == QStringLiteral("no reply") ? QStringLiteral("sent") : result);
    });
    QStringList arguments{verb};
    if (!parameter.isEmpty()) {
        arguments << parameter;
    }
    process->start(m_program, arguments);
}
