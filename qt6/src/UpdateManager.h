#pragma once

// Update checks and installs for the Windows builds.
//
// Source: the latest PUBLISHED GitHub release of VRECORD_UPDATE_REPO (drafts
// and pre-releases are never offered). The asset that matches this copy is
// used: *_windows_x86_64_setup.exe for an installed copy,
// *_windows_x86_64_portable.zip for a portable one (portable.txt next to the
// exe). Every download is checked against the release's SHA256SUMS.txt and
// refused when the sum is missing or differs.
//
// Install (after vrecord has exited, by a helper script that waits for it):
//   installed: the setup runs silently into the same folder (/S /D=...)
//   portable:  the verified zip is extracted to a staging folder and copied
//              over the package (user files that are not in the zip stay)
//
// VRECORD_UPDATE_API overrides the release URL (tests: a file:// JSON).

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QProcess;

struct UpdateInfo {
    QString tag;        // release tag, e.g. v2026.11.13
    QString title;      // release name
    QString notes;      // release body (markdown)
    QUrl pageUrl;       // release page
    QString assetName;  // the package for this copy
    QUrl assetUrl;
    qint64 assetSize = 0;
    QUrl sumsUrl;       // SHA256SUMS.txt
    bool isValid() const { return !tag.isEmpty() && assetUrl.isValid(); }
};

class UpdateManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { Off = 0, Notify = 1, Auto = 2 };

    explicit UpdateManager(const QString& packageRoot, QObject* parent = nullptr);
    ~UpdateManager() override;

    static QString currentVersion();
    static QString repository();
    // compares the digit groups (2026-03-03 == v2026.03.03): <0, 0, >0
    static int compareVersions(const QString& a, const QString& b);
    // "<hash>  <name>" / "<hash> *<name>" lines -> hash for name ("" if absent)
    static QString expectedSha256(const QByteArray& sums, const QString& assetName);
    // picks this copy's asset out of a GitHub release JSON; false + message if unusable
    static bool parseRelease(const QByteArray& json, bool portable, UpdateInfo* info, QString* message);

    bool isPortable() const { return m_portable; }
    QString installKind() const { return m_portable ? QStringLiteral("portable") : QStringLiteral("installed"); }
    QString packageRoot() const { return m_packageRoot; }

    Mode mode() const;
    void setMode(Mode mode);
    bool checkDue() const;           // last automatic check over 20 h ago
    QString skippedTag() const;
    void setSkippedTag(const QString& tag);

    bool isBusy() const { return m_reply != nullptr || m_stageProcess != nullptr; }
    bool isReady() const { return m_ready; }
    UpdateInfo pendingInfo() const { return m_info; }
    QString applyLogPath() const;   // the install helper's log

    void check(bool userInitiated);
    void download(const UpdateInfo& info);
    void cancel();
    // starts the helper that waits for processId to exit, then installs (and
    // relaunches vrecord if relaunch); the caller then quits
    bool launchInstall(qint64 processId, bool relaunch, QString* error);

signals:
    void checkFinished(bool ok, bool updateAvailable, const UpdateInfo& info, const QString& message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(bool ok, const QString& message);

private:
    QUrl apiUrl() const;
    QString workDir(const QString& tag) const;
    void fetchSums();
    void fetchAsset(const QString& expectedSha);
    void verifyAsset(const QString& expectedSha);
    void stagePortable();
    void finishDownload(bool ok, const QString& message);
    QNetworkReply* get(const QUrl& url, bool api);

    QString m_packageRoot;
    bool m_portable = false;
    std::unique_ptr<QSettings> m_settings;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_assetFile = nullptr;
    QProcess* m_stageProcess = nullptr;
    UpdateInfo m_info;
    QString m_assetPath;     // verified download
    QString m_stagingDir;    // portable: extracted package
    bool m_ready = false;
};
