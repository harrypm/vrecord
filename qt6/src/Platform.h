#pragma once

// Platform: the operating-system specific pieces of the Qt6 frontend in one
// place, so the rest of the code is the same on Windows, macOS and Linux.
//
//   tools      ffmpeg-ma, deckcontrol, ... : <root>/bin first (every package
//              layout puts them there), then the package's other tool
//              folders, then PATH
//   backend    the vrecord shell script runs under bash: MSYS2 / Git Bash on
//              Windows, a bash 4+ (Homebrew's on macOS) elsewhere
//   processes  hiding consoles, liveness, killing a tree, ending the player
//              windows of a record / passthrough session
//   state      where logs, PID files and the monitor levels file go

#include <QString>
#include <QStringList>

class QProcess;

namespace Platform {

// "ffmpeg-ma" -> "ffmpeg-ma.exe" on Windows, unchanged elsewhere
QString exe(const QString& baseName);

// folders searched for the bundled tools, in order (existing ones only)
QStringList toolDirs(const QString& packageRoot);
// full path of a tool (several base names may be given, first match wins);
// empty when it is nowhere to be found
QString findTool(const QString& packageRoot, const QStringList& baseNames);
QString findTool(const QString& packageRoot, const QString& baseName);
// findTool, or the expected <root>/bin path when missing (for messages)
QString toolPath(const QString& packageRoot, const QString& baseName);
// PATH for child processes: the package tool folders first
QString childPath(const QString& packageRoot, const QString& currentPath);

// bash for the backend script; empty if none is usable
QString findBash();
// Windows: the folder with the POSIX tools that belong to that bash
QString posixToolsDir(const QString& bash);
// Windows: C:\x\y -> /c/x/y for MSYS; unchanged elsewhere
QString toBashPath(const QString& path);
// what the user should install when findBash() comes back empty
QString bashMissingHint();

// Windows: no console window for the child; Linux: the child dies with us
void prepareChild(QProcess* process);
bool pidAlive(qint64 pid);
// terminate a process and all of its descendants
void killTree(qint64 pid);
// end a record / passthrough session the way the user would - close its
// player window - so the recording is finalised. shellPid scopes the search
// to that session on macOS / Linux. Returns how many players were asked.
int closePlayerWindows(qint64 shellPid);
// kill test-signal ffmpegs that outlived their launcher (not our children)
void sweepOrphanedTestStreams(bool background);
// running processes whose executable is <baseName>
int countProcesses(const QString& baseName);

// a folder for logs, PID files and the like (created if needed)
QString stateDir(const QString& packageRoot);
// height in logical pixels of the smallest attached screen (0 if unknown)
int smallestScreenHeight();

}  // namespace Platform
