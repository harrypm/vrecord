#include "UpdateManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#ifndef VRECORD_VERSION
#define VRECORD_VERSION "0"
#endif
#ifndef VRECORD_UPDATE_REPO
#define VRECORD_UPDATE_REPO "harrypm/vrecord"
#endif

namespace {
const QString kSetupSuffix = QStringLiteral("_windows_x86_64_setup.exe");
const QString kPortableSuffix = QStringLiteral("_windows_x86_64_portable.zip");
const QString kSumsName = QStringLiteral("SHA256SUMS.txt");

// PowerShell single-quoted literal
QString psQuote(const QString& text) {
    return QLatin1Char('\'') + QString(text).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
}

QString sha256OfFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(4 * 1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

UpdateManager::UpdateManager(const QString& packageRoot, QObject* parent)
    : QObject(parent)
    , m_packageRoot(packageRoot) {
    // the portable zip carries portable.txt; the installer removes it
    m_portable = QFileInfo::exists(QDir(packageRoot).filePath(QStringLiteral("portable.txt")));
    if (m_portable) {
        // portable: settings travel with the package
        m_settings = std::make_unique<QSettings>(QDir(packageRoot).filePath(QStringLiteral("vrecord-qt6-update.ini")), QSettings::IniFormat);
    } else {
        m_settings = std::make_unique<QSettings>(QStringLiteral("vrecord"), QStringLiteral("vrecord-qt6"));
    }
    m_network = new QNetworkAccessManager(this);
}

UpdateManager::~UpdateManager() {
    cancel();
}

QString UpdateManager::currentVersion() {
    return QStringLiteral(VRECORD_VERSION);
}

QString UpdateManager::repository() {
    return QStringLiteral(VRECORD_UPDATE_REPO);
}

int UpdateManager::compareVersions(const QString& a, const QString& b) {
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    const auto parts = [](const QString& text) {
        QList<qlonglong> values;
        auto it = digits.globalMatch(text);
        while (it.hasNext()) {
            values << it.next().captured(0).toLongLong();
        }
        return values;
    };
    const QList<qlonglong> left = parts(a);
    const QList<qlonglong> right = parts(b);
    const qsizetype count = qMax(left.size(), right.size());
    for (qsizetype i = 0; i < count; ++i) {
        const qlonglong l = i < left.size() ? left.at(i) : 0;
        const qlonglong r = i < right.size() ? right.at(i) : 0;
        if (l != r) {
            return l < r ? -1 : 1;
        }
    }
    return 0;
}

QString UpdateManager::expectedSha256(const QByteArray& sums, const QString& assetName) {
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+)$"));
    for (const QByteArray& raw : sums.split('\n')) {
        const QString text = QString::fromUtf8(raw).trimmed();
        const auto match = line.match(text);
        if (match.hasMatch() && match.captured(2).trimmed() == assetName) {
            return match.captured(1).toLower();
        }
    }
    return QString();
}

bool UpdateManager::parseRelease(const QByteArray& json, bool portable, UpdateInfo* info, QString* message) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *message = QStringLiteral("The release information could not be read (%1).").arg(parseError.errorString());
        return false;
    }
    const QJsonObject release = document.object();
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) {
        *message = QStringLiteral("The latest release is not published yet.");
        return false;
    }
    UpdateInfo result;
    result.tag = release.value(QStringLiteral("tag_name")).toString();
    result.title = release.value(QStringLiteral("name")).toString();
    result.notes = release.value(QStringLiteral("body")).toString();
    result.pageUrl = QUrl(release.value(QStringLiteral("html_url")).toString());
    if (result.tag.isEmpty()) {
        *message = QStringLiteral("The release information has no tag.");
        return false;
    }
    const QString suffix = portable ? kPortableSuffix : kSetupSuffix;
    for (const QJsonValue& value : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (name.endsWith(suffix)) {
            result.assetName = name;
            result.assetUrl = url;
            result.assetSize = asset.value(QStringLiteral("size")).toInteger();
        } else if (name == kSumsName) {
            result.sumsUrl = url;
        }
    }
    if (!result.assetUrl.isValid() || result.assetName.isEmpty()) {
        *message = QStringLiteral("Release %1 has no %2 Windows package (*%3).")
                       .arg(result.tag, portable ? QStringLiteral("portable") : QStringLiteral("installer"), suffix);
        return false;
    }
    if (!result.sumsUrl.isValid()) {
        *message = QStringLiteral("Release %1 has no %2 - an unverified update is never installed.").arg(result.tag, kSumsName);
        return false;
    }
    *info = result;
    return true;
}

