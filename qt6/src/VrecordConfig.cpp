#include "VrecordConfig.h"

#include "VrecordOptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {
// the shell sources this file, so values are bash double-quoted strings:
// \, ", $ and ` are escaped on save and unescaped on load (configs the shell
// wrote itself carry unescaped Windows paths like C:\Captures - a backslash
// before any other character is literal in bash and is kept as-is here too)
QString unquote(QString value) {
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
        value = value.mid(1, value.size() - 2);
        QString out;
        out.reserve(value.size());
        for (qsizetype i = 0; i < value.size(); ++i) {
            const QChar c = value.at(i);
            if (c == QLatin1Char('\\') && i + 1 < value.size()) {
                const QChar next = value.at(i + 1);
                if (next == QLatin1Char('\\') || next == QLatin1Char('"') || next == QLatin1Char('$') || next == QLatin1Char('`')) {
                    out += next;
                    ++i;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }
    return value;
}

QString bashQuote(const QString& value) {
    QString out;
    out.reserve(value.size() + 2);
    out += QLatin1Char('"');
    for (const QChar c : value) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"') || c == QLatin1Char('$') || c == QLatin1Char('`')) {
            out += QLatin1Char('\\');
        }
        out += c;
    }
    out += QLatin1Char('"');
    return out;
}
}

