#include "ComponentStatus.h"

#include "DeckLinkDiscovery.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

namespace {

// package bin first (what the backend runs), then the package root, then PATH
QString locate(const QString& packageRoot, const QStringList& names) {
    const QStringList dirs{QDir(packageRoot).filePath(QStringLiteral("bin")), packageRoot};
    for (const QString& name : names) {
        for (const QString& dir : dirs) {
            const QString candidate = QDir(dir).filePath(name);
            if (QFileInfo::exists(candidate)) {
                return QDir::toNativeSeparators(candidate);
            }
        }
    }
    for (const QString& name : names) {
        const QString onPath = QStandardPaths::findExecutable(name);
        if (!onPath.isEmpty()) {
            return QDir::toNativeSeparators(onPath);
        }
    }
    return {};
}

// run a tool briefly and return its combined output (empty on failure)
QString runTool(const QString& program, const QStringList& arguments, const QString& packageRoot, int timeoutMs = 4000) {
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), QStringLiteral("%1;%2").arg(QDir::toNativeSeparators(QDir(packageRoot).filePath(QStringLiteral("bin"))),
                                                               env.value(QStringLiteral("PATH"))));
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(2000)) {
        return {};
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
    }
    return QString::fromLocal8Bit(process.readAll());
}

QString firstLine(const QString& text) {
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed.left(90);
        }
    }
    return {};
}

ComponentStatus tool(const QString& packageRoot, const QString& name, const QString& purpose, bool required,
                     const QStringList& exeNames, const QStringList& versionArgs, const QString& missingHint) {
    ComponentStatus status;
    status.name = name;
    status.purpose = purpose;
    status.required = required;
    status.path = locate(packageRoot, exeNames);
    status.found = !status.path.isEmpty();
    if (status.found && !versionArgs.isEmpty()) {
        status.version = firstLine(runTool(status.path, versionArgs, packageRoot));
    }
    if (!status.found) {
        status.detail = missingHint;
    }
    return status;
}

}  // namespace