UpdateManager::Mode UpdateManager::mode() const {
    const int value = m_settings->value(QStringLiteral("update/mode"), int(Mode::Notify)).toInt();
    return value == int(Mode::Off) ? Mode::Off : value == int(Mode::Auto) ? Mode::Auto : Mode::Notify;
}

void UpdateManager::setMode(Mode mode) {
    m_settings->setValue(QStringLiteral("update/mode"), int(mode));
}

bool UpdateManager::checkDue() const {
    const QDateTime last = m_settings->value(QStringLiteral("update/lastCheck")).toDateTime();
    return !last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) > 20 * 3600;
}

QString UpdateManager::skippedTag() const {
    return m_settings->value(QStringLiteral("update/skipped")).toString();
}

void UpdateManager::setSkippedTag(const QString& tag) {
    m_settings->setValue(QStringLiteral("update/skipped"), tag);
}

QUrl UpdateManager::apiUrl() const {
    const QString overrideUrl = qEnvironmentVariable("VRECORD_UPDATE_API");
    if (!overrideUrl.isEmpty()) {
        return QUrl(overrideUrl);
    }
    const QString repo = m_settings->value(QStringLiteral("update/repo"), repository()).toString();
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo));
}

QString UpdateManager::workDir(const QString& tag) const {
    QString safeTag = tag;
    safeTag.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z._-]")), QStringLiteral("_"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("vrecord-update/%1").arg(safeTag));
}

QString UpdateManager::applyLogPath() const {
    return QDir(workDir(m_info.tag)).filePath(QStringLiteral("apply-update.log"));
}

QNetworkReply* UpdateManager::get(const QUrl& url, bool api) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("vrecord-updater/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // inactivity timeout: a stalled connection fails instead of hanging
    request.setTransferTimeout(api ? 30000 : 120000);
    if (api) {
        request.setRawHeader("Accept", "application/vnd.github+json");
    }
    return m_network->get(request);
}

void UpdateManager::check(bool userInitiated) {
    if (isBusy()) {
        emit checkFinished(false, false, UpdateInfo(), QStringLiteral("An update check or download is already running."));
        return;
    }
    m_settings->setValue(QStringLiteral("update/lastCheck"), QDateTime::currentDateTimeUtc());
    m_reply = get(apiUrl(), true);
    connect(m_reply, &QNetworkReply::finished, this, [this, userInitiated] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = status == 404
                ? QStringLiteral("No published release was found for %1.").arg(repository())
                : QStringLiteral("Could not check for updates: %1").arg(reply->errorString());
            emit checkFinished(false, false, UpdateInfo(), message);
            return;
        }
        UpdateInfo info;
        QString message;
        if (!parseRelease(reply->readAll(), m_portable, &info, &message)) {
            emit checkFinished(false, false, UpdateInfo(), message);
            return;
        }
        if (compareVersions(info.tag, currentVersion()) <= 0) {
            emit checkFinished(true, false, info, QStringLiteral("vrecord %1 is up to date (latest release: %2).").arg(currentVersion(), info.tag));
            return;
        }
        if (!userInitiated && info.tag == skippedTag()) {
            emit checkFinished(true, false, info, QStringLiteral("Update %1 is available (skipped).").arg(info.tag));
            return;
        }
        emit checkFinished(true, true, info, QStringLiteral("vrecord %1 is available (this copy: %2).").arg(info.tag, currentVersion()));
    });
}

void UpdateManager::download(const UpdateInfo& info) {
    if (isBusy()) {
        emit downloadFinished(false, QStringLiteral("An update check or download is already running."));
        return;
    }
    m_ready = false;
    m_info = info;
    m_assetPath.clear();
    m_stagingDir.clear();
    if (!QDir().mkpath(workDir(info.tag))) {
        finishDownload(false, QStringLiteral("Could not create %1.").arg(QDir::toNativeSeparators(workDir(info.tag))));
        return;
    }
    fetchSums();
}

void UpdateManager::fetchSums() {
    m_reply = get(m_info.sumsUrl, false);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finishDownload(false, QStringLiteral("Could not download %1: %2").arg(kSumsName, reply->errorString()));
            return;
        }
        const QString expected = expectedSha256(reply->readAll(), m_info.assetName);
        if (expected.isEmpty()) {
            finishDownload(false, QStringLiteral("%1 has no checksum for %2 - not installing an unverified update.").arg(kSumsName, m_info.assetName));
            return;
        }
        // already downloaded (an earlier session): verify instead of fetching again
        const QString finalPath = QDir(workDir(m_info.tag)).filePath(m_info.assetName);
        if (QFileInfo::exists(finalPath) && sha256OfFile(finalPath) == expected) {
            m_assetPath = finalPath;
            verifyAsset(expected);
            return;
        }
        fetchAsset(expected);
    });
}