VrecordConfig VrecordConfig::load(const QString& scriptName, const QString& explicitPath) {
    VrecordConfig cfg;
    cfg.configPath = explicitPath.isEmpty()
        ? QDir::home().filePath(QStringLiteral(".%1.conf").arg(scriptName))
        : explicitPath;

    QFile file(cfg.configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        cfg.values.insert(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0"));
        cfg.values.insert(QStringLiteral("VIDEO_INPUT_CHOICE"), QStringLiteral("SDI"));
        cfg.values.insert(QStringLiteral("AUDIO_INPUT_CHOICE"), QStringLiteral("SDI Embedded Audio"));
        cfg.values.insert(QStringLiteral("AUDIO_MAPPING_CHOICE"), QStringLiteral("1 Stereo Track (From Channels 1 & 2)"));
        cfg.values.insert(QStringLiteral("EMBED_LOGS_CHOICE"), QStringLiteral("Yes"));
        cfg.values.insert(QStringLiteral("PLAYBACKVIEW_CHOICE"), QStringLiteral("Visual"));
        cfg.values.insert(QStringLiteral("PLAYBACKVIEW_CHOICE_PASS"), QStringLiteral("Visual"));
        cfg.values.insert(QStringLiteral("MONITOR_AUDIO_CHOICE"), QStringLiteral("Yes"));
        cfg.values.insert(QStringLiteral("CONTAINER_CHOICE"), QStringLiteral("Matroska"));
        cfg.values.insert(QStringLiteral("VIDEO_CODEC_CHOICE"), QStringLiteral("FFV1 version 3"));
        cfg.values.insert(QStringLiteral("AUDIO_CODEC_CHOICE"), QStringLiteral("24-bit PCM"));
        cfg.values.insert(QStringLiteral("STANDARD_CHOICE"), QStringLiteral("NTSC"));
        cfg.values.insert(QStringLiteral("VIDEO_BIT_DEPTH_CHOICE"), QStringLiteral("10 bit"));
        cfg.values.insert(QStringLiteral("ASPECT_RATIO_CHOICE"), QStringLiteral("4/3"));
        cfg.values.insert(QStringLiteral("THEME_CHOICE"), QStringLiteral("Dark"));
        cfg.values.insert(QStringLiteral("FILEPLAY_AUTO_CHOICE"), QStringLiteral("true"));
        cfg.values.insert(QStringLiteral("QT6_CONFIG_REV"), QStringLiteral("2"));
        cfg.values.insert(QStringLiteral("PREVIEW_RELAY_CHOICE"), QStringLiteral("false"));
        return cfg;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = VrecordOptions::migrate(key, unquote(line.mid(eq + 1)));
        cfg.values.insert(key, value);
    }
    // the shell stores the recording time as DURATION="H:M:S"; split it for
    // the three entry boxes when this config came from the GTK frontend
    const QString duration = cfg.values.value(QStringLiteral("DURATION"));
    if (!duration.isEmpty() && !cfg.values.contains(QStringLiteral("DURATION_HOURS"))) {
        const QStringList parts = duration.split(QLatin1Char(':'));
        if (parts.size() == 3) {
            cfg.values.insert(QStringLiteral("DURATION_HOURS"), parts.at(0));
            cfg.values.insert(QStringLiteral("DURATION_MINUTES"), parts.at(1));
            cfg.values.insert(QStringLiteral("DURATION_SECONDS"), parts.at(2));
        }
    }
    // rev 2: file playout defaults to the file's native mode. Earlier Qt
    // builds saved FILEPLAY_AUTO_CHOICE="false" without the user choosing it,
    // so flip it once; a later explicit untick is saved with the marker and kept
    if (cfg.values.value(QStringLiteral("QT6_CONFIG_REV")).toInt() < 2) {
        cfg.values.insert(QStringLiteral("FILEPLAY_AUTO_CHOICE"), QStringLiteral("true"));
        cfg.values.insert(QStringLiteral("QT6_CONFIG_REV"), QStringLiteral("2"));
    }
    return cfg;
}

void VrecordConfig::prepareForBackend() {
    // DURATION is what the shell reads (the boxes are a frontend split)
    const QString h = value(QStringLiteral("DURATION_HOURS")).trimmed();
    const QString m = value(QStringLiteral("DURATION_MINUTES")).trimmed();
    const QString s = value(QStringLiteral("DURATION_SECONDS")).trimmed();
    if (h.isEmpty() && m.isEmpty() && s.isEmpty()) {
        setValue(QStringLiteral("DURATION"), QString());
    } else {
        setValue(QStringLiteral("DURATION"), QStringLiteral("%1:%2:%3").arg(h.isEmpty() ? QStringLiteral("0") : h,
                                                                           m.isEmpty() ? QStringLiteral("0") : m,
                                                                           s.isEmpty() ? QStringLiteral("0") : s));
    }
    // without HD_CHOICE=true the shell only accepts NTSC/PAL and rejects any
    // HD/UHD standard in _review_option
    if (VrecordOptions::isHdStandard(value(QStringLiteral("STANDARD_CHOICE")))) {
        setValue(QStringLiteral("HD_CHOICE"), QStringLiteral("true"));
    }
}

bool VrecordConfig::save() const {
    // atomic write: a failed save (disk full, lock) must never destroy the
    // existing config - write to a temp file, keep the old file as .bak,
    // then rename over
    QDir().mkpath(QFileInfo(configPath).absolutePath());
    const QString tempPath = configPath + QStringLiteral(".tmp");
    const QString backupPath = configPath + QStringLiteral(".bak");
    {
        QFile temp(tempPath);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        QTextStream out(&temp);
        out << "# vrecord Qt6 frontend configuration\n";
        for (auto it = values.keyValueBegin(); it != values.keyValueEnd(); ++it) {
            out << it->first << '=' << bashQuote(it->second) << '\n';
        }
        temp.flush();
        if (temp.error() != QFile::NoError) {
            temp.close();
            QFile::remove(tempPath);
            return false;
        }
    }
    QFile::remove(backupPath);
    if (QFile::exists(configPath)) {
        if (!QFile::rename(configPath, backupPath)) {
            QFile::remove(tempPath);
            return false;
        }
    }
    if (!QFile::rename(tempPath, configPath)) {
        // roll the backup back so the old settings survive
        QFile::rename(backupPath, configPath);
        return false;
    }
    return true;
}

QString VrecordConfig::value(const QString& key, const QString& fallback) const {
    return values.value(key, fallback);
}

void VrecordConfig::setValue(const QString& key, const QString& value) {
    values.insert(key, value);
}

QString VrecordConfig::outputExtension() const {
    const QString deviceInput = value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0"));
    if (deviceInput == QStringLiteral("1")) {
        return QStringLiteral("dv");
    }
    if (deviceInput == QStringLiteral("2")) {
        return value(QStringLiteral("AUDIO_MODE_CODEC_CHOICE")) == QStringLiteral("24-bit FLAC")
            ? QStringLiteral("flac")
            : QStringLiteral("wav");
    }
    const QString container = value(QStringLiteral("CONTAINER_CHOICE"), QStringLiteral("Matroska"));
    if (container == QStringLiteral("QuickTime")) return QStringLiteral("mov");
    if (container == QStringLiteral("AVI")) return QStringLiteral("avi");
    if (container == QStringLiteral("MXF")) return QStringLiteral("mxf");
    if (container == QStringLiteral("MP4")) return QStringLiteral("mp4");
    return QStringLiteral("mkv");
}

QString VrecordConfig::outputFilename() const {
    QString dir = value(QStringLiteral("DIR"));
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QString base = value(QStringLiteral("PREFIX")) + value(QStringLiteral("ID"), QStringLiteral("output"));
    if (value(QStringLiteral("YES_SUFFIX")) == QStringLiteral("true") && !value(QStringLiteral("USER_SUFFIX")).isEmpty()) {
        base += QStringLiteral("_") + value(QStringLiteral("USER_SUFFIX"));
    }
    return QDir::toNativeSeparators(QDir(dir).filePath(base + QStringLiteral(".") + outputExtension()));
}

QStringList VrecordConfig::validate() const {
    QStringList issues;
    const QString dir = value(QStringLiteral("DIR"));
    const QString logDir = value(QStringLiteral("LOGDIR"));
    if (dir.isEmpty()) {
        issues << QStringLiteral("WARNING: Recording directory is not set; current directory will be used.");
    } else if (!QFileInfo::exists(dir) || !QFileInfo(dir).isDir()) {
        issues << QStringLiteral("ERROR: Recording directory does not exist.");
    }
    if (!logDir.isEmpty() && (!QFileInfo::exists(logDir) || !QFileInfo(logDir).isDir())) {
        issues << QStringLiteral("ERROR: Auxiliary directory does not exist.");
    }
    if (value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0")) == QStringLiteral("0")
        && value(QStringLiteral("DECKLINK_INPUT_CHOICE")).isEmpty()) {
        issues << QStringLiteral("ERROR: Select a DeckLink device.");
    }
    if (value(QStringLiteral("ID")).isEmpty()) {
        issues << QStringLiteral("WARNING: Enter a Name of Recording to enable Record.");
    }
    return issues;
}

QString VrecordConfig::summary() const {
    const QString deviceInput = value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0"));
    QStringList lines;
    if (deviceInput == QStringLiteral("0")) {
        lines << QStringLiteral("Device Input: DeckLink");
        lines << QStringLiteral("Card: %1").arg(value(QStringLiteral("DECKLINK_INPUT_CHOICE")));
        lines << QStringLiteral("Video Input: %1").arg(value(QStringLiteral("VIDEO_INPUT_CHOICE")));
        lines << QStringLiteral("Audio Input: %1").arg(value(QStringLiteral("AUDIO_INPUT_CHOICE")));
        lines << QStringLiteral("Container: %1").arg(value(QStringLiteral("CONTAINER_CHOICE")));
        lines << QStringLiteral("Video Codec: %1").arg(value(QStringLiteral("VIDEO_CODEC_CHOICE")));
        lines << QStringLiteral("Audio Codec: %1").arg(value(QStringLiteral("AUDIO_CODEC_CHOICE")));
        lines << QStringLiteral("Standard: %1").arg(value(QStringLiteral("STANDARD_CHOICE")));
    } else if (deviceInput == QStringLiteral("1")) {
        lines << QStringLiteral("Device Input: DV");
        lines << QStringLiteral("Device: %1").arg(value(QStringLiteral("DVRESCUE_INPUT_CHOICE")));
    } else {
        lines << QStringLiteral("Device Input: Audio");
        lines << QStringLiteral("Device: %1").arg(value(QStringLiteral("AUDIO_DEV_CHOICE")));
    }
    lines << QStringLiteral("Output: %1").arg(outputFilename());
    return lines.join('\n');
}
