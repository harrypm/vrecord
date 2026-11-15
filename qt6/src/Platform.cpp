#include "Platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace Platform {

namespace {

#ifndef Q_OS_WIN
struct ProcessEntry {
    qint64 pid = 0;
    qint64 parent = 0;
    QString command;
};

// every process with its parent and full command line (ps works the same on
// macOS and Linux with these options)
QVector<ProcessEntry> processTable() {
    QVector<ProcessEntry> table;
    QProcess ps;
    ps.start(QStringLiteral("ps"), {QStringLiteral("-A"), QStringLiteral("-o"), QStringLiteral("pid=,ppid=,command=")});
    if (!ps.waitForFinished(5000)) {
        ps.kill();
        return table;
    }
    const QStringList lines = QString::fromLocal8Bit(ps.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const qsizetype first = line.indexOf(QLatin1Char(' '));
        if (first < 0) {
            continue;
        }
        const QString rest = line.mid(first).trimmed();
        const qsizetype second = rest.indexOf(QLatin1Char(' '));
        ProcessEntry entry;
        entry.pid = line.left(first).toLongLong();
        entry.parent = (second < 0 ? rest : rest.left(second)).toLongLong();
        entry.command = second < 0 ? QString() : rest.mid(second).trimmed();
        if (entry.pid > 0) {
            table << entry;
        }
    }
    return table;
}

// pid and every descendant, parents before children
QVector<ProcessEntry> processTree(qint64 root) {
    const QVector<ProcessEntry> table = processTable();
    QMultiHash<qint64, int> children;
    int rootIndex = -1;
    for (int i = 0; i < table.size(); ++i) {
        children.insert(table[i].parent, i);
        if (table[i].pid == root) {
            rootIndex = i;
        }
    }
    QVector<ProcessEntry> tree;
    if (rootIndex >= 0) {
        tree << table[rootIndex];
    } else {
        ProcessEntry entry;
        entry.pid = root;
        tree << entry;
    }
    for (int i = 0; i < tree.size(); ++i) {
        for (int child : children.values(tree[i].pid)) {
            tree << table[child];
        }
    }
    return tree;
}

QString executableName(const QString& command) {
    const QString first = command.section(QLatin1Char(' '), 0, 0);
    return QFileInfo(first).fileName();
}
#endif

QStringList appendUnique(QStringList list, const QString& dir) {
    const QString clean = QDir::cleanPath(dir);
    if (!list.contains(clean) && QFileInfo(clean).isDir()) {
        list << clean;
    }
    return list;
}

}  // namespace

QString exe(const QString& baseName) {
#ifdef Q_OS_WIN
    if (baseName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || baseName.endsWith(QStringLiteral(".com"), Qt::CaseInsensitive)) {
        return baseName;
    }
    return baseName + QStringLiteral(".exe");
#else
    return baseName;
#endif
}

QStringList toolDirs(const QString& packageRoot) {
    QStringList dirs;
    dirs = appendUnique(dirs, QDir(packageRoot).filePath(QStringLiteral("bin")));
    dirs = appendUnique(dirs, packageRoot);
#ifndef Q_OS_WIN
    // the .pkg / .deb layout keeps the tools beside the script's prefix
    dirs = appendUnique(dirs, QDir(packageRoot).filePath(QStringLiteral("lib/vrecord/bin")));
    dirs = appendUnique(dirs, QDir(packageRoot).filePath(QStringLiteral("../lib/vrecord/bin")));
    // the macOS .app: the script in Contents/MacOS, the tools in Contents/Resources/bin
    dirs = appendUnique(dirs, QDir(packageRoot).filePath(QStringLiteral("../Resources/bin")));
    dirs = appendUnique(dirs, QStringLiteral("/usr/local/lib/vrecord/bin"));
#endif
    return dirs;
}