void UpdateManager::fetchAsset(const QString& expectedSha) {
    const QString partPath = QDir(workDir(m_info.tag)).filePath(m_info.assetName + QStringLiteral(".part"));
    m_assetFile = new QFile(partPath, this);
    if (!m_assetFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = QStringLiteral("Could not write %1: %2").arg(QDir::toNativeSeparators(partPath), m_assetFile->errorString());
        delete m_assetFile;
        m_assetFile = nullptr;
        finishDownload(false, message);
        return;
    }
    m_reply = get(m_info.assetUrl, false);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply && m_assetFile) {
            m_assetFile->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit downloadProgress(received, total > 0 ? total : m_info.assetSize);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, expectedSha, partPath] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (m_assetFile) {
            m_assetFile->write(reply->readAll());
            m_assetFile->close();
            delete m_assetFile;
            m_assetFile = nullptr;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(partPath);
            finishDownload(false, QStringLiteral("Download failed: %1").arg(reply->errorString()));
            return;
        }
        const QString finalPath = QDir(workDir(m_info.tag)).filePath(m_info.assetName);
        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath)) {
            finishDownload(false, QStringLiteral("Could not save %1.").arg(QDir::toNativeSeparators(finalPath)));
            return;
        }
        m_assetPath = finalPath;
        verifyAsset(expectedSha);
    });
}

void UpdateManager::verifyAsset(const QString& expectedSha) {
    const QString actual = sha256OfFile(m_assetPath);
    if (actual != expectedSha) {
        QFile::remove(m_assetPath);
        finishDownload(false, QStringLiteral("Checksum mismatch for %1 (expected %2, got %3) - the download was deleted.")
                                  .arg(m_info.assetName, expectedSha.left(16), actual.left(16)));
        return;
    }
    if (m_portable) {
        stagePortable();
    } else {
        finishDownload(true, QStringLiteral("%1 downloaded and verified.").arg(m_info.assetName));
    }
}

void UpdateManager::stagePortable() {
    // Windows 10+ ships bsdtar, which reads zip archives
    m_stagingDir = QDir(workDir(m_info.tag)).filePath(QStringLiteral("package"));
    QDir(m_stagingDir).removeRecursively();
    QDir().mkpath(m_stagingDir);
    const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))).filePath(QStringLiteral("System32/tar.exe"));
    m_stageProcess = new QProcess(this);
    m_stageProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_stageProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromLocal8Bit(m_stageProcess->readAll()).trimmed();
        m_stageProcess->deleteLater();
        m_stageProcess = nullptr;
        const bool complete = QFileInfo::exists(QDir(m_stagingDir).filePath(QStringLiteral("VrecordQt6.exe")))
            && QFileInfo::exists(QDir(m_stagingDir).filePath(QStringLiteral("vrecord")));
        if (status != QProcess::NormalExit || exitCode != 0 || !complete) {
            finishDownload(false, QStringLiteral("Could not unpack %1 (tar exit %2) %3").arg(m_info.assetName).arg(exitCode).arg(output.left(300)));
            return;
        }
        finishDownload(true, QStringLiteral("%1 downloaded, verified and unpacked.").arg(m_info.assetName));
    });
    connect(m_stageProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_stageProcess) {
            m_stageProcess->deleteLater();
            m_stageProcess = nullptr;
            finishDownload(false, QStringLiteral("Could not start tar.exe to unpack the update."));
        }
    });
    m_stageProcess->start(tar, {QStringLiteral("-xf"), QDir::toNativeSeparators(m_assetPath),
                                QStringLiteral("-C"), QDir::toNativeSeparators(m_stagingDir)});
}

void UpdateManager::finishDownload(bool ok, const QString& message) {
    m_ready = ok;
    emit downloadFinished(ok, message);
}

void UpdateManager::cancel() {
    if (m_reply) {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_assetFile) {
        m_assetFile->close();
        m_assetFile->remove();
        delete m_assetFile;
        m_assetFile = nullptr;
    }
    if (m_stageProcess) {
        m_stageProcess->disconnect(this);
        m_stageProcess->kill();
        m_stageProcess->waitForFinished(2000);
        delete m_stageProcess;
        m_stageProcess = nullptr;
    }
}