QVector<ComponentStatus> checkComponents(const QString& packageRoot) {
    QVector<ComponentStatus> list;

    // ---- required
    ComponentStatus ffmpeg = tool(packageRoot, QStringLiteral("ffmpeg-ma"), QStringLiteral("capture, encode, preview and playout"), true,
                                  {QStringLiteral("ffmpeg-ma.exe")}, {QStringLiteral("-hide_banner"), QStringLiteral("-version")},
                                  QStringLiteral("Missing from the package bin folder - reinstall the package."));
    if (ffmpeg.found) {
        const QString devices = runTool(ffmpeg.path, {QStringLiteral("-hide_banner"), QStringLiteral("-devices")}, packageRoot);
        const QString filters = runTool(ffmpeg.path, {QStringLiteral("-hide_banner"), QStringLiteral("-filters")}, packageRoot);
        QStringList missing;
        if (!devices.contains(QStringLiteral(" decklink "))) {
            missing << QStringLiteral("DeckLink input/output (built without --enable-decklink)");
        }
        if (!filters.contains(QStringLiteral(" drawtext "))) {
            missing << QStringLiteral("drawtext (view labels / numbers; built without freetype)");
        }
        if (!missing.isEmpty()) {
            ffmpeg.degraded = true;
            ffmpeg.detail = QStringLiteral("Missing: %1").arg(missing.join(QStringLiteral("; ")));
        } else {
            ffmpeg.detail = QStringLiteral("DeckLink in/out and drawtext available");
        }
    }
    list << ffmpeg;
    list << tool(packageRoot, QStringLiteral("ffplay-ma"), QStringLiteral("the monitor window with scopes / views"), true,
                 {QStringLiteral("ffplay-ma.exe")}, {QStringLiteral("-hide_banner"), QStringLiteral("-version")},
                 QStringLiteral("Missing from the package bin folder - reinstall the package."));
    list << tool(packageRoot, QStringLiteral("ffprobe-ma"), QStringLiteral("reading media for playout and reports"), true,
                 {QStringLiteral("ffprobe-ma.exe")}, {QStringLiteral("-hide_banner"), QStringLiteral("-version")},
                 QStringLiteral("Missing from the package bin folder - reinstall the package."));

    ComponentStatus driver;
    driver.name = QStringLiteral("Desktop Video (DeckLink driver)");
    driver.purpose = QStringLiteral("the Blackmagic card itself");
    driver.required = true;
    driver.version = DeckLinkDiscovery::driverVersion();
    driver.found = !driver.version.isEmpty();
    if (driver.found) {
        const auto devices = DeckLinkDiscovery::enumerateDevices();
        QStringList names;
        for (const auto& device : devices) {
            names << device.displayName;
        }
        driver.path = names.isEmpty() ? QStringLiteral("no DeckLink device found") : names.join(QStringLiteral(", "));
        driver.version = QStringLiteral("API %1").arg(driver.version);
        if (names.isEmpty()) {
            driver.degraded = true;
            driver.detail = QStringLiteral("Driver installed but no card is visible.");
        }
    } else {
        driver.detail = QStringLiteral("Install Blackmagic Desktop Video from blackmagicdesign.com/support.");
    }
    list << driver;

    // the backend shell: MSYS2 first, then Git for Windows (what the GUI launches)
    ComponentStatus bash;
    bash.name = QStringLiteral("bash (MSYS2 / Git)");
    bash.purpose = QStringLiteral("runs the vrecord backend script");
    bash.required = true;
    for (const QString& candidate : {QStringLiteral("C:/msys64/usr/bin/bash.exe"), QStringLiteral("C:/Program Files/Git/usr/bin/bash.exe"),
                                     QStringLiteral("C:/Program Files/Git/bin/bash.exe")}) {
        if (QFileInfo::exists(candidate)) {
            bash.path = QDir::toNativeSeparators(candidate);
            break;
        }
    }
    bash.found = !bash.path.isEmpty();
    if (bash.found) {
        bash.version = firstLine(runTool(bash.path, {QStringLiteral("--version")}, packageRoot));
    } else {
        bash.detail = QStringLiteral("Install MSYS2 (msys2.org) to C:\\msys64, or Git for Windows.");
    }
    list << bash;

    ComponentStatus script;
    script.name = QStringLiteral("vrecord backend script");
    script.purpose = QStringLiteral("record / passthrough / audio check");
    script.required = true;
    const QString scriptPath = QDir(packageRoot).filePath(QStringLiteral("vrecord"));
    script.found = QFileInfo::exists(scriptPath);
    script.path = QDir::toNativeSeparators(scriptPath);
    if (!script.found) {
        script.detail = QStringLiteral("Missing from the package folder - reinstall the package.");
    }
    list << script;

    list << tool(packageRoot, QStringLiteral("MediaInfo"), QStringLiteral("technical metadata of each recording"), true,
                 {QStringLiteral("MediaInfo.exe"), QStringLiteral("mediainfo.exe")}, {QStringLiteral("--Version")},
                 QStringLiteral("Missing from the package bin folder."));
    list << tool(packageRoot, QStringLiteral("xmlstarlet"), QStringLiteral("reading the capture reports"), true,
                 {QStringLiteral("xmlstarlet.exe"), QStringLiteral("xml.exe")}, {QStringLiteral("--version")},
                 QStringLiteral("Missing from the package bin folder."));

    // ---- optional (the GTK version's "Optional Tools")
    ComponentStatus deck = tool(packageRoot, QStringLiteral("deckcontrol"), QStringLiteral("RS-422 deck control (play, stop, rewind ...) and deck timecode"), false,
                                {QStringLiteral("deckcontrol.exe")}, {}, QStringLiteral("Not packaged."));
    if (deck.found) {
        deck.version = QStringLiteral("present");
    }
    list << deck;
    list << tool(packageRoot, QStringLiteral("mkvpropedit"), QStringLiteral("embedding the capture logs into Matroska files"), false,
                 {QStringLiteral("mkvpropedit.exe")}, {QStringLiteral("--version")}, QStringLiteral("Install MKVToolNix."));
    list << tool(packageRoot, QStringLiteral("mediaconch"), QStringLiteral("policy checks of each recording after capture"), false,
                 {QStringLiteral("mediaconch.exe"), QStringLiteral("MediaConch.exe")}, {QStringLiteral("--version")},
                 QStringLiteral("Install the MediaConch CLI (mediaarea.net)."));
    list << tool(packageRoot, QStringLiteral("mpv"), QStringLiteral("the Quality Control View (mpv) monitor"), false,
                 {QStringLiteral("mpv.com"), QStringLiteral("mpv.exe")}, {QStringLiteral("--version")}, QStringLiteral("Install mpv (mpv.io)."));
    list << tool(packageRoot, QStringLiteral("dvrescue"), QStringLiteral("DV capture and repair"), false,
                 {QStringLiteral("dvrescue.exe"), QStringLiteral("DVRescue.exe")}, {QStringLiteral("--version")},
                 QStringLiteral("Install the DVRescue CLI (mipops.github.io/dvrescue)."));
    list << tool(packageRoot, QStringLiteral("gnuplot"), QStringLiteral("graphs from QCTools reports"), false,
                 {QStringLiteral("gnuplot.exe")}, {QStringLiteral("--version")}, QStringLiteral("Not packaged - install gnuplot (gnuplot.info)."));
    list << tool(packageRoot, QStringLiteral("dvpackager"), QStringLiteral("splitting / wrapping DV captures"), false,
                 {QStringLiteral("dvpackager"), QStringLiteral("dvpackager.exe")}, {}, QStringLiteral("Not packaged - vendored copy: Release/vendor/dvpackager."));
    return list;
}