QString resourcesDir(const QString& packageRoot) {
    for (const QString& candidate : {QDir(packageRoot).filePath(QStringLiteral("Resources")),
                                     QDir(packageRoot).filePath(QStringLiteral("../share/vrecord/Resources")),
                                     QDir(packageRoot).filePath(QStringLiteral("share/vrecord/Resources"))}) {
        if (QFileInfo(QDir(candidate).filePath(QStringLiteral("vrecord_functions"))).isFile()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

QString findTool(const QString& packageRoot, const QStringList& baseNames) {
    const QStringList dirs = toolDirs(packageRoot);
    for (const QString& base : baseNames) {
        const QString name = exe(base);
        for (const QString& dir : dirs) {
            const QString candidate = QDir(dir).filePath(name);
            const QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                return candidate;
            }
        }
    }
    QStringList fallback;
#ifndef Q_OS_WIN
    // a GUI app started from the Finder / a desktop file has a minimal PATH
    fallback << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin")
             << QStringLiteral("/home/linuxbrew/.linuxbrew/bin") << QStringLiteral("/usr/bin");
#endif
    for (const QString& base : baseNames) {
        QString found = QStandardPaths::findExecutable(exe(base));
        if (found.isEmpty() && !fallback.isEmpty()) {
            found = QStandardPaths::findExecutable(exe(base), fallback);
        }
        if (!found.isEmpty()) {
            return found;
        }
    }
    return {};
}

QString findTool(const QString& packageRoot, const QString& baseName) {
    return findTool(packageRoot, QStringList{baseName});
}

QString toolPath(const QString& packageRoot, const QString& baseName) {
    const QString found = findTool(packageRoot, baseName);
    return found.isEmpty() ? QDir(packageRoot).filePath(QStringLiteral("bin/") + exe(baseName)) : found;
}

QString childPath(const QString& packageRoot, const QString& currentPath) {
    QStringList parts;
    for (const QString& dir : toolDirs(packageRoot)) {
        parts << QDir::toNativeSeparators(dir);
    }
    if (!currentPath.isEmpty()) {
        parts << currentPath;
    }
#ifdef Q_OS_WIN
    return parts.join(QLatin1Char(';'));
#else
    // Finder / desktop launches: keep the package managers' tools reachable
    for (const QString& dir : {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                               QStringLiteral("/home/linuxbrew/.linuxbrew/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/bin")}) {
        if (!currentPath.split(QLatin1Char(':')).contains(dir) && QFileInfo(dir).isDir()) {
            parts << dir;
        }
    }
    return parts.join(QLatin1Char(':'));
#endif
}

QString findBash() {
#ifdef Q_OS_WIN
    const QString msysBash = QStringLiteral("C:/msys64/usr/bin/bash.exe");
    if (QFileInfo::exists(msysBash)) {
        return msysBash;
    }
    const QString gitBash = QStringLiteral("%1/Git/bin/bash.exe").arg(QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")));
    if (QFileInfo::exists(gitBash)) {
        return gitBash;
    }
    return {};
#else
    // the backend needs bash 4 or later; macOS ships 3.2 in /bin, so
    // Homebrew's comes first there
    for (const QString& candidate : {QStringLiteral("/opt/homebrew/bin/bash"), QStringLiteral("/usr/local/bin/bash"),
                                     QStringLiteral("/home/linuxbrew/.linuxbrew/bin/bash"), QStringLiteral("/bin/bash"),
                                     QStringLiteral("/usr/bin/bash")}) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("bash"));
#endif
}

QString posixToolsDir(const QString& bash) {
#ifdef Q_OS_WIN
    const QString dir = QFileInfo(bash).absolutePath();
    if (dir.endsWith(QStringLiteral("/Git/bin"), Qt::CaseInsensitive)) {
        return QDir(dir).filePath(QStringLiteral("../usr/bin"));
    }
    return dir;
#else
    return QFileInfo(bash).absolutePath();
#endif
}

QString toBashPath(const QString& path) {
#ifdef Q_OS_WIN
    QString value = path;
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (value.size() >= 3 && value[1] == QLatin1Char(':') && value[2] == QLatin1Char('/')) {
        const QChar drive = value[0].toLower();
        return QStringLiteral("/%1/%2").arg(drive, value.mid(3));
    }
    return value;
#else
    return path;
#endif
}

QString bashMissingHint() {
#ifdef Q_OS_WIN
    return QStringLiteral("Install MSYS2 (msys2.org) to C:\\msys64, or Git for Windows.");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Install bash 4 or later: brew install bash");
#else
    return QStringLiteral("Install bash with your package manager.");
#endif
}

void prepareChild(QProcess* process) {
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#elif defined(Q_OS_LINUX)
    // like the Windows kill-on-close job: a child must not outlive the GUI
    // and keep holding the DeckLink
    process->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
#else
    Q_UNUSED(process);
#endif
}

bool pidAlive(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!handle) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

void killTree(qint64 pid) {
    if (pid <= 0) {
        return;
    }
#ifdef Q_OS_WIN
    QProcess killer;
    killer.start(QStringLiteral("taskkill.exe"), {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
    killer.waitForFinished(5000);
#else
    const QVector<ProcessEntry> tree = processTree(pid);
    // children first, so nothing gets re-parented and missed
    for (auto it = tree.crbegin(); it != tree.crend(); ++it) {
        ::kill(pid_t(it->pid), SIGKILL);
    }
#endif
}

int closePlayerWindows(qint64 shellPid) {
#ifdef Q_OS_WIN
    Q_UNUSED(shellPid);
    // the shell names its player window "mode:record - ..." / "mode:passthrough
    // - ..." (WINDOW_NAME in _setup_vrecord_process); post WM_CLOSE to each
    int count = 0;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        wchar_t title[64] = {};
        if (IsWindowVisible(hwnd) && GetWindowTextW(hwnd, title, 64) > 0 && wcsncmp(title, L"mode:", 5) == 0) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ++*reinterpret_cast<int*>(param);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count;
#else
    // the player gets that title on its command line (ffplay -window_title,
    // mpv --title); SDL and mpv treat SIGTERM as closing the window, so the
    // capture is finalised as if the user had closed it
    int count = 0;
    for (const ProcessEntry& entry : processTree(shellPid)) {
        if (entry.pid != shellPid && entry.command.contains(QStringLiteral("mode:"))) {
            const QString name = executableName(entry.command);
            if (name.startsWith(QStringLiteral("ffplay")) || name.startsWith(QStringLiteral("mpv"))) {
                ::kill(pid_t(entry.pid), SIGTERM);
                ++count;
            }
        }
    }
    return count;
#endif
}

void sweepOrphanedTestStreams(bool background) {
#ifdef Q_OS_WIN
    // background (GUI): never block the window on PowerShell, and spare our
    // own children (a test signal started meanwhile is not an orphan)
    const QString command = QStringLiteral("Get-CimInstance Win32_Process -Filter \"Name='ffmpeg-ma.exe'\" | "
                                           "Where-Object { $_.CommandLine -match 'vrecord-test-signal' -and $_.ParentProcessId -ne %1 } | "
                                           "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
                                .arg(QCoreApplication::applicationPid());
    const QStringList arguments{QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                                QStringLiteral("-Command"), command};
    if (background) {
        QProcess::startDetached(QStringLiteral("powershell.exe"), arguments);
        return;
    }
    QProcess sweeper;
    sweeper.start(QStringLiteral("powershell.exe"), arguments);
    sweeper.waitForFinished(5000);
#else
    Q_UNUSED(background);   // ps is quick enough to run inline
    const qint64 self = QCoreApplication::applicationPid();
    for (const ProcessEntry& entry : processTable()) {
        if (entry.parent != self && executableName(entry.command).startsWith(QStringLiteral("ffmpeg"))
            && entry.command.contains(QStringLiteral("vrecord-test-signal"))) {
            ::kill(pid_t(entry.pid), SIGKILL);
        }
    }
#endif
}

int countProcesses(const QString& baseName) {
#ifdef Q_OS_WIN
    // tasklist prints "INFO: No tasks are running..." when nothing matches
    const QString image = exe(baseName);
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist.exe"), {QStringLiteral("/NH"), QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq %1").arg(image)});
    tasklist.waitForFinished(5000);
    return QString::fromUtf8(tasklist.readAllStandardOutput()).count(image);
#else
    int count = 0;
    for (const ProcessEntry& entry : processTable()) {
        if (executableName(entry.command) == baseName) {
            ++count;
        }
    }
    return count;
#endif
}

QString stateDir(const QString& packageRoot) {
#ifdef Q_OS_WIN
    // the Windows packages are per-user folders: state lives beside the exe
    return packageRoot;
#else
    // an .app bundle or a system prefix is not ours to write into
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) {
        dir = QDir(QDir::tempPath()).filePath(QStringLiteral("vrecord-qt6"));
    }
    QDir().mkpath(dir);
    return dir;
#endif
}

int smallestScreenHeight() {
#ifdef Q_OS_WIN
    // ffplay (SDL) is DPI-unaware on Windows: logical pixels at the system DPI
    HDC screenDc = GetDC(nullptr);
    const int dpi = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
    if (screenDc) {
        ReleaseDC(nullptr, screenDc);
    }
    const double dpiScale = dpi > 0 ? dpi / 96.0 : 1.0;
    int smallest = 0;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto* value = reinterpret_cast<int*>(data);
            const int height = info.rcMonitor.bottom - info.rcMonitor.top;
            if (height > 0 && (*value == 0 || height < *value)) {
                *value = height;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&smallest));
    return smallest > 0 ? qRound(smallest / dpiScale) : 0;
#else
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        return 0;
    }
    int smallest = 0;
    for (const QScreen* screen : QGuiApplication::screens()) {
        const int height = screen->geometry().height();
        if (height > 0 && (smallest == 0 || height < smallest)) {
            smallest = height;
        }
    }
    return smallest;
#endif
}

}  // namespace Platform