bool UpdateManager::launchInstall(qint64 processId, bool relaunch, QString* error) {
    if (!m_ready || m_assetPath.isEmpty()) {
        *error = QStringLiteral("No verified update is ready.");
        return false;
    }
    const QString dir = workDir(m_info.tag);
    // a fresh log: an old run's "done" must not pass for this one
    QFile::remove(applyLogPath());
    const QString root = QDir::toNativeSeparators(QDir(m_packageRoot).absolutePath());
    const QString exe = root + QStringLiteral("\\VrecordQt6.exe");
    QStringList script;
    script << QStringLiteral("$ErrorActionPreference = 'Continue'")
           << QStringLiteral("$log = %1").arg(psQuote(QDir::toNativeSeparators(applyLogPath())))
           << QStringLiteral("function Log($m) { Add-Content -LiteralPath $log -Value ((Get-Date -Format s) + ' ' + $m) }")
           << QStringLiteral("Log 'vrecord update %1 (%2): waiting for process %3 to exit'").arg(m_info.tag, installKind()).arg(processId)
           << QStringLiteral("Wait-Process -Id %1 -Timeout 180 -ErrorAction SilentlyContinue").arg(processId)
           << QStringLiteral("Start-Sleep -Milliseconds 800");
    if (m_portable) {
        // copy the unpacked package over this one; files the zip does not
        // carry (user settings, logs) stay. Retries cover late file releases.
        // robocopy by full path through Start-Process: the environment vrecord
        // was started from may lack System32 on PATH or PATHEXT (MSYS), and
        // then `& robocopy` is "a document"; a missing exit code is a failure
        // (in PowerShell `$null -lt 8` is true)
        const QString robocopyArgs = QStringLiteral("\"%1\" \"%2\" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP")
                                         .arg(QDir::toNativeSeparators(m_stagingDir), root);
        script << QStringLiteral("$robocopy = Join-Path $env:SystemRoot 'System32\\robocopy.exe'")
               << QStringLiteral("$ok = $false")
               << QStringLiteral("for ($i = 0; $i -lt 10 -and -not $ok; $i++) {")
               << QStringLiteral("  $code = $null")
               << QStringLiteral("  try { $code = (Start-Process -FilePath $robocopy -ArgumentList %1 -Wait -PassThru -WindowStyle Hidden).ExitCode } catch { Log ('robocopy failed to start: ' + $_) }")
                      .arg(psQuote(robocopyArgs))
               << QStringLiteral("  Log ('robocopy exit ' + $code)")
               << QStringLiteral("  if ($null -ne $code -and $code -lt 8) { $ok = $true } else { Start-Sleep -Seconds 2 }")
               << QStringLiteral("}")
               << QStringLiteral("Log ('portable files copied: ' + $ok)")
               << QStringLiteral("if ($ok) { Remove-Item -LiteralPath %1 -Recurse -Force -ErrorAction SilentlyContinue }").arg(psQuote(QDir::toNativeSeparators(m_stagingDir)));
    } else {
        // NSIS: /D= must be last and unquoted - one verbatim argument string
        script << QStringLiteral("$p = Start-Process -FilePath %1 -ArgumentList %2 -Wait -PassThru")
                      .arg(psQuote(QDir::toNativeSeparators(m_assetPath)), psQuote(QStringLiteral("/S /D=") + root))
               << QStringLiteral("Log ('setup exit code ' + $p.ExitCode)");
    }
    if (relaunch) {
        script << QStringLiteral("if (Test-Path -LiteralPath %1) { Start-Process -FilePath %1; Log 'relaunched' }").arg(psQuote(exe));
    }
    script << QStringLiteral("Log 'done'");

    const QString scriptPath = QDir(dir).filePath(QStringLiteral("apply-update.ps1"));
    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("Could not write %1.").arg(QDir::toNativeSeparators(scriptPath));
        return false;
    }
    // UTF-8 with BOM: Windows PowerShell 5.1 reads non-ASCII paths correctly
    file.write("\xEF\xBB\xBF");
    file.write(script.join(QStringLiteral("\r\n")).toUtf8());
    file.write("\r\n");
    file.close();
    // full path: the inherited PATH may lack System32
    const QString powershell = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")))
                                   .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    const bool started = QProcess::startDetached(
        QDir::toNativeSeparators(powershell),
        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
         QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)});
    if (!started) {
        *error = QStringLiteral("Could not start PowerShell to apply the update.");
    }
    return started;
}
