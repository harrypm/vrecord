#include "MainWindow.h"
#include <cstdio>

#include "DeckLinkDiscovery.h"
#include "LevelMeter.h"
#include "VideoView.h"
#include "VrecordOptions.h"
#include "Wss.h"
#include "DeckControl.h"

#include <QApplication>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSplitter>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QCoreApplication>
#include <QDateTime>

namespace {
// self-test trace: appended to VrecordQt6-selftest-trace.log (a file: survives
// a hang and a GUI-subsystem exe's lost stderr); CI prints it on failure
void selfTestTrace(const QString& step) {
    if (!BackendController::testMode()) {
        return;
    }
    QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("VrecordQt6-selftest-trace.log")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(QStringLiteral("%1 trace %2\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate), step).toUtf8());
    }
}

QPalette stockDarkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(53, 53, 53));
    palette.setColor(QPalette::WindowText, QColor(255, 255, 255));
    palette.setColor(QPalette::Base, QColor(25, 25, 25));
    palette.setColor(QPalette::AlternateBase, QColor(64, 64, 64));
    palette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
    palette.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(245, 247, 250));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(255, 85, 85));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(208, 212, 217));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(160, 160, 160));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(170, 175, 181));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(160, 160, 160));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}

QPalette stockLightPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(239, 239, 239));
    palette.setColor(QPalette::WindowText, QColor(0, 0, 0));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    palette.setColor(QPalette::Text, QColor(22, 24, 28));
    palette.setColor(QPalette::Button, QColor(239, 239, 239));
    palette.setColor(QPalette::ButtonText, QColor(0, 0, 0));
    palette.setColor(QPalette::BrightText, QColor(255, 85, 85));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(95, 99, 104));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(107, 114, 128));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(200, 200, 200));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0, 0, 0));
    return palette;
}

bool systemPrefersDarkTheme() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
#else
    return true;
#endif
}
}

MainWindow::MainWindow(const QString& packageRoot, QWidget* parent)
    : QMainWindow(parent)
    , m_config(VrecordConfig::load(QStringLiteral("vrecord")))
    , m_backend(packageRoot, this) {
    m_backend.setManagedOutputs(true);
    selfTestTrace(QStringLiteral("ctor: buildUi"));
    buildUi();
    selfTestTrace(QStringLiteral("ctor: populateFromConfig"));
    populateFromConfig();
    applySelectedTheme();
    selfTestTrace(QStringLiteral("ctor: refreshDeckLinkDevices"));
    refreshDeckLinkDevices();
    selfTestTrace(QStringLiteral("ctor: connect signals"));
    // (the self-test runs only with --self-test: it drives every action and
    // would stop the live preview and stomp the user's fields on each launch)
    updateDerivedState();

    // frames arrive as fast as the card delivers them; each one repaints its pane
    connect(&m_backend, &BackendController::inputFrame, this, [this](const QImage& frame) {
        m_inputView->setFrame(frame);
        // relay: the card outputs exactly the input frames, so the output pane
        // shows them too (playout / the test signal take the pane over)
        if (m_backend.isPreviewRelaying() && !m_backend.isFileOutputActive() && !m_backend.isManagedTestSignalRunning()) {
            if (!m_outputMirrorsInput) {
                m_outputMirrorsInput = true;
                m_outputView->setCaption(QStringLiteral("relay: live input on the output"));
            }
            m_outputView->setFrame(frame);
        } else if (m_outputMirrorsInput) {
            m_outputMirrorsInput = false;
            if (!m_backend.isFileOutputActive() && !m_backend.isManagedTestSignalRunning()) {
                m_outputView->setCaption(QString());
                m_outputView->clearFrame(QStringLiteral("No hardware output running"));
            }
        }
    });
    connect(&m_backend, &BackendController::outputFrame, m_outputView, &VideoView::setFrame);
    connect(&m_backend, &BackendController::inputStatus, this, [this](const QString& message) {
        m_inputView->setCaption(message.startsWith(QStringLiteral("streaming")) ? message.mid(11).chopped(1) : QString());
        if (!message.startsWith(QStringLiteral("streaming"))) {
            if (!message.startsWith(QStringLiteral("starting")) && !message.contains(QStringLiteral("failed"))) {
                m_inputView->clearFrame(QStringLiteral("Input preview %1").arg(message));
                updatePreviewButton();
            } else {
                m_inputView->setMessage(QStringLiteral("Input preview %1").arg(message));
            }
        }
        appendActivity(QStringLiteral("input: %1").arg(message));
    });
    connect(&m_backend, &BackendController::outputStatus, this, [this](const QString& message) {
        if (message.startsWith(QStringLiteral("playing")) || message.startsWith(QStringLiteral("holding")) || message.startsWith(QStringLiteral("test signal"))) {
            m_outputView->setCaption(message);
        } else if (message.startsWith(QStringLiteral("stopped")) || message.startsWith(QStringLiteral("could not"))) {
            m_outputView->clearFrame(QStringLiteral("Output %1").arg(message));
        }
        appendActivity(QStringLiteral("output: %1").arg(message));
    });
    connect(&m_backend, &BackendController::inputModeDetected, this, &MainWindow::followInputMode);
    connect(&m_backend, &BackendController::inputAspectDetected, this, &MainWindow::onInputAspectDetected);
    connect(&m_backend, &BackendController::testSignalResult, this, [this](bool ok, const QString& message) {
        if (!ok) {
            m_testSignalOutputCheck->blockSignals(true);
            m_testSignalOutputCheck->setChecked(false);
            m_testSignalOutputCheck->blockSignals(false);
            syncConfigFromUi();
            m_outputView->clearFrame(QStringLiteral("No hardware output running"));
            showError(message);
        } else {
            statusBar()->showMessage(QStringLiteral("Test signal output running"), 3000);
        }
        updatePreviewButton();
    });
    connect(&m_backend, &BackendController::inputAudioLevels, this, [this](const QVector<float>& peak, const QVector<float>& rms) {
        m_previewMeter->setLevels(peak, rms);
        // relay: the output carries the monitored pair of the input
        if (m_backend.isPreviewRelaying() && !m_backend.isFileOutputActive() && !m_backend.isManagedTestSignalRunning()) {
            const int pair = m_monitorPairCombo->currentIndex();
            m_outputMeter->setLevels({peak.value(pair * 2, -120.0f), peak.value(pair * 2 + 1, -120.0f)},
                                     {rms.value(pair * 2, -120.0f), rms.value(pair * 2 + 1, -120.0f)});
        }
    });
    connect(&m_backend, &BackendController::outputAudioLevels, m_outputMeter, &LevelMeter::setLevels);
    connect(&m_backend, &BackendController::shellStarted, this, [this](const QString& mode) {
        if (m_backend.isSessionMonitorActive()) {
            m_inputView->clearFrame(QStringLiteral("%1 - monitoring here").arg(mode));
            m_inputView->setCaption(mode == QStringLiteral("record") ? QStringLiteral("RECORDING - press Stop Recording to finish")
                                                                     : QStringLiteral("%1 (in-pane monitor)").arg(mode));
        } else {
            m_inputView->clearFrame(mode == QStringLiteral("record")
                                        ? QStringLiteral("Recording - monitor in the player window.\nClose that window (or press Stop Recording) to finish.")
                                        : QStringLiteral("%1 running in the player window").arg(mode));
        }
        m_recordButton->setText(mode == QStringLiteral("record") ? QStringLiteral("Stop Recording") : QStringLiteral("Stop %1").arg(mode));
        // the session took the card (releaseDeckLink stopped the generator and
        // any playout): reflect that instead of leaving a stale tick behind
        if (m_testSignalOutputCheck->isChecked()) {
            m_testSignalOutputCheck->blockSignals(true);
            m_testSignalOutputCheck->setChecked(false);
            m_testSignalOutputCheck->blockSignals(false);
            syncConfigFromUi();
        }
        m_outputView->clearFrame(QStringLiteral("Output released to the %1 session").arg(mode));
        updatePreviewButton();
        appendActivity(QStringLiteral("backend: %1 started").arg(mode));
    });
    connect(&m_backend, &BackendController::shellFinished, this, [this](const QString& mode, int exitCode, const QString& message) {
        m_recordButton->setText(QStringLiteral("Record"));
        appendActivity(QStringLiteral("backend: %1 finished (exit %2)").arg(mode).arg(exitCode));
        if (exitCode != 0) {
            showError(QStringLiteral("The %1 session ended with exit code %2.\n\n%3\n\nFull log: %4")
                          .arg(mode).arg(exitCode).arg(message, QDir::toNativeSeparators(m_backend.shellLogPath())));
        } else {
            statusBar()->showMessage(QStringLiteral("%1 finished").arg(mode), 5000);
        }
        // back to the live preview, as the window started
        startPreviewQuietly();
    });

    // legacy parity: the live input preview starts automatically with the
    // window (not in the self-test: it drives every action itself)
    if (!BackendController::testMode()) {
        startPreviewQuietly();
    }
    // indicator lights: checked in the background once the window is up
    QTimer::singleShot(300, this, &MainWindow::refreshComponents);
    // update check: a few seconds after start, at most once a day (never in
    // the self-test)
    if (!BackendController::testMode() && m_updates->mode() != UpdateManager::Mode::Off && m_updates->checkDue()) {
        QTimer::singleShot(5000, this, [this] { m_updates->check(false); });
    }
    selfTestTrace(QStringLiteral("ctor: done"));
}

// ---------------------------------------------------------------- updates

QWidget* MainWindow::buildUpdatesPage(QWidget* parent) {
    m_updates = new UpdateManager(m_backend.packageRoot(), this);
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout();
    auto* versionLabel = new QLabel(QStringLiteral("%1 (%2 copy, updates from github.com/%3)")
                                        .arg(UpdateManager::currentVersion(), m_updates->installKind(), UpdateManager::repository()),
                                    page);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("This version"), versionLabel);
    m_updateModeCombo = new QComboBox(page);
    m_updateModeCombo->addItem(QStringLiteral("Off"), int(UpdateManager::Mode::Off));
    m_updateModeCombo->addItem(QStringLiteral("Tell me when an update is available"), int(UpdateManager::Mode::Notify));
    m_updateModeCombo->addItem(QStringLiteral("Download automatically, install when vrecord closes"), int(UpdateManager::Mode::Auto));
    m_updateModeCombo->setCurrentIndex(m_updateModeCombo->findData(int(m_updates->mode())));
    form->addRow(QStringLiteral("Automatic updates"), m_updateModeCombo);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    m_updateCheckButton = new QPushButton(QStringLiteral("Check for updates"), page);
    m_updateInstallButton = new QPushButton(QStringLiteral("Install and restart"), page);
    m_updateInstallButton->setEnabled(false);
    m_updateInstallButton->setToolTip(QStringLiteral("Close vrecord, install the downloaded update and start it again"));
    buttons->addWidget(m_updateCheckButton);
    buttons->addWidget(m_updateInstallButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    m_updateProgress = new QProgressBar(page);
    m_updateProgress->setVisible(false);
    layout->addWidget(m_updateProgress);
    m_updateStatus = new QLabel(QStringLiteral("Not checked yet."), page);
    m_updateStatus->setWordWrap(true);
    m_updateStatus->setTextFormat(Qt::RichText);
    m_updateStatus->setOpenExternalLinks(true);
    layout->addWidget(m_updateStatus);
    layout->addStretch(1);

    connect(m_updateModeCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_updates->setMode(UpdateManager::Mode(m_updateModeCombo->currentData().toInt()));
    });
    connect(m_updateCheckButton, &QPushButton::clicked, this, [this] {
        if (m_updates->isReady()) {
            setUpdateStatus(QStringLiteral("Update %1 is downloaded and ready to install.").arg(m_updates->pendingInfo().tag));
            return;
        }
        m_updateCheckButton->setEnabled(false);
        setUpdateStatus(QStringLiteral("Checking ..."));
        m_updateCheckUserInitiated = true;
        m_updates->check(true);
    });
    connect(m_updateInstallButton, &QPushButton::clicked, this, &MainWindow::installUpdateNow);
    connect(m_updates, &UpdateManager::checkFinished, this, &MainWindow::onUpdateCheckFinished);
    connect(m_updates, &UpdateManager::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_updateProgress->setVisible(true);
        m_updateProgress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) {
            m_updateProgress->setValue(int(received * 1000 / total));
        }
        m_updateProgress->setFormat(QStringLiteral("%1 of %2 MB").arg(received / 1048576).arg(total / 1048576));
    });
    connect(m_updates, &UpdateManager::downloadFinished, this, &MainWindow::onUpdateDownloaded);
    return page;
}

void MainWindow::setUpdateStatus(const QString& text) {
    m_updateStatus->setText(text.toHtmlEscaped());
}

void MainWindow::onUpdateCheckFinished(bool ok, bool available, const UpdateInfo& info, const QString& message) {
    const bool userInitiated = m_updateCheckUserInitiated;
    m_updateCheckUserInitiated = false;
    m_updateCheckButton->setEnabled(true);
    if (!ok || !available) {
        setUpdateStatus(message);
        if (userInitiated && !ok) {
            appendActivity(QStringLiteral("update check: %1").arg(message));
        }
        return;
    }
    appendActivity(QStringLiteral("update: %1").arg(message));
    m_updateStatus->setText(QStringLiteral("%1 <a href=\"%2\">Release notes</a>")
                                .arg(message.toHtmlEscaped(), info.pageUrl.toString(QUrl::FullyEncoded).toHtmlEscaped()));
    if (m_updates->mode() == UpdateManager::Mode::Auto) {
        startUpdateDownload(info);
        return;
    }
    statusBar()->showMessage(QStringLiteral("vrecord %1 is available - Config > Updates").arg(info.tag), 15000);
    // never interrupt a recording with a prompt: the status line says it
    if (m_backend.isShellRunning()) {
        return;
    }
    auto* box = new QMessageBox(QMessageBox::Information, QStringLiteral("vrecord update"),
                                QStringLiteral("vrecord %1 is available (this copy: %2).\n\nDownload and install it?")
                                    .arg(info.tag, UpdateManager::currentVersion()),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    if (!info.notes.isEmpty()) {
        box->setDetailedText(info.notes.left(4000));
    }
    QPushButton* downloadButton = box->addButton(QStringLiteral("Download"), QMessageBox::AcceptRole);
    box->addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    QPushButton* skipButton = box->addButton(QStringLiteral("Skip this version"), QMessageBox::DestructiveRole);
    connect(box, &QMessageBox::finished, this, [this, box, downloadButton, skipButton, info] {
        if (box->clickedButton() == downloadButton) {
            startUpdateDownload(info);
        } else if (box->clickedButton() == skipButton) {
            m_updates->setSkippedTag(info.tag);
            setUpdateStatus(QStringLiteral("Update %1 skipped - Check for updates still offers it.").arg(info.tag));
        }
    });
    box->open();   // non-modal: the window keeps working
}

void MainWindow::startUpdateDownload(const UpdateInfo& info) {
    setUpdateStatus(QStringLiteral("Downloading %1 ...").arg(info.assetName));
    m_updateCheckButton->setEnabled(false);
    m_updateProgress->setVisible(true);
    m_updateProgress->setRange(0, 0);
    m_updates->download(info);
}

void MainWindow::onUpdateDownloaded(bool ok, const QString& message) {
    m_updateCheckButton->setEnabled(true);
    m_updateProgress->setVisible(false);
    appendActivity(QStringLiteral("update: %1").arg(message));
    if (!ok) {
        setUpdateStatus(message);
        return;
    }
    const QString tag = m_updates->pendingInfo().tag;
    m_updateInstallButton->setEnabled(true);
    // installs when the window closes; "Install and restart" does it now
    m_installUpdateOnExit = true;
    setUpdateStatus(QStringLiteral("Update %1 is ready. It installs when vrecord closes, or now with Install and restart.").arg(tag));
    statusBar()->showMessage(QStringLiteral("Update %1 ready - installs when vrecord closes").arg(tag), 15000);
}

void MainWindow::installUpdateNow() {
    if (!m_updates->isReady()) {
        return;
    }
    if (m_backend.isShellRunning()) {
        showError(QStringLiteral("A %1 session is running - stop it before installing the update.").arg(m_backend.shellMode()));
        return;
    }
    m_restartAfterUpdate = true;
    close();   // closeEvent cleans up and starts the installer
    m_restartAfterUpdate = false;   // still open: the close was cancelled
}

// ---------------------------------------------------------------- deck control

void MainWindow::setDeckControlEnabled(bool enabled) {
    m_deckControl->setEnabled(enabled);
    for (QPushButton* button : m_deckButtons) {
        button->setEnabled(enabled && m_deckControl->isAvailable());
    }
}

// the GTK "Scan timecode types": open the input once per timecode type and
// report what each carries (the card's input is exclusive, so the preview
// pauses meanwhile and resumes afterwards)
void MainWindow::startTimecodeScan() {
    if (m_timecodeScanProcess) {
        return;
    }
    if (m_backend.isShellRunning()) {
        showError(QStringLiteral("Stop the running session first - it is using the card."));
        return;
    }
    syncConfigFromUi();
    if (m_config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed().isEmpty()) {
        showError(QStringLiteral("Select a DeckLink device first."));
        return;
    }
    m_timecodeScanResumePreview = m_backend.isVideoPreviewActive();
    if (m_timecodeScanResumePreview) {
        m_backend.stopVideoPreview();
        m_inputView->clearFrame(QStringLiteral("Preview paused for the timecode scan"));
    }
    m_timecodeScanQueue = {QStringLiteral("all"), QStringLiteral("rp188vitc"), QStringLiteral("rp188vitc2"), QStringLiteral("rp188ltc"),
                           QStringLiteral("rp188any"), QStringLiteral("vitc"), QStringLiteral("vitc2"), QStringLiteral("serial")};
    m_timecodeScanText->setPlainText(QStringLiteral("Type          Timecode\n"));
    m_timecodeScanButton->setEnabled(false);
    m_timecodeScanButton->setText(QStringLiteral("Scanning ..."));
    runNextTimecodeScan();
}

void MainWindow::runNextTimecodeScan() {
    if (m_timecodeScanQueue.isEmpty()) {
        finishTimecodeScan();
        return;
    }
    const QString type = m_timecodeScanQueue.takeFirst();
    m_timecodeScanProcess = new QProcess(this);
    m_timecodeScanProcess->setProcessChannelMode(QProcess::MergedChannels);
    m_timecodeScanProcess->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000;   // CREATE_NO_WINDOW
    });
    auto* timeout = new QTimer(m_timecodeScanProcess);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, m_timecodeScanProcess, &QProcess::kill);
    connect(m_timecodeScanProcess, &QProcess::finished, this, [this, type](int, QProcess::ExitStatus) {
        const QString output = QString::fromLocal8Bit(m_timecodeScanProcess->readAll());
        m_timecodeScanProcess->deleteLater();
        m_timecodeScanProcess = nullptr;
        static const QRegularExpression tc(QStringLiteral("^\\s*timecode\\s*:\\s*(\\S+)"), QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = tc.match(output);
        m_timecodeScanText->appendPlainText(QStringLiteral("%1 %2").arg(type, -13).arg(match.hasMatch() ? match.captured(1) : QStringLiteral("none")));
        runNextTimecodeScan();
    });
    const QString ffmpeg = QDir(m_backend.packageRoot()).filePath(QStringLiteral("bin/ffmpeg-ma.exe"));
    m_timecodeScanProcess->start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("info"),
                                          QStringLiteral("-timecode_format"), type,
                                          QStringLiteral("-video_input"), VrecordOptions::ffmpegVideoInput(m_config.value(QStringLiteral("VIDEO_INPUT_CHOICE"))),
                                          QStringLiteral("-f"), QStringLiteral("decklink"),
                                          QStringLiteral("-i"), m_config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed()});
    timeout->start(8000);
}

void MainWindow::finishTimecodeScan() {
    m_timecodeScanButton->setEnabled(true);
    m_timecodeScanButton->setText(QStringLiteral("Scan timecode types"));
    appendActivity(QStringLiteral("timecode scan finished"));
    if (m_timecodeScanResumePreview) {
        startPreviewQuietly();
    }
}

// ---------------------------------------------------------------- components

void MainWindow::refreshComponents() {
    if (m_componentsChecking) {
        return;
    }
    m_componentsChecking = true;
    m_componentsRecheck->setEnabled(false);
    m_componentsSummary->setText(QStringLiteral("Checking ..."));
    // each tool is started once for its version: keep that off the GUI thread
    const QString root = m_backend.packageRoot();
    QThread* worker = QThread::create([this, root] {
        const QVector<ComponentStatus> components = checkComponents(root);
        QMetaObject::invokeMethod(this, [this, components] { showComponents(components); }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::showComponents(const QVector<ComponentStatus>& components) {
    m_componentsChecking = false;
    m_componentsRecheck->setEnabled(true);
    while (QLayoutItem* item = m_componentsGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int row = 0;
    int requiredMissing = 0;
    int optionalPresent = 0;
    int optionalTotal = 0;
    bool headerOptional = false;
    const auto heading = [&](const QString& text) {
        auto* label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), m_componentsPanel);
        m_componentsGrid->addWidget(label, row++, 0, 1, 3);
    };
    heading(QStringLiteral("Required"));
    for (const ComponentStatus& component : components) {
        if (!component.required && !headerOptional) {
            headerOptional = true;
            heading(QStringLiteral("Optional tools"));
        }
        // green = present, amber = present but missing a feature, red =
        // missing and required, grey = optional and not installed
        QString colour = QStringLiteral("#2ecc40");
        QString state = QStringLiteral("installed");
        if (!component.found) {
            colour = component.required ? QStringLiteral("#e74c3c") : QStringLiteral("#7f8c8d");
            state = QStringLiteral("not installed");
        } else if (component.degraded) {
            colour = QStringLiteral("#f39c12");
            state = QStringLiteral("limited");
        }
        if (component.required && !component.found) {
            ++requiredMissing;
        }
        if (!component.required) {
            ++optionalTotal;
            optionalPresent += component.found ? 1 : 0;
        }
        auto* light = new QLabel(m_componentsPanel);
        light->setFixedSize(12, 12);
        light->setStyleSheet(QStringLiteral("background:%1; border-radius:6px; border:1px solid #222;").arg(colour));
        auto* name = new QLabel(component.name, m_componentsPanel);
        auto* info = new QLabel(component.found ? (component.version.isEmpty() ? state : component.version) : component.detail, m_componentsPanel);
        info->setStyleSheet(QStringLiteral("color: palette(mid);"));
        QStringList tip{QStringLiteral("%1 - %2").arg(component.name, state), QStringLiteral("Used for: %1").arg(component.purpose)};
        if (!component.path.isEmpty()) {
            tip << QStringLiteral("Location: %1").arg(component.path);
        }
        if (!component.version.isEmpty()) {
            tip << QStringLiteral("Version: %1").arg(component.version);
        }
        if (!component.detail.isEmpty()) {
            tip << component.detail;
        }
        for (QLabel* label : {light, name, info}) {
            label->setToolTip(tip.join(QLatin1Char('\n')));
        }
        m_componentsGrid->addWidget(light, row, 0, Qt::AlignVCenter);
        m_componentsGrid->addWidget(name, row, 1);
        m_componentsGrid->addWidget(info, row, 2);
        ++row;
    }
    m_componentsGrid->setColumnStretch(2, 1);
    for (const ComponentStatus& component : components) {
        if (component.name == QStringLiteral("dvpackager") && m_dvPackagerBox) {
            m_dvPackagerBox->setEnabled(component.found);
            m_dvPackagerBox->setToolTip(component.found
                                            ? QStringLiteral("dvpackager splits / rewraps the DV capture afterwards (%1)").arg(component.path)
                                            : QStringLiteral("dvpackager is not installed - DV is saved as captured."));
        }
    }
    m_componentsSummary->setText(requiredMissing == 0
                                     ? QStringLiteral("All required parts present - %1 of %2 optional tools").arg(optionalPresent).arg(optionalTotal)
                                     : QStringLiteral("%1 required part(s) missing").arg(requiredMissing));
    // the FFmpeg Status box: what the backend's ffmpeg-ma can do
    QStringList status;
    for (const ComponentStatus& component : components) {
        if (component.name.startsWith(QStringLiteral("ffmpeg-ma")) || component.name.startsWith(QStringLiteral("ffplay-ma"))
            || component.name.startsWith(QStringLiteral("ffprobe-ma"))) {
            status << QStringLiteral("%1: %2").arg(component.name, component.found ? component.version : QStringLiteral("missing"));
            if (!component.detail.isEmpty()) {
                status << QStringLiteral("  %1").arg(component.detail);
            }
            if (!component.path.isEmpty()) {
                status << QStringLiteral("  %1").arg(component.path);
            }
        }
    }
    m_ffmpegStatusText->setPlainText(status.join(QLatin1Char('\n')));
}

bool MainWindow::selectTab(const QString& title) {
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabText(i).compare(title, Qt::CaseInsensitive) == 0) {
            m_tabs->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

void MainWindow::startPreviewQuietly() {
    syncConfigFromUi();
    QString previewError;
    if (!m_backend.startVideoPreview(m_config, &previewError)) {
        m_inputView->clearFrame(QStringLiteral("Live preview not running: %1").arg(previewError));
    }
    updatePreviewButton();
}

void MainWindow::updatePreviewButton() {
    const bool active = m_backend.isVideoPreviewActive();
    m_previewToggleButton->setText(active ? QStringLiteral("Stop Preview") : QStringLiteral("Start Preview"));
    m_previewToggleButton->setIcon(QApplication::style()->standardIcon(active ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
    m_previewToggleButton->setEnabled(!m_backend.isShellRunning());
}

void MainWindow::appendActivity(const QString& line) {
    if (m_activityText) {
        m_activityText->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), line));
    }
}
namespace {
// "PCM (uncompressed)" etc.; the item data keeps the shell's spelling
void labelAudioCodecItems(QComboBox* combo) {
    for (int i = 0; i < combo->count(); ++i) {
        const QString value = combo->itemText(i);
        combo->setItemData(i, value);
        combo->setItemText(i, VrecordOptions::audioCodecLabel(value));
    }
}

void setComboItemEnabled(QComboBox* combo, int index, bool enabled) {
    if (auto* model = qobject_cast<QStandardItemModel*>(combo->model())) {
        if (QStandardItem* item = model->item(index)) {
            item->setEnabled(enabled);
        }
    }
}
} // namespace

QComboBox* MainWindow::makeCombo(const QStringList& items) {
    auto* combo = new QComboBox(this);
    combo->addItems(items);
    return combo;
}

// show "MOV (QuickTime)" etc.; the item data keeps the shell's spelling
void MainWindow::labelContainerItems(QComboBox* combo) {
    for (int i = 0; i < combo->count(); ++i) {
        const QString value = combo->itemText(i);
        combo->setItemData(i, value);
        combo->setItemText(i, VrecordOptions::containerLabel(value));
    }
}

QString MainWindow::choiceValue(const QComboBox* combo) {
    const QVariant data = combo->currentData();
    return data.isValid() && !data.toString().isEmpty() ? data.toString() : combo->currentText();
}

void MainWindow::registerDropTarget(QWidget* widget, QLineEdit* target, bool wantsFolder) {
    widget->setAcceptDrops(true);
    widget->installEventFilter(this);
    m_dropTargets.insert(widget, {target, wantsFolder});
}

// the local path a drop would set, or empty when the drop does not fit
QString MainWindow::dropPathFor(QObject* watched, const QMimeData* mime) const {
    const auto it = m_dropTargets.constFind(watched);
    if (it == m_dropTargets.constEnd() || !mime || !mime->hasUrls() || mime->urls().isEmpty()) {
        return {};
    }
    const QString local = mime->urls().constFirst().toLocalFile();
    if (local.isEmpty()) {
        return {};
    }
    const QFileInfo info(local);
    if (!it->target) {   // the window at large: any existing folder or file
        return info.exists() ? info.absoluteFilePath() : QString();
    }
    if (it->wantsFolder) {
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
    return info.isFile() ? info.absoluteFilePath() : QString();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (m_dropTargets.contains(watched)) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* drag = static_cast<QDropEvent*>(event);
            if (!dropPathFor(watched, drag->mimeData()).isEmpty()) {
                drag->acceptProposedAction();
                return true;
            }
            if (m_dropTargets.value(watched).target) {
                // a specific box that does not take this (e.g. a folder on the
                // playout file): refuse it here rather than letting it bubble
                // up to the window-wide fallback (Qt keeps propagating drag
                // events that are not accepted, so accept with no action =
                // the "no drop" cursor, stopped here)
                drag->setDropAction(Qt::IgnoreAction);
                drag->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            const QString path = dropPathFor(watched, drop->mimeData());
            if (path.isEmpty() && m_dropTargets.value(watched).target) {
                drop->setDropAction(Qt::IgnoreAction);
                drop->accept();
                return true;
            }
            if (!path.isEmpty()) {
                QLineEdit* target = m_dropTargets.value(watched).target;
                if (!target) {   // window fallback: folder -> recording dir, file -> playout file
                    target = QFileInfo(path).isDir() ? m_dirEdit : m_playFileEdit;
                }
                target->setText(QDir::toNativeSeparators(path));
                drop->acceptProposedAction();
                statusBar()->showMessage(QStringLiteral("Set from drop: %1").arg(QDir::toNativeSeparators(path)), 4000);
                if (target == m_playFileEdit) {
                    appendActivity(QStringLiteral("playout file set by drop: %1 - press Play File to Output").arg(QFileInfo(path).fileName()));
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

QCheckBox* MainWindow::makeCheckBox(const QString& label) {
    return new QCheckBox(label, this);
}

QLineEdit* MainWindow::makeLineEdit() {
    return new QLineEdit(this);
}

void MainWindow::buildUi() {
    const auto styleIcon = [](QStyle::StandardPixmap pixmap) {
        return QApplication::style()->standardIcon(pixmap);
    };
    const auto logoIcon = [this](const QString& fileName) {
        return QIcon(QDir(m_backend.packageRoot()).filePath(QStringLiteral("Resources/%1").arg(fileName)));
    };

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    m_tabs = new QTabWidget(central);

    m_deckLinkCombo = new QComboBox(this);
    // every *_CHOICE combo offers exactly the shell backend's option values
    // (VrecordOptions.h): anything else makes `vrecord -r` abort in
    // _review_option before the capture starts
    m_videoInputCombo = makeCombo(VrecordOptions::videoInputs());
    m_audioInputCombo = makeCombo(VrecordOptions::audioInputs());
    m_audioMappingCombo = makeCombo(VrecordOptions::channelMappings());
    m_standardCombo = makeCombo(VrecordOptions::standards());
    m_videoOutputConnectionCombo = makeCombo({QStringLiteral("Auto"),
                                              QStringLiteral("SDI"),
                                              QStringLiteral("HDMI"),
                                              QStringLiteral("Component"),
                                              QStringLiteral("Composite"),
                                              QStringLiteral("S-Video")});
    m_bitDepthCombo = makeCombo(VrecordOptions::bitDepths());
    m_aspectRatioCombo = makeCombo(VrecordOptions::aspectRatios());
    m_enforceStandardCheck = makeCheckBox(QStringLiteral("Enforce manual selection"));
    m_enforceStandardCheck->setToolTip(QStringLiteral("Unticked: the Standard follows the mode the card locks on the input, and the preview switches with it.\n"
                                                      "Ticked: always capture the Standard chosen here, whatever the input sends."));
    m_enforceAspectCheck = makeCheckBox(QStringLiteral("Enforce manual selection"));
    m_enforceAspectCheck->setToolTip(QStringLiteral("Unticked: the aspect ratio follows the input - 16/9 for HD and larger, and for PAL\n"
                                                    "the widescreen signalling (WSS) on line 23 when the source sends it.\n"
                                                    "Ticked: always use the aspect ratio chosen here."));
    m_embedLogsCombo = makeCombo(VrecordOptions::embedLogs());
    m_containerCombo = makeCombo(VrecordOptions::containers());
    labelContainerItems(m_containerCombo);
    m_videoCodecCombo = makeCombo(VrecordOptions::videoCodecs());
    m_audioCodecCombo = makeCombo(VrecordOptions::audioCodecs());
    labelAudioCodecItems(m_audioCodecCombo);
    m_audioCodecCombo->setToolTip(QStringLiteral("Opus can only be recorded to MKV or MP4."));
    m_audioBitDepthCombo = makeCombo(VrecordOptions::audioBitDepths());
    m_audioBitDepthCombo->setToolTip(QStringLiteral("Bit depth of PCM and FLAC audio (AAC and Opus are lossy and have none)"));
    m_playbackViewCombo = makeCombo(VrecordOptions::playbackViews());
    m_playbackPassCombo = makeCombo(VrecordOptions::playbackViews());
    m_mp4Check = makeCheckBox(QStringLiteral("Access MP4"));
    m_frameMd5Check = makeCheckBox(QStringLiteral("FrameMD5s"));
    m_timecodeVttCheck = makeCheckBox(QStringLiteral("Timecode VTT"));
    m_qcToolsCheck = makeCheckBox(QStringLiteral("Vrecord QCTools Report"));

    // audio (GTK has these on its main page too): monitoring, the capture
    // channel count, the DeckLink capture rate, and live level meters
    m_monitorAudioCombo = makeCombo(VrecordOptions::monitorAudio());
    m_captureChannelsCombo = makeCombo(VrecordOptions::captureChannels());
    m_captureChannelsCombo->setToolTip(QStringLiteral("Audio channels captured from the card (the channel mapping picks the tracks).\n"
                                                      "2: channels 1-2 only. 4: channels 1-4 (the card captures 8). 8 / 16: all embedded channels."));
    m_ffplayMonitorCheck = makeCheckBox(QStringLiteral("ffplay monitor window (scopes / views)"));
    m_ffplayAspectCombo = makeCombo({QStringLiteral("16:9"), QStringLiteral("21:9"), QStringLiteral("5:4")});
    m_ffplayAspectCombo->setToolTip(QStringLiteral("Shape of the ffplay monitor view, to match the monitor it is shown on:\n"
                                                   "the window opens in this shape and fullscreen (f / double-click) fills that screen.\n"
                                                   "In the window, w cycles through the views."));
    m_ffplayMonitorCheck->setToolTip(QStringLiteral("Ticked: Record and Passthrough open the ffplay monitor with the selected view.\n"
                                                    "Unticked: they show in the Video Preview pane instead (no ffplay window)."));
    m_previewMeter = new LevelMeter(this);

    auto* deckTab = new QWidget(this);
    auto* deckRoot = new QVBoxLayout(deckTab);
    auto* deckInputBox = new QGroupBox(QStringLiteral("Decklink Control Options"), deckTab);
    auto* deckInputLayout = new QGridLayout(deckInputBox);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Video Card"), deckInputBox), 0, 0);
    deckInputLayout->addWidget(m_deckLinkCombo, 1, 0);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Video Input"), deckInputBox), 0, 1);
    deckInputLayout->addWidget(m_videoInputCombo, 1, 1);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Video Output"), deckInputBox), 0, 2);
    deckInputLayout->addWidget(m_videoOutputConnectionCombo, 1, 2);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Audio Input"), deckInputBox), 0, 3);
    deckInputLayout->addWidget(m_audioInputCombo, 1, 3);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Audio Channel Mapping"), deckInputBox), 2, 0);
    deckInputLayout->addWidget(m_audioMappingCombo, 3, 0);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Standard"), deckInputBox), 2, 1);
    deckInputLayout->addWidget(m_standardCombo, 3, 1);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Video Bit Depth"), deckInputBox), 2, 2);
    deckInputLayout->addWidget(m_bitDepthCombo, 3, 2);
    deckInputLayout->addWidget(new QLabel(QStringLiteral("Display Aspect Ratio"), deckInputBox), 2, 3);
    deckInputLayout->addWidget(m_aspectRatioCombo, 3, 3);
    deckInputLayout->addWidget(m_enforceStandardCheck, 4, 1);
    deckInputLayout->addWidget(m_enforceAspectCheck, 4, 3);

    auto* deckOutputBox = new QGroupBox(QStringLiteral("Output file options"), deckTab);
    auto* deckOutputLayout = new QGridLayout(deckOutputBox);
    deckOutputLayout->addWidget(new QLabel(QStringLiteral("Embed digitization logs"), deckOutputBox), 0, 0);
    deckOutputLayout->addWidget(m_embedLogsCombo, 1, 0);
    deckOutputLayout->addWidget(new QLabel(QStringLiteral("Select File Format"), deckOutputBox), 0, 1);
    deckOutputLayout->addWidget(m_containerCombo, 1, 1);
    deckOutputLayout->addWidget(new QLabel(QStringLiteral("Select Video Codec"), deckOutputBox), 0, 2);
    deckOutputLayout->addWidget(m_videoCodecCombo, 1, 2);
    deckOutputLayout->addWidget(new QLabel(QStringLiteral("Select Audio Codec"), deckOutputBox), 0, 3);
    deckOutputLayout->addWidget(m_audioCodecCombo, 1, 3);
    deckOutputLayout->addWidget(new QLabel(QStringLiteral("Audio Bit Depth"), deckOutputBox), 0, 4);
    deckOutputLayout->addWidget(m_audioBitDepthCombo, 1, 4);

    auto* deckLowerRow = new QHBoxLayout();
    auto* playbackBox = new QGroupBox(QStringLiteral("Playback options"), deckTab);
    auto* playbackLayout = new QFormLayout(playbackBox);
    playbackLayout->addRow(m_ffplayMonitorCheck);
    playbackLayout->addRow(QStringLiteral("View shape (monitor)"), m_ffplayAspectCombo);
    playbackLayout->addRow(QStringLiteral("View (recording)"), m_playbackViewCombo);
    playbackLayout->addRow(QStringLiteral("View (passthrough)"), m_playbackPassCombo);
    auto* sidecarBox = new QGroupBox(QStringLiteral("Sidecar file options"), deckTab);
    auto* sidecarLayout = new QVBoxLayout(sidecarBox);
    sidecarLayout->addWidget(m_mp4Check);
    sidecarLayout->addWidget(m_frameMd5Check);
    sidecarLayout->addWidget(m_timecodeVttCheck);
    sidecarLayout->addWidget(m_qcToolsCheck);
    auto* rs422Box = new QGroupBox(QStringLiteral("RS422 deck control and timecode"), deckTab);
    // RS-422 deck control (packaged deckcontrol) + timecode scan, as in GTK:
    // [Deck Control] + icon-only transport on one row, the readouts under it,
    // then the scan button and its results
    m_deckControl = new DeckControl(m_backend.packageRoot(), this);
    auto* rs422Layout = new QVBoxLayout(rs422Box);
    m_deckControlToggle = makeCheckBox(QStringLiteral("Deck Control"));
    m_deckControlToggle->setToolTip(m_deckControl->isAvailable()
                                        ? QStringLiteral("Control a video deck over RS-422 through the Blackmagic card (deckcontrol).\n"
                                                         "Polls the deck state and timecode twice a second while on.")
                                        : QStringLiteral("deckcontrol is not installed in the package bin folder."));
    m_deckControlToggle->setEnabled(m_deckControl->isAvailable());
    // eject has no standard icon: a triangle over a bar, in the button text colour
    const auto ejectIcon = [this]() {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::ButtonText));
        painter.drawPolygon(QPolygonF{QPointF(12, 4), QPointF(21, 14), QPointF(3, 14)});
        painter.drawRect(QRectF(3, 17, 18, 3));
        return QIcon(pixmap);
    };
    auto* deckButtonRow = new QHBoxLayout();
    deckButtonRow->setSpacing(4);
    deckButtonRow->addWidget(m_deckControlToggle);
    deckButtonRow->addSpacing(8);
    const QList<QPair<QString, QPair<QStyle::StandardPixmap, QString>>> deckVerbs{
        {QStringLiteral("rewind"), {QStyle::SP_MediaSeekBackward, QStringLiteral("Rewind")}},
        {QStringLiteral("stepback"), {QStyle::SP_MediaSkipBackward, QStringLiteral("Step back")}},
        {QStringLiteral("play"), {QStyle::SP_MediaPlay, QStringLiteral("Play")}},
        {QStringLiteral("stop"), {QStyle::SP_MediaStop, QStringLiteral("Stop")}},
        {QStringLiteral("stepforward"), {QStyle::SP_MediaSkipForward, QStringLiteral("Step forward")}},
        {QStringLiteral("fastforward"), {QStyle::SP_MediaSeekForward, QStringLiteral("Fast forward")}},
        {QStringLiteral("eject"), {QStyle::SP_CustomBase, QStringLiteral("Eject")}},
    };
    for (const auto& verb : deckVerbs) {
        // icon only (the name is the tooltip), the size of the playout transport
        const QIcon icon = verb.first == QStringLiteral("eject") ? ejectIcon() : styleIcon(verb.second.first);
        auto* button = new QPushButton(icon, QString(), rs422Box);
        button->setIconSize(QSize(20, 20));
        button->setFixedSize(40, 30);
        button->setToolTip(QStringLiteral("Deck: %1").arg(verb.second.second));
        button->setAccessibleName(verb.second.second);
        button->setEnabled(false);
        button->setProperty("deckVerb", verb.first);
        connect(button, &QPushButton::clicked, this, [this, button] {
            m_deckControl->command(button->property("deckVerb").toString());
        });
        m_deckButtons << button;
        deckButtonRow->addWidget(button);
    }
    deckButtonRow->addStretch(1);
    rs422Layout->addLayout(deckButtonRow);
    auto* deckReadout = new QFormLayout();
    m_deckStateLabel = new QLabel(m_deckControl->isAvailable() ? QStringLiteral("disabled") : QStringLiteral("deckcontrol not installed"), rs422Box);
    m_deckStateLabel->setWordWrap(true);
    m_deckTimecodeLabel = new QLabel(QStringLiteral("--:--:--:--"), rs422Box);
    {
        QFont mono(QStringLiteral("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(mono.pointSize() + 3);
        mono.setBold(true);
        m_deckTimecodeLabel->setFont(mono);
    }
    deckReadout->addRow(QStringLiteral("Status"), m_deckStateLabel);
    deckReadout->addRow(QStringLiteral("Timecode"), m_deckTimecodeLabel);
    rs422Layout->addLayout(deckReadout);
    auto* scanRow = new QHBoxLayout();
    m_timecodeScanButton = new QPushButton(QStringLiteral("Scan timecode types"), rs422Box);
    m_timecodeScanButton->setToolTip(QStringLiteral("Read the input once with each timecode type (RP188, VITC, serial ...) and list what it carries.\n"
                                                    "The live preview pauses during the scan."));
    m_timecodeScanButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    scanRow->addWidget(m_timecodeScanButton);
    scanRow->addStretch(1);
    rs422Layout->addLayout(scanRow);
    m_timecodeScanText = new QPlainTextEdit(rs422Box);
    m_timecodeScanText->setReadOnly(true);
    m_timecodeScanText->setMaximumHeight(90);
    m_timecodeScanText->setPlaceholderText(QStringLiteral("Timecode scan results"));
    rs422Layout->addWidget(m_timecodeScanText);
    connect(m_deckControlToggle, &QCheckBox::toggled, this, &MainWindow::setDeckControlEnabled);
    connect(m_deckControl, &DeckControl::stateChanged, m_deckStateLabel, &QLabel::setText);
    connect(m_deckControl, &DeckControl::timecodeChanged, m_deckTimecodeLabel, &QLabel::setText);
    connect(m_deckControl, &DeckControl::commandFinished, this, [this](const QString& verb, const QString& message) {
        statusBar()->showMessage(QStringLiteral("Deck %1: %2").arg(verb, message), 4000);
        appendActivity(QStringLiteral("deck control: %1 - %2").arg(verb, message));
    });
    connect(m_timecodeScanButton, &QPushButton::clicked, this, &MainWindow::startTimecodeScan);
    deckLowerRow->addWidget(playbackBox);
    deckLowerRow->addWidget(sidecarBox);
    deckLowerRow->addWidget(rs422Box);
    auto* deckAudioBox = new QGroupBox(QStringLiteral("Audio"), deckTab);
    auto* deckAudioLayout = new QGridLayout(deckAudioBox);
    deckAudioLayout->addWidget(new QLabel(QStringLiteral("Monitor Audio?"), deckAudioBox), 0, 0);
    deckAudioLayout->addWidget(m_monitorAudioCombo, 1, 0);
    deckAudioLayout->addWidget(new QLabel(QStringLiteral("Capture channels"), deckAudioBox), 0, 1);
    deckAudioLayout->addWidget(m_captureChannelsCombo, 1, 1);
    deckAudioLayout->addWidget(new QLabel(QStringLiteral("Sample rate"), deckAudioBox), 0, 2);
    auto* rateLabel = new QLabel(QStringLiteral("48 kHz, 24-bit in a 32-bit word (DeckLink capture)"), deckAudioBox);
    rateLabel->setToolTip(QStringLiteral("DeckLink cards capture embedded and analog audio at 48 kHz only"));
    deckAudioLayout->addWidget(rateLabel, 1, 2);
    deckAudioLayout->addWidget(new QLabel(QStringLiteral("Level meters and the monitored pair are on the Video Preview tab."), deckAudioBox), 2, 0, 1, 3);
    deckRoot->addWidget(deckInputBox);
    deckRoot->addWidget(deckOutputBox);
    deckRoot->addWidget(deckAudioBox);
    deckRoot->addLayout(deckLowerRow);
    deckRoot->addStretch();
    m_tabs->addTab(deckTab, QStringLiteral("Decklink"));

    m_dvDeviceCombo = makeCombo({QStringLiteral("auto"), QStringLiteral("device 0")});
    m_dvContainerCombo = makeCombo(VrecordOptions::dvContainers());
    labelContainerItems(m_dvContainerCombo);
    m_dvOptionSCheck = makeCheckBox(QStringLiteral("recording start flags"));
    m_dvOptionDCheck = makeCheckBox(QStringLiteral("recording timestamps jumps"));
    m_dvOptionTCheck = makeCheckBox(QStringLiteral("timecode jumps"));
    m_dvOptionTcCheck = makeCheckBox(QStringLiteral("Embed captions as subtitles"));
    auto* dvTab = new QWidget(this);
    auto* dvRoot = new QVBoxLayout(dvTab);
    auto* dvInputBox = new QGroupBox(QStringLiteral("DVRescue input options"), dvTab);
    auto* dvInputLayout = new QHBoxLayout(dvInputBox);
    auto* dvDeviceForm = new QFormLayout();
    dvDeviceForm->addRow(QStringLiteral("Select a DV Device"), m_dvDeviceCombo);
    auto* dvPackagerBox = new QGroupBox(QStringLiteral("dvpackager options"), dvInputBox);
    m_dvPackagerBox = dvPackagerBox;   // enabled by the component check
    auto* dvPackagerLayout = new QVBoxLayout(dvPackagerBox);
    dvPackagerLayout->addWidget(new QLabel(QStringLiteral("Split the output on:"), dvPackagerBox));
    dvPackagerLayout->addWidget(m_dvOptionSCheck);
    dvPackagerLayout->addWidget(m_dvOptionDCheck);
    dvPackagerLayout->addWidget(m_dvOptionTCheck);
    dvPackagerLayout->addSpacing(8);
    dvPackagerLayout->addWidget(new QLabel(QStringLiteral("Other options:"), dvPackagerBox));
    dvPackagerLayout->addWidget(m_dvOptionTcCheck);
    dvPackagerLayout->addWidget(new QLabel(QStringLiteral("Select File Format"), dvPackagerBox));
    dvPackagerLayout->addWidget(m_dvContainerCombo);
    dvInputLayout->addLayout(dvDeviceForm, 1);
    dvInputLayout->addWidget(dvPackagerBox, 1);
    auto* dvControlBox = new QGroupBox(QStringLiteral("DV deck control"), dvTab);
    auto* dvControlLayout = new QVBoxLayout(dvControlBox);
    dvControlLayout->addWidget(new QLabel(QStringLiteral("DV deck control remains backend-managed on Windows; use Record / Passthrough for transport actions."), dvControlBox));
    dvRoot->addWidget(dvInputBox);
    dvRoot->addWidget(dvControlBox);
    dvRoot->addStretch();
    m_tabs->addTab(dvTab, QStringLiteral("DV"));

    m_audioDeviceCombo = makeCombo({QStringLiteral("default"), QStringLiteral("Blackmagic"), QStringLiteral("Line In"), QStringLiteral("Microphone")});
    m_audioModeCodecCombo = makeCombo(VrecordOptions::audioModeCodecs());
    labelAudioCodecItems(m_audioModeCodecCombo);
    m_audioModeBitDepthCombo = makeCombo(VrecordOptions::audioBitDepths());
    m_audioModeBitDepthCombo->setToolTip(m_audioBitDepthCombo->toolTip());
    m_audioChannelsCombo = makeCombo(VrecordOptions::audioChannels());
    m_audioSampleRateCombo = makeCombo(VrecordOptions::audioSampleRates());
    auto* audioTab = new QWidget(this);
    auto* audioRoot = new QVBoxLayout(audioTab);
    auto* audioBox = new QGroupBox(QStringLiteral("Audio input options"), audioTab);
    auto* audioLayout = new QGridLayout(audioBox);
    audioLayout->addWidget(new QLabel(QStringLiteral("Select an Audio Device"), audioBox), 0, 0);
    audioLayout->addWidget(m_audioDeviceCombo, 1, 0);
    audioLayout->addWidget(new QLabel(QStringLiteral("Select Audio Codec"), audioBox), 0, 1);
    audioLayout->addWidget(m_audioModeCodecCombo, 1, 1);
    audioLayout->addWidget(new QLabel(QStringLiteral("Select Audio Channels"), audioBox), 0, 2);
    audioLayout->addWidget(m_audioChannelsCombo, 1, 2);
    audioLayout->addWidget(new QLabel(QStringLiteral("Select Sampling Rate"), audioBox), 0, 3);
    audioLayout->addWidget(m_audioSampleRateCombo, 1, 3);
    audioLayout->addWidget(new QLabel(QStringLiteral("Bit Depth"), audioBox), 0, 4);
    audioLayout->addWidget(m_audioModeBitDepthCombo, 1, 4);
    audioRoot->addWidget(audioBox);
    audioRoot->addStretch();
    m_tabs->addTab(audioTab, QStringLiteral("Audio"));

    m_waveformScaleCombo = makeCombo(VrecordOptions::waveformScales());
    m_signalViewCombo = makeCombo(VrecordOptions::signalViews());
    m_signalIntCombo = makeCombo(VrecordOptions::signalInterlacements());
    m_ntsc43SarCombo = makeCombo(VrecordOptions::ntsc43Sars());
    m_ntsc169SarCombo = makeCombo(VrecordOptions::ntsc169Sars());
    m_pal43SarCombo = makeCombo(VrecordOptions::pal43Sars());
    m_pal169SarCombo = makeCombo(VrecordOptions::pal169Sars());
    m_ccVttCheck = makeCheckBox(QStringLiteral("Make a webVTT"));
    m_ccSrtCheck = makeCheckBox(QStringLiteral("Make an SRT"));
    m_capToMp4Check = makeCheckBox(QStringLiteral("Wrap captions into Access MP4 file"));
    m_mediaIdTypeEdit = makeLineEdit();
    m_webVttExtraEdit = makeLineEdit();
    m_colorSafeCheck = makeCheckBox(QStringLiteral("Enable color free alerts"));
    m_themeCombo = makeCombo(VrecordOptions::themes());
    m_invertPhase2Check = makeCheckBox(QStringLiteral("Invert channel 2"));
    m_invertPhase4Check = makeCheckBox(QStringLiteral("Invert channel 4"));
    m_ffmpegStatusText = new QPlainTextEdit(this);
    m_ffmpegStatusText->setReadOnly(true);
    m_environmentText = new QPlainTextEdit(this);
    m_environmentText->setReadOnly(true);

    auto* configTab = new QWidget(this);
    auto* configRoot = new QVBoxLayout(configTab);
    auto* prefTabs = new QTabWidget(configTab);
    auto* playerPage = new QWidget(prefTabs);
    auto* playerForm = new QFormLayout(playerPage);
    playerForm->addRow(QStringLiteral("Select Waveform Scale"), m_waveformScaleCombo);
    playerForm->addRow(QStringLiteral("Select Video Scale"), m_signalViewCombo);
    playerForm->addRow(QStringLiteral("Force Interlacement"), m_signalIntCombo);
    playerForm->addRow(new QLabel(QStringLiteral("Monitor Audio is on the Decklink page."), playerPage));
    prefTabs->addTab(playerPage, QStringLiteral("Player"));
    auto* aspectPage = new QWidget(prefTabs);
    auto* aspectForm = new QFormLayout(aspectPage);
    aspectForm->addRow(QStringLiteral("NTSC 4/3"), m_ntsc43SarCombo);
    aspectForm->addRow(QStringLiteral("NTSC 16/9"), m_ntsc169SarCombo);
    aspectForm->addRow(QStringLiteral("PAL 4/3"), m_pal43SarCombo);
    aspectForm->addRow(QStringLiteral("PAL 16/9"), m_pal169SarCombo);
    prefTabs->addTab(aspectPage, QStringLiteral("Aspect Ratio"));
    auto* ccPage = new QWidget(prefTabs);
    auto* ccLayout = new QVBoxLayout(ccPage);
    ccLayout->addWidget(m_ccVttCheck);
    ccLayout->addWidget(m_ccSrtCheck);
    ccLayout->addWidget(m_capToMp4Check);
    ccLayout->addStretch();
    prefTabs->addTab(ccPage, QStringLiteral("Closed Captioning"));
    auto* fadgiPage = new QWidget(prefTabs);
    auto* fadgiForm = new QFormLayout(fadgiPage);
    fadgiForm->addRow(QStringLiteral("Media Identifier Type"), m_mediaIdTypeEdit);
    fadgiForm->addRow(QStringLiteral("Additional FADGI WebVTT header details"), m_webVttExtraEdit);
    prefTabs->addTab(fadgiPage, QStringLiteral("FADGI WebVTT header"));
    auto* colorPage = new QWidget(prefTabs);
    auto* colorLayout = new QVBoxLayout(colorPage);
    colorLayout->addWidget(m_colorSafeCheck);
    colorLayout->addStretch();
    prefTabs->addTab(colorPage, QStringLiteral("Color Options"));
    auto* themePage = new QWidget(prefTabs);
    auto* themeForm = new QFormLayout(themePage);
    themeForm->addRow(QStringLiteral("Select Theme"), m_themeCombo);
    prefTabs->addTab(themePage, QStringLiteral("Themes"));
    prefTabs->addTab(buildUpdatesPage(prefTabs), QStringLiteral("Updates"));

    auto* lowerConfigRow = new QHBoxLayout();
    auto* invertBox = new QGroupBox(QStringLiteral("Invert Audio Phase"), configTab);
    auto* invertLayout = new QVBoxLayout(invertBox);
    invertLayout->addWidget(new QLabel(QStringLiteral("WARNING: Do not use this option unless required"), invertBox));
    invertLayout->addWidget(m_invertPhase2Check);
    invertLayout->addWidget(m_invertPhase4Check);
    // installed / deployed components: the GTK version's tick lights, for
    // the required parts as well as the optional tools
    auto* toolsBox = new QGroupBox(QStringLiteral("Installed components"), configTab);
    auto* toolsLayout = new QVBoxLayout(toolsBox);
    m_componentsPanel = new QWidget(toolsBox);
    m_componentsGrid = new QGridLayout(m_componentsPanel);
    m_componentsGrid->setContentsMargins(0, 0, 0, 0);
    m_componentsGrid->setHorizontalSpacing(8);
    m_componentsGrid->setVerticalSpacing(2);
    toolsLayout->addWidget(m_componentsPanel);
    auto* componentsFooter = new QHBoxLayout();
    m_componentsSummary = new QLabel(QStringLiteral("Checking ..."), toolsBox);
    m_componentsRecheck = new QPushButton(styleIcon(QStyle::SP_BrowserReload), QString(), toolsBox);
    m_componentsRecheck->setIconSize(QSize(20, 20));
    m_componentsRecheck->setFixedSize(40, 30);
    m_componentsRecheck->setToolTip(QStringLiteral("Check the installed components again"));
    m_componentsRecheck->setAccessibleName(QStringLiteral("Re-check"));
    componentsFooter->addWidget(m_componentsSummary, 1);
    componentsFooter->addWidget(m_componentsRecheck);
    toolsLayout->addLayout(componentsFooter);
    toolsLayout->addStretch(1);
    connect(m_componentsRecheck, &QPushButton::clicked, this, &MainWindow::refreshComponents);
    auto* statusBox = new QGroupBox(QStringLiteral("FFmpeg Status"), configTab);
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addWidget(m_ffmpegStatusText);
    auto* envBox = new QGroupBox(QStringLiteral("Environment"), configTab);
    auto* envLayout = new QVBoxLayout(envBox);
    envLayout->addWidget(m_environmentText);
    lowerConfigRow->addWidget(invertBox);
    lowerConfigRow->addWidget(toolsBox);
    lowerConfigRow->addWidget(statusBox);
    lowerConfigRow->addWidget(envBox);
    configRoot->addWidget(prefTabs);
    configRoot->addLayout(lowerConfigRow);
    m_tabs->addTab(configTab, QStringLiteral("Config"));

    m_previewRelayCheck = makeCheckBox(QStringLiteral("Relay live feed to the device output"));
    m_videoOutputDeviceCombo = new QComboBox(this);
    m_relayDeviceCombo = new QComboBox(this);
    m_filePlayAutoCheck = makeCheckBox(QStringLiteral("Auto-detect file format (play natively)"));
    m_filePlayAutoCheck->hide();   // folded into the output standard's "Auto" entry
    m_outputStandardCombo = makeCombo(QStringList{QStringLiteral("Auto (match file)")} + VrecordOptions::standards());
    m_previewInfoText = new QPlainTextEdit(this);
    m_previewInfoText->setReadOnly(true);
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(700);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        QString error;
        if (!m_backend.saveConfig(m_config, &error)) {
            statusBar()->showMessage(error, 5000);
        }
    });
    m_feedTimer = new QTimer(this);
    m_feedTimer->setInterval(100);
    auto* previewTab = new QWidget(this);
    auto* previewRoot = new QHBoxLayout(previewTab);
    auto* livePreviewBox = new QGroupBox(QStringLiteral("Live input preview"), previewTab);
    auto* livePreviewLayout = new QVBoxLayout(livePreviewBox);
    auto* outputDeviceBox = new QGroupBox(QStringLiteral("Output device feed"), previewTab);
    auto* outputDeviceLayout = new QVBoxLayout(outputDeviceBox);
    m_inputView = new VideoView(livePreviewBox);
    m_outputView = new VideoView(outputDeviceBox);
    m_outputView->clearFrame(QStringLiteral("No hardware output running"));
    const QSize transportIconSize(28, 28);
    const QSize transportButtonSize(56, 40);
    m_playPauseButton = new QPushButton(styleIcon(QStyle::SP_MediaPlay), QString(), outputDeviceBox);
    m_playPauseButton->setToolTip(QStringLiteral("Play / Pause (holds the exact frame)"));
    m_playPauseButton->setIconSize(transportIconSize);
    m_playPauseButton->setMinimumSize(transportButtonSize);
    auto* btnBack100 = new QPushButton(styleIcon(QStyle::SP_MediaSkipBackward), QString(), outputDeviceBox);
    btnBack100->setToolTip(QStringLiteral("Step back 100 frames"));
    btnBack100->setIconSize(transportIconSize);
    btnBack100->setMinimumSize(transportButtonSize);
    auto* btnBack1 = new QPushButton(styleIcon(QStyle::SP_ArrowLeft), QString(), outputDeviceBox);
    btnBack1->setToolTip(QStringLiteral("Step back 1 frame"));
    btnBack1->setIconSize(transportIconSize);
    btnBack1->setMinimumSize(transportButtonSize);
    auto* btnFwd1 = new QPushButton(styleIcon(QStyle::SP_ArrowRight), QString(), outputDeviceBox);
    btnFwd1->setToolTip(QStringLiteral("Step forward 1 frame"));
    btnFwd1->setIconSize(transportIconSize);
    btnFwd1->setMinimumSize(transportButtonSize);
    auto* btnFwd100 = new QPushButton(styleIcon(QStyle::SP_MediaSkipForward), QString(), outputDeviceBox);
    btnFwd100->setToolTip(QStringLiteral("Step forward 100 frames"));
    btnFwd100->setIconSize(transportIconSize);
    btnFwd100->setMinimumSize(transportButtonSize);
    m_seekSlider = new QSlider(Qt::Horizontal, outputDeviceBox);
    m_seekSlider->setRange(0, 10000);
    // fixed-format, monospace counter: the digits never move between play,
    // pause and halt; the state sits in its own fixed-width tag
    // counter: an editable timecode (type HH:MM:SS:FF + Enter to jump), the
    // duration, and an editable frame number - fixed widths and a monospace
    // face so nothing shifts between play, pause and halt
    QFont counterFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    counterFont.setPointSizeF(counterFont.pointSizeF() * 1.35);
    m_timecodeEdit = new QLineEdit(QStringLiteral("00:00:00:00"), outputDeviceBox);
    m_timecodeEdit->setFont(counterFont);
    m_timecodeEdit->setInputMask(QStringLiteral("99:99:99:99"));
    m_timecodeEdit->setAlignment(Qt::AlignCenter);
    m_timecodeEdit->setFixedWidth(QFontMetrics(counterFont).horizontalAdvance(QStringLiteral("00:00:00:00")) + 18);
    m_timecodeEdit->setToolTip(QStringLiteral("Position (HH:MM:SS:FF). Type a timecode and press Enter to jump there."));
    QFont durationFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_positionLabel = new QLabel(QStringLiteral("/ 00:00:00:00"), outputDeviceBox);
    m_positionLabel->setFont(durationFont);
    m_positionLabel->setFixedWidth(QFontMetrics(durationFont).horizontalAdvance(QStringLiteral("/ 00:00:00:00")) + 6);
    {
        QPalette dim = m_positionLabel->palette();
        dim.setColor(QPalette::WindowText, dim.color(QPalette::PlaceholderText));
        m_positionLabel->setPalette(dim);
    }
    m_frameSpin = new QSpinBox(outputDeviceBox);
    m_frameSpin->setFont(durationFont);
    m_frameSpin->setPrefix(QStringLiteral("frame "));
    m_frameSpin->setRange(0, 0);
    m_frameSpin->setKeyboardTracking(false);
    m_frameSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_frameSpin->setAlignment(Qt::AlignRight);
    m_frameSpin->setFixedWidth(QFontMetrics(durationFont).horizontalAdvance(QStringLiteral("frame 0000000")) + 16);
    m_frameSpin->setToolTip(QStringLiteral("Frame number. Type a frame and press Enter to jump there."));
    m_transportStateLabel = new QLabel(QStringLiteral("STOPPED"), outputDeviceBox);
    m_transportStateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        QFont stateFont = m_transportStateLabel->font();
        stateFont.setBold(true);
        m_transportStateLabel->setFont(stateFont);
        m_transportStateLabel->setFixedWidth(QFontMetrics(stateFont).horizontalAdvance(QStringLiteral("HALTED - RELAY ON OUTPUT")) + 12);
    }
    m_previewToggleButton = new QPushButton(styleIcon(QStyle::SP_MediaPlay), QStringLiteral("Start Preview"), livePreviewBox);
    m_previewToggleButton->setIconSize(QSize(20, 20));
    m_activityText = new QPlainTextEdit(livePreviewBox);
    m_activityText->setReadOnly(true);
    m_activityText->setMaximumBlockCount(400);
    m_testSignalOutputCheck = makeCheckBox(QStringLiteral("Test signal output"));
    m_testPatternCombo = makeCombo({QStringLiteral("SMPTE SD Bars (75%)"),
                                    QStringLiteral("SMPTE SD Bars (100%)"),
                                    QStringLiteral("EBU Bars (75%)"),
                                    QStringLiteral("EBU Bars (100%)"),
                                    QStringLiteral("SMPTE RP 219 \"HDTV\" Bars (75%)"),
                                    QStringLiteral("SMPTE RP 219 \"UHDTV\" Bars (75%, BT.2020)"),
                                    QStringLiteral("BT.2111 HDR Bars (HLG 1080p)"),
                                    QStringLiteral("BT.2111 HDR Bars (PQ 1080p)"),
                                    QStringLiteral("BT.2111 HDR Bars (HLG 2160p)"),
                                    QStringLiteral("BT.2111 HDR Bars (PQ 2160p)"),
                                    QStringLiteral("FFmpeg Bars"),
                                    QStringLiteral("Test Pattern"),
                                    QStringLiteral("RGB Bars"),
                                    QStringLiteral("YUV Bars"),
                                    QStringLiteral("Black"),
                                    QStringLiteral("White"),
                                    QStringLiteral("PLUGE"),
                                    QStringLiteral("Grid")});
    m_testAudioCombo = makeCombo({QStringLiteral("1 kHz Tone"), QStringLiteral("Stereo 1k/440"), QStringLiteral("Silence")});
    m_outputInfoText = new QPlainTextEdit(this);
    m_outputInfoText->setReadOnly(true);
    m_outputMeter = new LevelMeter(this);
    m_outputMeter->setTitle(QStringLiteral("out"));
    m_previewMeter->setTitle(QStringLiteral("in"));
    m_previewMeter->setPairOnly(true);   // 2 channels wide: the monitored pair
    m_monitorPairCombo = makeCombo({QStringLiteral("1/2"), QStringLiteral("3/4"), QStringLiteral("5/6"), QStringLiteral("7/8"),
                                    QStringLiteral("9/10"), QStringLiteral("11/12"), QStringLiteral("13/14"), QStringLiteral("15/16")});
    m_monitorPairCombo->setToolTip(QStringLiteral("Stereo pair to monitor: highlighted on the meters and sent to the relay output.\n"
                                                  "1/2 is the usual stereo of most sources."));

    // ---- input side: picture with its meters beside it
    livePreviewLayout->addWidget(new QLabel(QStringLiteral("Input preview (native frame rate, straight from the capture)"), livePreviewBox));
    auto* inputPictureRow = new QHBoxLayout();
    inputPictureRow->addWidget(m_inputView, 1);
    inputPictureRow->addWidget(m_previewMeter);
    livePreviewLayout->addLayout(inputPictureRow);
    auto* inputControlRow = new QHBoxLayout();
    inputControlRow->addWidget(m_previewToggleButton, 1);
    inputControlRow->addWidget(new QLabel(QStringLiteral("Monitor pair"), livePreviewBox));
    inputControlRow->addWidget(m_monitorPairCombo);
    livePreviewLayout->addLayout(inputControlRow);
    livePreviewLayout->addStretch(1);

    // ---- output side: devices side by side, the signal generator in one
    // compact block with the output standard, then picture + meters + transport
    auto* deviceGrid = new QGridLayout();
    deviceGrid->addWidget(new QLabel(QStringLiteral("Hardware output device"), outputDeviceBox), 0, 0);
    deviceGrid->addWidget(new QLabel(QStringLiteral("Relay output device"), outputDeviceBox), 0, 1);
    deviceGrid->addWidget(m_videoOutputDeviceCombo, 1, 0);
    deviceGrid->addWidget(m_relayDeviceCombo, 1, 1);
    deviceGrid->addWidget(m_previewRelayCheck, 2, 0);
    deviceGrid->addWidget(m_testSignalOutputCheck, 2, 1);
    deviceGrid->setColumnStretch(0, 1);
    deviceGrid->setColumnStretch(1, 1);
    outputDeviceLayout->addLayout(deviceGrid);
    auto* generatorBox = new QGroupBox(QStringLiteral("Signal / audio generator"), outputDeviceBox);
    // one row: compact standard, then pattern and audio side by side
    auto* generatorRow = new QHBoxLayout(generatorBox);
    m_outputStandardCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_outputStandardCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    generatorRow->addWidget(new QLabel(QStringLiteral("Standard"), generatorBox));
    generatorRow->addWidget(m_outputStandardCombo);
    generatorRow->addSpacing(8);
    generatorRow->addWidget(new QLabel(QStringLiteral("Pattern"), generatorBox));
    generatorRow->addWidget(m_testPatternCombo, 3);
    generatorRow->addSpacing(8);
    generatorRow->addWidget(new QLabel(QStringLiteral("Audio"), generatorBox));
    generatorRow->addWidget(m_testAudioCombo, 1);
    m_outputStandardCombo->setToolTip(QStringLiteral("Standard sent to the hardware output for file playout and the test signal.\n"
                                                     "Auto plays files in their native mode; relay on = the output follows the input."));
    outputDeviceLayout->addWidget(generatorBox);
    auto* outputPictureRow = new QHBoxLayout();
    outputPictureRow->addWidget(m_outputView, 1);
    outputPictureRow->addWidget(m_outputMeter);
    outputDeviceLayout->addLayout(outputPictureRow);
    auto* transportRow = new QHBoxLayout();
    transportRow->addWidget(btnBack100);
    transportRow->addWidget(btnBack1);
    transportRow->addWidget(m_playPauseButton);
    transportRow->addWidget(btnFwd1);
    transportRow->addWidget(btnFwd100);
    outputDeviceLayout->addLayout(transportRow);
    outputDeviceLayout->addWidget(m_seekSlider);
    auto* counterRow = new QHBoxLayout();
    counterRow->addWidget(m_timecodeEdit);
    counterRow->addWidget(m_positionLabel);
    counterRow->addSpacing(12);
    counterRow->addWidget(m_frameSpin);
    counterRow->addStretch(1);
    counterRow->addWidget(m_transportStateLabel);
    outputDeviceLayout->addLayout(counterRow);
    outputDeviceLayout->addStretch(1);
    previewRoot->addWidget(livePreviewBox, 1);
    previewRoot->addWidget(outputDeviceBox, 1);
    m_tabs->addTab(previewTab, QStringLiteral("Video Preview"));

    root->addWidget(m_tabs);

    auto* directoryRow = new QHBoxLayout();
    auto* dirBox = new QGroupBox(QStringLiteral("Select a recording directory"), central);
    auto* dirLayout = new QHBoxLayout(dirBox);
    m_dirEdit = makeLineEdit();
    auto* dirBrowseButton = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("Browse..."), dirBox);
    dirBrowseButton->setIconSize(QSize(20, 20));
    dirLayout->addWidget(m_dirEdit);
    dirLayout->addWidget(dirBrowseButton);
    auto* logDirBox = new QGroupBox(QStringLiteral("Select a directory for auxiliary files"), central);
    auto* logDirLayout = new QHBoxLayout(logDirBox);
    m_logDirEdit = makeLineEdit();
    auto* logDirBrowseButton = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("Browse..."), logDirBox);
    logDirBrowseButton->setIconSize(QSize(20, 20));
    logDirLayout->addWidget(m_logDirEdit);
    logDirLayout->addWidget(logDirBrowseButton);
    auto* playFileBox = new QGroupBox(QStringLiteral("Select a file to play to the output device"), central);
    auto* playFileLayout = new QHBoxLayout(playFileBox);
    m_playFileEdit = makeLineEdit();
    auto* playFileBrowseButton = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("Browse..."), playFileBox);
    playFileBrowseButton->setIconSize(QSize(20, 20));
    playFileLayout->addWidget(m_playFileEdit);
    playFileLayout->addWidget(playFileBrowseButton);
    directoryRow->addWidget(dirBox);
    directoryRow->addWidget(logDirBox);
    directoryRow->addWidget(playFileBox);
    // drag and drop: a folder (or a file - its folder is used) onto the
    // recording / auxiliary folder boxes, a media file onto the playout box or
    // straight onto the output picture
    for (QWidget* widget : std::initializer_list<QWidget*>{dirBox, m_dirEdit, dirBrowseButton}) {
        registerDropTarget(widget, m_dirEdit, true);
    }
    for (QWidget* widget : std::initializer_list<QWidget*>{logDirBox, m_logDirEdit, logDirBrowseButton}) {
        registerDropTarget(widget, m_logDirEdit, true);
    }
    for (QWidget* widget : std::initializer_list<QWidget*>{playFileBox, m_playFileEdit, playFileBrowseButton, m_outputView}) {
        registerDropTarget(widget, m_playFileEdit, false);
    }
    root->addLayout(directoryRow);

    auto* eventRow = new QHBoxLayout();
    auto* recordEventBox = new QGroupBox(QStringLiteral("Recording event options"), central);
    auto* recordEventLayout = new QGridLayout(recordEventBox);
    m_idEdit = makeLineEdit();
    m_technicianEdit = makeLineEdit();
    m_durationHoursEdit = makeLineEdit();
    m_durationMinutesEdit = makeLineEdit();
    m_durationSecondsEdit = makeLineEdit();
    recordEventLayout->addWidget(new QLabel(QStringLiteral("Name of Recording"), recordEventBox), 0, 0);
    recordEventLayout->addWidget(m_idEdit, 1, 0);
    recordEventLayout->addWidget(new QLabel(QStringLiteral("Name of the person digitizing this tape"), recordEventBox), 0, 1);
    recordEventLayout->addWidget(m_technicianEdit, 1, 1);
    recordEventLayout->addWidget(new QLabel(QStringLiteral("Recording time (HH:MM:SS)"), recordEventBox), 0, 2);
    auto* durationLayout = new QHBoxLayout();
    durationLayout->addWidget(m_durationHoursEdit);
    durationLayout->addWidget(new QLabel(QStringLiteral(":"), recordEventBox));
    durationLayout->addWidget(m_durationMinutesEdit);
    durationLayout->addWidget(new QLabel(QStringLiteral(":"), recordEventBox));
    durationLayout->addWidget(m_durationSecondsEdit);
    recordEventLayout->addLayout(durationLayout, 1, 2);

    auto* namingBox = new QGroupBox(QStringLiteral("File naming options"), central);
    auto* namingLayout = new QGridLayout(namingBox);
    m_prefixEdit = makeLineEdit();
    m_suffixEdit = makeLineEdit();
    m_enableSuffixCheck = makeCheckBox(QStringLiteral("Enable suffixes"));
    namingLayout->addWidget(new QLabel(QStringLiteral("Optional filename prefix"), namingBox), 0, 0);
    namingLayout->addWidget(m_prefixEdit, 1, 0);
    namingLayout->addWidget(new QLabel(QStringLiteral("Optional filename suffix"), namingBox), 0, 1);
    namingLayout->addWidget(m_suffixEdit, 1, 1);
    namingLayout->addWidget(m_enableSuffixCheck, 1, 2);
    eventRow->addWidget(recordEventBox, 2);
    eventRow->addWidget(namingBox, 1);
    root->addLayout(eventRow);

    auto* outputNameBox = new QGroupBox(QStringLiteral("Output Filename"), central);
    auto* outputNameLayout = new QVBoxLayout(outputNameBox);
    auto* outputNameLabel = new QLabel(outputNameBox);
    outputNameLayout->addWidget(outputNameLabel);

    auto* inputModeBox = new QGroupBox(QStringLiteral("Input Mode"), central);
    auto* inputModeLayout = new QHBoxLayout(inputModeBox);
    for (const auto& entry : QMap<QString, QString>{{QStringLiteral("0"), QStringLiteral("DeckLink")},
                                                    {QStringLiteral("1"), QStringLiteral("DV")},
                                                    {QStringLiteral("2"), QStringLiteral("Audio")}}.asKeyValueRange()) {
        auto* radio = new QRadioButton(entry.second, inputModeBox);
        m_inputChoiceRadios.insert(entry.first, radio);
        inputModeLayout->addWidget(radio);
    }
    inputModeLayout->addStretch();

    auto* actionBar = new QHBoxLayout();
    auto* refreshDevicesButton = new QPushButton(styleIcon(QStyle::SP_BrowserReload), QStringLiteral("Refresh Devices"), central);
    auto* saveButton = new QPushButton(styleIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Save Settings"), central);
    auto* recordButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo.png")), QStringLiteral("Record"), central);
    m_recordButton = recordButton;
    auto* passthroughButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo_playback.png")), QStringLiteral("Passthrough"), central);
    auto* audioButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo_audio.png")), QStringLiteral("Audio Check"), central);
    auto* fileOutputButton = new QPushButton(styleIcon(QStyle::SP_MediaPlay), QStringLiteral("Play File to Output"), central);
    auto* stopOutputButton = new QPushButton(styleIcon(QStyle::SP_MediaStop), QStringLiteral("Stop Output"), central);
    for (QPushButton* button : {refreshDevicesButton, saveButton, recordButton, passthroughButton,
                                audioButton, fileOutputButton, stopOutputButton}) {
        button->setIconSize(QSize(24, 24));
    }

    actionBar->addWidget(refreshDevicesButton);
    actionBar->addWidget(saveButton);
    actionBar->addStretch();
    actionBar->addWidget(recordButton);
    actionBar->addWidget(passthroughButton);
    actionBar->addWidget(audioButton);
    actionBar->addWidget(fileOutputButton);
    actionBar->addWidget(stopOutputButton);
    auto* summaryRow = new QHBoxLayout();
    summaryRow->addWidget(inputModeBox, 1);
    summaryRow->addWidget(outputNameBox, 2);
    root->addLayout(summaryRow);
    root->addLayout(actionBar);

    auto* logsTab = new QWidget(this);
    auto* logsRoot = new QVBoxLayout(logsTab);
    auto* logsSplitter = new QSplitter(Qt::Vertical, logsTab);
    auto* summaryBox = new QGroupBox(QStringLiteral("Settings Summary"), logsTab);
    auto* summaryLayout = new QVBoxLayout(summaryBox);
    m_summaryText = new QPlainTextEdit(summaryBox);
    m_summaryText->setReadOnly(true);
    summaryLayout->addWidget(m_summaryText);
    auto* validationBox = new QGroupBox(QStringLiteral("Errors & Warnings (incl. self test)"), logsTab);
    auto* validationLayout = new QVBoxLayout(validationBox);
    m_validationText = new QPlainTextEdit(validationBox);
    m_validationText->setReadOnly(true);
    validationLayout->addWidget(m_validationText);
    auto* activityBox = new QGroupBox(QStringLiteral("Activity (preview, playout, backend sessions)"), logsTab);
    auto* activityLayout = new QVBoxLayout(activityBox);
    activityLayout->addWidget(m_activityText);
    auto* notesBox = new QGroupBox(QStringLiteral("Preview / playout / test signal notes"), logsTab);
    auto* notesLayout = new QHBoxLayout(notesBox);
    notesLayout->addWidget(m_previewInfoText);
    notesLayout->addWidget(m_outputInfoText);
    logsSplitter->addWidget(activityBox);
    logsSplitter->addWidget(notesBox);
    logsSplitter->addWidget(summaryBox);
    logsSplitter->addWidget(validationBox);
    logsSplitter->setStretchFactor(0, 2);
    logsSplitter->setStretchFactor(1, 1);
    logsSplitter->setStretchFactor(2, 1);
    logsSplitter->setStretchFactor(3, 1);
    logsRoot->addWidget(logsSplitter);
    m_tabs->addTab(logsTab, QStringLiteral("Logs"));

    setCentralWidget(central);
    // drop anywhere else in the window: a folder sets the recording directory,
    // a file the playout file (the specific boxes above take precedence)
    registerDropTarget(central, nullptr, false);
    setWindowTitle(QStringLiteral("vrecord Qt6"));
    resize(1380, 920);

    const auto updateSlots = [this] {
        updateDerivedState();
    };
    for (auto* edit : {m_idEdit, m_dirEdit, m_logDirEdit, m_playFileEdit, m_technicianEdit, m_durationHoursEdit,
                       m_durationMinutesEdit, m_durationSecondsEdit, m_prefixEdit, m_suffixEdit, m_mediaIdTypeEdit,
                       m_webVttExtraEdit}) {
        connect(edit, &QLineEdit::textChanged, this, updateSlots);
    }
    for (auto* combo : {m_deckLinkCombo, m_videoInputCombo, m_audioInputCombo, m_audioMappingCombo, m_standardCombo,
                        m_embedLogsCombo, m_containerCombo, m_videoCodecCombo, m_audioCodecCombo, m_playbackViewCombo,
                        m_playbackPassCombo, m_bitDepthCombo, m_aspectRatioCombo, m_dvDeviceCombo, m_dvContainerCombo,
                        m_audioDeviceCombo, m_audioModeCodecCombo, m_audioChannelsCombo, m_audioSampleRateCombo,
                        m_waveformScaleCombo, m_signalViewCombo, m_signalIntCombo, m_monitorAudioCombo, m_ntsc43SarCombo,
                        m_ntsc169SarCombo, m_pal43SarCombo, m_pal169SarCombo, m_themeCombo, m_videoOutputDeviceCombo, m_relayDeviceCombo,
                        m_testPatternCombo, m_testAudioCombo, m_videoOutputConnectionCombo, m_ffplayAspectCombo,
                        m_audioBitDepthCombo, m_captureChannelsCombo}) {
        connect(combo, &QComboBox::currentTextChanged, this, updateSlots);
    }
    for (auto* check : {m_enableSuffixCheck, m_mp4Check, m_frameMd5Check, m_timecodeVttCheck, m_qcToolsCheck,
                        m_dvOptionSCheck, m_dvOptionDCheck, m_dvOptionTCheck, m_dvOptionTcCheck, m_ccVttCheck,
                        m_ccSrtCheck, m_capToMp4Check, m_colorSafeCheck, m_invertPhase2Check, m_invertPhase4Check,
                        m_previewRelayCheck, m_filePlayAutoCheck, m_testSignalOutputCheck, m_enforceAspectCheck}) {
        connect(check, &QCheckBox::checkStateChanged, this, updateSlots);
    }
    for (auto* radio : m_inputChoiceRadios) {
        connect(radio, &QRadioButton::toggled, this, updateSlots);
    }
    connect(m_idEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        if (m_populating) {
            return;
        }
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_prefixEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        if (m_populating) {
            return;
        }
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_suffixEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        if (m_populating) {
            return;
        }
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_enableSuffixCheck, &QCheckBox::checkStateChanged, this, [outputNameLabel, this] {
        if (m_populating) {
            return;
        }
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_dirEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        if (m_populating) {
            return;
        }
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });

    connect(refreshDevicesButton, &QPushButton::clicked, this, &MainWindow::refreshDeckLinkDevices);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveConfig);
    connect(recordButton, &QPushButton::clicked, this, &MainWindow::startRecord);
    connect(passthroughButton, &QPushButton::clicked, this, &MainWindow::startPassthrough);
    connect(audioButton, &QPushButton::clicked, this, &MainWindow::startAudioCheck);
    connect(fileOutputButton, &QPushButton::clicked, this, &MainWindow::startFileOutput);
    connect(stopOutputButton, &QPushButton::clicked, this, &MainWindow::stopAllOutputs);
    connect(m_testSignalOutputCheck, &QCheckBox::clicked, this, &MainWindow::setTestSignalOutput);
    connect(m_themeCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        applySelectedTheme();
    });
    connect(dirBrowseButton, &QPushButton::clicked, this, [this] {
        const QString startDir = m_dirEdit->text().trimmed().isEmpty() ? QDir::homePath() : m_dirEdit->text().trimmed();
        const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select recording directory"), startDir);
        if (!selected.isEmpty()) {
            m_dirEdit->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(logDirBrowseButton, &QPushButton::clicked, this, [this] {
        QString startDir = m_logDirEdit->text().trimmed();
        if (startDir.isEmpty()) {
            startDir = m_dirEdit->text().trimmed().isEmpty() ? QDir::homePath() : m_dirEdit->text().trimmed();
        }
        const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select auxiliary directory"), startDir);
        if (!selected.isEmpty()) {
            m_logDirEdit->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(playFileBrowseButton, &QPushButton::clicked, this, [this] {
        QString startPath = m_playFileEdit->text().trimmed();
        if (startPath.isEmpty()) {
            startPath = m_dirEdit->text().trimmed().isEmpty() ? QDir::homePath() : m_dirEdit->text().trimmed();
        }
        const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select a file to play to the output device"), startPath);
        if (!selected.isEmpty()) {
            m_playFileEdit->setText(QDir::toNativeSeparators(selected));
        }
    });
    const auto restartTestSignalIfRunning = [this] {
        if (m_testSignalOutputCheck->isChecked()) {
            setTestSignalOutput();
        }
    };
    connect(m_standardCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_outputStandardCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_testPatternCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_testAudioCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_videoOutputDeviceCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_relayDeviceCombo, &QComboBox::activated, this, [restartTestSignalIfRunning](int) {
        restartTestSignalIfRunning();
    });
    connect(m_videoOutputConnectionCombo, &QComboBox::activated, this, [this, restartTestSignalIfRunning](int) {
        applyVideoOutputConnection();
        restartTestSignalIfRunning();
    });
    // input-side selections take effect on the running preview right away
    // (ffmpeg's -video_input selects the connector, -format_code the mode)
    const auto restartPreviewIfRunning = [this] {
        // take the new choice into the config BEFORE relaunching, otherwise
        // the preview restarts with the previous settings
        updateDerivedState();
        if (m_backend.isVideoPreviewActive()) {
            m_backend.stopVideoPreview();
            startPreviewQuietly();
        }
    };
    // the relay is part of the preview process: toggling it (or its device /
    // the audio feeding it) relaunches the preview, starting it if needed
    const auto restartPreviewForRelay = [this] {
        updateDerivedState();
        if (m_backend.isShellRunning()) {
            return;
        }
        // while playout / the test signal owns the output the backend starts
        // the preview without the relay and adds it back when the output frees up
        m_backend.stopVideoPreview();
        startPreviewQuietly();
    };
    connect(m_previewRelayCheck, &QCheckBox::clicked, this, [this, restartPreviewForRelay](bool on) {
        // relay back on while a file is loaded: the file stops on its exact
        // frame (Play resumes there) and the relay takes the output
        if (on && m_backend.isFileOutputActive()) {
            m_backend.haltFileOutputForRelay();
            appendActivity(QStringLiteral("relay on: file halted at %1 s, press Play to resume it").arg(m_backend.fileOutputPositionSeconds(), 0, 'f', 3));
        }
        restartPreviewForRelay();
        updateTransportUi();
    });
    connect(m_relayDeviceCombo, &QComboBox::activated, this, [this, restartPreviewForRelay](int) {
        if (m_previewRelayCheck->isChecked()) {
            restartPreviewForRelay();
        }
    });
    connect(m_audioInputCombo, &QComboBox::activated, this, [this, restartPreviewIfRunning](int) {
        if (m_previewRelayCheck->isChecked()) {
            restartPreviewIfRunning();
        }
    });
    connect(m_deckLinkCombo, &QComboBox::activated, this, [this, restartPreviewIfRunning](int) {
        rebuildConnectorCombos();
        restartPreviewIfRunning();
    });
    connect(m_videoInputCombo, &QComboBox::activated, this, [restartPreviewIfRunning](int) { restartPreviewIfRunning(); });
    connect(m_standardCombo, &QComboBox::activated, this, [restartPreviewIfRunning](int) { restartPreviewIfRunning(); });
    connect(m_aspectRatioCombo, &QComboBox::activated, this, [restartPreviewIfRunning](int) { restartPreviewIfRunning(); });
    // enforcing switches the preview between autodetect and the chosen mode
    connect(m_enforceStandardCheck, &QCheckBox::clicked, this, [this, restartPreviewIfRunning](bool on) {
        m_enforcedInputNotice.clear();
        appendActivity(on ? QStringLiteral("Standard enforced: capturing %1 whatever the input sends").arg(m_standardCombo->currentText())
                          : QStringLiteral("Standard follows the input mode"));
        restartPreviewIfRunning();
    });
    connect(m_enforceAspectCheck, &QCheckBox::clicked, this, [this](bool on) {
        appendActivity(on ? QStringLiteral("Aspect ratio enforced: %1").arg(m_aspectRatioCombo->currentText())
                          : QStringLiteral("Aspect ratio follows the input (HD: 16/9, PAL: WSS)"));
    });
    // one bit depth, shown on the Decklink and the Audio tab
    connect(m_audioBitDepthCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_audioModeBitDepthCombo->currentIndex() != index) {
            m_audioModeBitDepthCombo->setCurrentIndex(index);
        }
    });
    connect(m_audioModeBitDepthCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_audioBitDepthCombo->currentIndex() != index) {
            m_audioBitDepthCombo->setCurrentIndex(index);
        }
    });
    connect(m_captureChannelsCombo, &QComboBox::activated, this, [restartPreviewIfRunning](int) { restartPreviewIfRunning(); });
    connect(m_monitorPairCombo, &QComboBox::activated, this, [this, restartPreviewIfRunning](int index) {
        m_previewMeter->setMonitorPair(index);
        // the relay carries this pair: relaunch it with the new mapping
        if (m_backend.isPreviewRelaying()) {
            restartPreviewIfRunning();
        } else {
            updateDerivedState();
        }
    });
    connect(m_monitorAudioCombo, &QComboBox::activated, this, [this](int) { updateDerivedState(); });
    connect(m_ffplayMonitorCheck, &QCheckBox::clicked, this, [this](bool) { updateDerivedState(); });
    connect(m_previewToggleButton, &QPushButton::clicked, this, &MainWindow::togglePreview);
    connect(m_playPauseButton, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    connect(btnBack100, &QPushButton::clicked, this, [this] { stepPlayback(-100); });
    connect(btnBack1, &QPushButton::clicked, this, [this] { stepPlayback(-1); });
    connect(btnFwd1, &QPushButton::clicked, this, [this] { stepPlayback(1); });
    connect(btnFwd100, &QPushButton::clicked, this, [this] { stepPlayback(100); });
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::seekToSlider);
    connect(m_timecodeEdit, &QLineEdit::returnPressed, this, [this] {
        // HH:MM:SS:FF at the nominal (rounded) rate, like the readout
        const QStringList parts = m_timecodeEdit->text().split(QLatin1Char(':'));
        const double fps = m_backend.fileOutputFps() > 0 ? m_backend.fileOutputFps() : 25.0;
        const qint64 nominal = qRound64(fps);
        if (parts.size() == 4 && nominal > 0) {
            const qint64 frame = ((parts.at(0).toLongLong() * 60 + parts.at(1).toLongLong()) * 60 + parts.at(2).toLongLong()) * nominal
                                 + qMin<qint64>(parts.at(3).toLongLong(), nominal - 1);
            jumpToFrame(frame);
        }
        m_timecodeEdit->clearFocus();
    });
    connect(m_frameSpin, &QSpinBox::editingFinished, this, [this] {
        if (m_frameSpin->hasFocus()) {
            jumpToFrame(m_frameSpin->value());
            m_frameSpin->clearFocus();
        }
    });
    connect(m_feedTimer, &QTimer::timeout, this, &MainWindow::refreshFeeds);
    m_feedTimer->start();
    m_outputNameLabel = outputNameLabel;
}

void MainWindow::populateFromConfig() {
    m_populating = true;
    const auto done = qScopeGuard([this] {
        m_populating = false;
        if (m_outputNameLabel) {
            m_outputNameLabel->setText(m_config.outputFilename());
        }
    });
    m_idEdit->setText(m_config.value(QStringLiteral("ID")));
    m_dirEdit->setText(m_config.value(QStringLiteral("DIR")));
    m_logDirEdit->setText(m_config.value(QStringLiteral("LOGDIR")));
    m_playFileEdit->setText(m_config.value(QStringLiteral("VRECORD_PLAY_FILE")));
    m_technicianEdit->setText(m_config.value(QStringLiteral("TECHNICIAN")));
    m_durationHoursEdit->setText(m_config.value(QStringLiteral("DURATION_HOURS")));
    m_durationMinutesEdit->setText(m_config.value(QStringLiteral("DURATION_MINUTES")));
    m_durationSecondsEdit->setText(m_config.value(QStringLiteral("DURATION_SECONDS")));
    m_prefixEdit->setText(m_config.value(QStringLiteral("PREFIX")));
    m_suffixEdit->setText(m_config.value(QStringLiteral("USER_SUFFIX")));
    m_enableSuffixCheck->setChecked(m_config.value(QStringLiteral("YES_SUFFIX")) == QStringLiteral("true"));
    setRadioValue(m_config.value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0")));
    setChoice(m_videoInputCombo, m_config.value(QStringLiteral("VIDEO_INPUT_CHOICE")), QStringLiteral("SDI"));
    setChoice(m_audioInputCombo, m_config.value(QStringLiteral("AUDIO_INPUT_CHOICE")), QStringLiteral("SDI Embedded Audio"));
    setChoice(m_audioMappingCombo, m_config.value(QStringLiteral("AUDIO_MAPPING_CHOICE")), QStringLiteral("1 Stereo Track (From Channels 1 & 2)"));
    setChoice(m_standardCombo, m_config.value(QStringLiteral("STANDARD_CHOICE")), QStringLiteral("NTSC"));
    setChoice(m_embedLogsCombo, m_config.value(QStringLiteral("EMBED_LOGS_CHOICE")), QStringLiteral("Yes"));
    setChoice(m_containerCombo, m_config.value(QStringLiteral("CONTAINER_CHOICE")), QStringLiteral("Matroska"));
    setChoice(m_videoCodecCombo, m_config.value(QStringLiteral("VIDEO_CODEC_CHOICE")), QStringLiteral("FFV1 version 3"));
    setChoice(m_audioCodecCombo, m_config.value(QStringLiteral("AUDIO_CODEC_CHOICE")), QStringLiteral("24-bit PCM"));
    setChoice(m_playbackViewCombo, m_config.value(QStringLiteral("PLAYBACKVIEW_CHOICE")), QStringLiteral("Visual"));
    setChoice(m_playbackPassCombo, m_config.value(QStringLiteral("PLAYBACKVIEW_CHOICE_PASS")), QStringLiteral("Visual"));
    setChoice(m_bitDepthCombo, m_config.value(QStringLiteral("VIDEO_BIT_DEPTH_CHOICE")), QStringLiteral("10 bit"));
    setChoice(m_aspectRatioCombo, m_config.value(QStringLiteral("ASPECT_RATIO_CHOICE")), QStringLiteral("4/3"));
    m_enforceStandardCheck->setChecked(m_config.value(QStringLiteral("STANDARD_ENFORCE_CHOICE")) == QStringLiteral("true"));
    m_enforceAspectCheck->setChecked(m_config.value(QStringLiteral("ASPECT_ENFORCE_CHOICE")) == QStringLiteral("true"));
    setChoice(m_audioBitDepthCombo, m_config.value(QStringLiteral("AUDIO_BIT_DEPTH_CHOICE")), QStringLiteral("24-bit"));
    setComboValue(m_dvDeviceCombo, m_config.value(QStringLiteral("DVRESCUE_INPUT_CHOICE"), QStringLiteral("auto")));
    setChoice(m_dvContainerCombo, m_config.value(QStringLiteral("DV_CONTAINER_CHOICE")), QStringLiteral("DV"));
    setComboValue(m_audioDeviceCombo, m_config.value(QStringLiteral("AUDIO_DEV_CHOICE"), QStringLiteral("default")));
    setChoice(m_audioModeCodecCombo, m_config.value(QStringLiteral("AUDIO_MODE_CODEC_CHOICE")), QStringLiteral("24-bit PCM"));
    setChoice(m_audioChannelsCombo, m_config.value(QStringLiteral("AUDIO_CHANNEL_CHOICE")), QStringLiteral("Stereo"));
    setChoice(m_audioSampleRateCombo, m_config.value(QStringLiteral("AUDIO_MODE_SR_CHOICE")), QStringLiteral("48 kHz"));
    setChoice(m_waveformScaleCombo, m_config.value(QStringLiteral("WAVEFORM_SCALE_CHOICE")), QStringLiteral("digital"));
    setChoice(m_signalViewCombo, m_config.value(QStringLiteral("SIGNAL_VIEW_CHOICE")), QStringLiteral("Broadcast Range"));
    setChoice(m_signalIntCombo, m_config.value(QStringLiteral("SIGNAL_INT_CHOICE")), QStringLiteral("auto"));
    setChoice(m_monitorAudioCombo, m_config.value(QStringLiteral("MONITOR_AUDIO_CHOICE")), QStringLiteral("Yes"));
    setChoice(m_captureChannelsCombo, m_config.value(QStringLiteral("DECKLINK_AUDIO_CHANNELS_CHOICE")), QStringLiteral("8"));
    setChoice(m_monitorPairCombo, m_config.value(QStringLiteral("MONITOR_PAIR_CHOICE")), QStringLiteral("1/2"));
    m_previewMeter->setMonitorPair(m_monitorPairCombo->currentIndex());
    m_ffplayMonitorCheck->setChecked(m_config.value(QStringLiteral("FFPLAY_MONITOR_CHOICE"), QStringLiteral("true")) != QStringLiteral("false"));
    setComboValue(m_ffplayAspectCombo, m_config.value(QStringLiteral("FFPLAY_VIEW_ASPECT_CHOICE"), QStringLiteral("16:9")));
    setChoice(m_ntsc43SarCombo, m_config.value(QStringLiteral("NTSC_43_SAR_CHOICE")), QStringLiteral("10/11"));
    setChoice(m_ntsc169SarCombo, m_config.value(QStringLiteral("NTSC_169_SAR_CHOICE")), QStringLiteral("40/33"));
    setChoice(m_pal43SarCombo, m_config.value(QStringLiteral("PAL_43_SAR_CHOICE")), QStringLiteral("12/11"));
    setChoice(m_pal169SarCombo, m_config.value(QStringLiteral("PAL_169_SAR_CHOICE")), QStringLiteral("16/11"));
    setChoice(m_themeCombo, m_config.value(QStringLiteral("THEME_CHOICE")), QStringLiteral("Dark"));
    setComboValue(m_videoOutputDeviceCombo, m_config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")));
    setComboValue(m_videoOutputConnectionCombo, m_config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), QStringLiteral("Auto")));
    setComboValue(m_relayDeviceCombo, m_config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")));
    setComboValue(m_testPatternCombo, m_config.value(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), QStringLiteral("SMPTE SD Bars (75%)")));
    setComboValue(m_testAudioCombo, m_config.value(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE"), QStringLiteral("1 kHz Tone")));
    m_mp4Check->setChecked(m_config.value(QStringLiteral("MP4_CHOICE")) == QStringLiteral("true"));
    m_frameMd5Check->setChecked(m_config.value(QStringLiteral("FRAMEMD5_CHOICE")) == QStringLiteral("true"));
    m_timecodeVttCheck->setChecked(m_config.value(QStringLiteral("TIMECODEVTT_CHOICE")) == QStringLiteral("true"));
    m_qcToolsCheck->setChecked(m_config.value(QStringLiteral("QCTOOLSXML_V2_CHOICE")) == QStringLiteral("true"));
    m_dvOptionSCheck->setChecked(m_config.value(QStringLiteral("DV_RESCUE_OPTION_S")) == QStringLiteral("true"));
    m_dvOptionDCheck->setChecked(m_config.value(QStringLiteral("DV_RESCUE_OPTION_D")) == QStringLiteral("true"));
    m_dvOptionTCheck->setChecked(m_config.value(QStringLiteral("DV_RESCUE_OPTION_T")) == QStringLiteral("true"));
    m_dvOptionTcCheck->setChecked(m_config.value(QStringLiteral("DV_RESCUE_OPTION_TC")) == QStringLiteral("true"));
    m_ccVttCheck->setChecked(m_config.value(QStringLiteral("CC2VTT")) == QStringLiteral("true"));
    m_ccSrtCheck->setChecked(m_config.value(QStringLiteral("CC2SRT")) == QStringLiteral("true"));
    m_capToMp4Check->setChecked(m_config.value(QStringLiteral("CAP2MP4")) == QStringLiteral("true"));
    m_colorSafeCheck->setChecked(m_config.value(QStringLiteral("COLOR_SAFE")) == QStringLiteral("true"));
    m_invertPhase2Check->setChecked(m_config.value(QStringLiteral("INVERT_PHASE_2")) == QStringLiteral("true"));
    m_invertPhase4Check->setChecked(m_config.value(QStringLiteral("INVERT_PHASE_4")) == QStringLiteral("true"));
    m_previewRelayCheck->setChecked(m_config.value(QStringLiteral("PREVIEW_RELAY_CHOICE"), QStringLiteral("true")) == QStringLiteral("true"));
    m_filePlayAutoCheck->setChecked(m_config.value(QStringLiteral("FILEPLAY_AUTO_CHOICE"), QStringLiteral("true")) == QStringLiteral("true"));
    setChoice(m_outputStandardCombo, m_config.value(QStringLiteral("OUTPUT_STANDARD_CHOICE")), QStringLiteral("Auto (match file)"));
    m_testSignalOutputCheck->setChecked(m_config.value(QStringLiteral("TEST_SIGNAL_OUTPUT_CHOICE")) == QStringLiteral("true"));
    m_mediaIdTypeEdit->setText(m_config.value(QStringLiteral("MEDIA_ID_TYPE")));
    m_webVttExtraEdit->setText(m_config.value(QStringLiteral("WEBVTT_EXTRA")));
}

void MainWindow::syncConfigFromUi() {
    m_config.setValue(QStringLiteral("ID"), m_idEdit->text().trimmed());
    m_config.setValue(QStringLiteral("DIR"), m_dirEdit->text().trimmed());
    m_config.setValue(QStringLiteral("LOGDIR"), m_logDirEdit->text().trimmed());
    m_config.setValue(QStringLiteral("VRECORD_PLAY_FILE"), m_playFileEdit->text().trimmed());
    m_config.setValue(QStringLiteral("TECHNICIAN"), m_technicianEdit->text().trimmed());
    m_config.setValue(QStringLiteral("DURATION_HOURS"), m_durationHoursEdit->text().trimmed());
    m_config.setValue(QStringLiteral("DURATION_MINUTES"), m_durationMinutesEdit->text().trimmed());
    m_config.setValue(QStringLiteral("DURATION_SECONDS"), m_durationSecondsEdit->text().trimmed());
    m_config.setValue(QStringLiteral("PREFIX"), m_prefixEdit->text().trimmed());
    m_config.setValue(QStringLiteral("USER_SUFFIX"), m_suffixEdit->text().trimmed());
    m_config.setValue(QStringLiteral("YES_SUFFIX"), m_enableSuffixCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DEVICE_INPUT_CHOICE"), currentRadioValue());
    m_config.setValue(QStringLiteral("DECKLINK_INPUT_CHOICE"), m_deckLinkCombo->currentData().toString());
    m_config.setValue(QStringLiteral("VIDEO_INPUT_CHOICE"), m_videoInputCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_INPUT_CHOICE"), m_audioInputCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MAPPING_CHOICE"), m_audioMappingCombo->currentText());
    m_config.setValue(QStringLiteral("STANDARD_CHOICE"), m_standardCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), m_videoOutputConnectionCombo->currentText());
    m_config.setValue(QStringLiteral("EMBED_LOGS_CHOICE"), m_embedLogsCombo->currentText());
    m_config.setValue(QStringLiteral("CONTAINER_CHOICE"), choiceValue(m_containerCombo));
    m_config.setValue(QStringLiteral("VIDEO_CODEC_CHOICE"), m_videoCodecCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_CODEC_CHOICE"), choiceValue(m_audioCodecCombo));
    m_config.setValue(QStringLiteral("AUDIO_BIT_DEPTH_CHOICE"), m_audioBitDepthCombo->currentText());
    m_config.setValue(QStringLiteral("PLAYBACKVIEW_CHOICE"), m_playbackViewCombo->currentText());
    m_config.setValue(QStringLiteral("PLAYBACKVIEW_CHOICE_PASS"), m_playbackPassCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_BIT_DEPTH_CHOICE"), m_bitDepthCombo->currentText());
    m_config.setValue(QStringLiteral("ASPECT_RATIO_CHOICE"), m_aspectRatioCombo->currentText());
    m_config.setValue(QStringLiteral("STANDARD_ENFORCE_CHOICE"), m_enforceStandardCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("ASPECT_ENFORCE_CHOICE"), m_enforceAspectCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DVRESCUE_INPUT_CHOICE"), m_dvDeviceCombo->currentText());
    m_config.setValue(QStringLiteral("DV_CONTAINER_CHOICE"), choiceValue(m_dvContainerCombo));
    m_config.setValue(QStringLiteral("AUDIO_DEV_CHOICE"), m_audioDeviceCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MODE_CODEC_CHOICE"), choiceValue(m_audioModeCodecCombo));
    m_config.setValue(QStringLiteral("AUDIO_CHANNEL_CHOICE"), m_audioChannelsCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MODE_SR_CHOICE"), m_audioSampleRateCombo->currentText());
    m_config.setValue(QStringLiteral("WAVEFORM_SCALE_CHOICE"), m_waveformScaleCombo->currentText());
    m_config.setValue(QStringLiteral("SIGNAL_VIEW_CHOICE"), m_signalViewCombo->currentText());
    m_config.setValue(QStringLiteral("SIGNAL_INT_CHOICE"), m_signalIntCombo->currentText());
    m_config.setValue(QStringLiteral("MONITOR_AUDIO_CHOICE"), m_monitorAudioCombo->currentText());
    m_config.setValue(QStringLiteral("DECKLINK_AUDIO_CHANNELS_CHOICE"), m_captureChannelsCombo->currentText());
    m_config.setValue(QStringLiteral("MONITOR_PAIR_CHOICE"), m_monitorPairCombo->currentText());
    m_config.setValue(QStringLiteral("FFPLAY_MONITOR_CHOICE"), m_ffplayMonitorCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("FFPLAY_VIEW_ASPECT_CHOICE"), m_ffplayAspectCombo->currentText());
    m_config.setValue(QStringLiteral("NTSC_43_SAR_CHOICE"), m_ntsc43SarCombo->currentText());
    m_config.setValue(QStringLiteral("NTSC_169_SAR_CHOICE"), m_ntsc169SarCombo->currentText());
    m_config.setValue(QStringLiteral("PAL_43_SAR_CHOICE"), m_pal43SarCombo->currentText());
    m_config.setValue(QStringLiteral("PAL_169_SAR_CHOICE"), m_pal169SarCombo->currentText());
    m_config.setValue(QStringLiteral("THEME_CHOICE"), m_themeCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE"), m_videoOutputDeviceCombo->currentData().toString());
    m_config.setValue(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE"), m_relayDeviceCombo->currentData().toString());
    m_config.setValue(QStringLiteral("TEST_SIGNAL_PATTERN_CHOICE"), m_testPatternCombo->currentText());
    m_config.setValue(QStringLiteral("TEST_SIGNAL_AUDIO_CHOICE"), m_testAudioCombo->currentText());
    m_config.setValue(QStringLiteral("MP4_CHOICE"), m_mp4Check->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("FRAMEMD5_CHOICE"), m_frameMd5Check->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("TIMECODEVTT_CHOICE"), m_timecodeVttCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("QCTOOLSXML_V2_CHOICE"), m_qcToolsCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DV_RESCUE_OPTION_S"), m_dvOptionSCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DV_RESCUE_OPTION_D"), m_dvOptionDCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DV_RESCUE_OPTION_T"), m_dvOptionTCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("DV_RESCUE_OPTION_TC"), m_dvOptionTcCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("CC2VTT"), m_ccVttCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("CC2SRT"), m_ccSrtCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("CAP2MP4"), m_capToMp4Check->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("COLOR_SAFE"), m_colorSafeCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("INVERT_PHASE_2"), m_invertPhase2Check->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("INVERT_PHASE_4"), m_invertPhase4Check->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("PREVIEW_RELAY_CHOICE"), m_previewRelayCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("OUTPUT_STANDARD_CHOICE"), m_outputStandardCombo->currentText());
    // the shell's own file playback reads FILEPLAY_AUTO_CHOICE: Auto = native
    m_config.setValue(QStringLiteral("FILEPLAY_AUTO_CHOICE"), m_outputStandardCombo->currentIndex() == 0 ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("TEST_SIGNAL_OUTPUT_CHOICE"), m_testSignalOutputCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("MEDIA_ID_TYPE"), m_mediaIdTypeEdit->text().trimmed());
    m_config.setValue(QStringLiteral("WEBVTT_EXTRA"), m_webVttExtraEdit->text().trimmed());
}

void MainWindow::populateDeckLinkCombo(const QVector<DeckLinkDeviceInfo>& devices, const QString& selectedValue) {
    m_deckLinkCombo->clear();
    m_videoOutputDeviceCombo->clear();
    m_relayDeviceCombo->clear();
    for (const auto& device : devices) {
        // the value is the name ffmpeg's decklink device opens (display name)
        m_deckLinkCombo->addItem(device.displayName, device.ffmpegName);
        m_videoOutputDeviceCombo->addItem(device.displayName, device.ffmpegName);
        m_relayDeviceCombo->addItem(device.displayName, device.ffmpegName);
    }
    // configs from older builds stored the model name
    const auto findDevice = [&devices](QComboBox* combo, const QString& value) {
        int index = combo->findData(value);
        if (index < 0) {
            for (int i = 0; i < devices.size(); ++i) {
                if (devices.at(i).modelName == value) {
                    index = i;
                    break;
                }
            }
        }
        return index;
    };
    const int idx = findDevice(m_deckLinkCombo, selectedValue);
    if (idx >= 0) {
        m_deckLinkCombo->setCurrentIndex(idx);
    } else if (!devices.isEmpty()) {
        m_deckLinkCombo->setCurrentIndex(0);
    }
    const int outputIdx = findDevice(m_videoOutputDeviceCombo, m_config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")));
    if (outputIdx >= 0) {
        m_videoOutputDeviceCombo->setCurrentIndex(outputIdx);
    } else if (!devices.isEmpty()) {
        m_videoOutputDeviceCombo->setCurrentIndex(0);
    }
    const int relayIdx = findDevice(m_relayDeviceCombo, m_config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")));
    if (relayIdx >= 0) {
        m_relayDeviceCombo->setCurrentIndex(relayIdx);
    } else if (!devices.isEmpty()) {
        m_relayDeviceCombo->setCurrentIndex(0);
    }
    rebuildConnectorCombos();
}

void MainWindow::rebuildConnectorCombos() {
    // populate the input/output connector selectors from the selected
    // device's real hardware IO (BMDDeckLinkVideoInput/OutputConnections)
    const QString selectedDevice = m_deckLinkCombo->currentData().toString();
    qint64 inputMask = 0;
    qint64 outputMask = 0;
    for (const auto& device : m_devices) {
        if (device.ffmpegName == selectedDevice) {
            inputMask = device.videoInputConnections;
            outputMask = device.videoOutputConnections;
            break;
        }
    }

    // input connectors the hardware has, in the shell's spelling (optical
    // SDI is selected through the plain SDI input there)
    const QString currentInput = m_videoInputCombo->currentText().isEmpty()
        ? m_config.value(QStringLiteral("VIDEO_INPUT_CHOICE"))
        : m_videoInputCombo->currentText();
    m_videoInputCombo->clear();
    QStringList inputNames;
    for (const QString& name : DeckLinkDiscovery::connectionNamesFromMask(inputMask)) {
        const QString shellName = name == QStringLiteral("Optical SDI") ? QStringLiteral("SDI") : name;
        if (VrecordOptions::videoInputs().contains(shellName) && !inputNames.contains(shellName)) {
            inputNames << shellName;
        }
    }
    if (inputNames.isEmpty()) {
        inputNames = VrecordOptions::videoInputs();
    }
    m_videoInputCombo->addItems(inputNames);
    const int inputIndex = m_videoInputCombo->findText(currentInput);
    m_videoInputCombo->setCurrentIndex(inputIndex >= 0 ? inputIndex : 0);

    // output connectors: Auto always allowed, hardware list otherwise
    const QString currentOutput = m_videoOutputConnectionCombo->count() > 0 && !m_populating
        ? m_videoOutputConnectionCombo->currentText()
        : m_config.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), QStringLiteral("Auto"));
    m_videoOutputConnectionCombo->clear();
    m_videoOutputConnectionCombo->addItem(QStringLiteral("Auto"));
    const QStringList outputNames = DeckLinkDiscovery::connectionNamesFromMask(outputMask);
    for (const QString& name : outputNames) {
        m_videoOutputConnectionCombo->addItem(name);
    }
    const int outputIndex = m_videoOutputConnectionCombo->findText(currentOutput);
    m_videoOutputConnectionCombo->setCurrentIndex(outputIndex >= 0 ? outputIndex : 0);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_backend.isShellRunning()) {
        // the session is a child of this window: closing it would cut the
        // recording off mid-file
        const auto answer = QMessageBox::question(this, QStringLiteral("vrecord Qt6"),
                                                  QStringLiteral("A %1 session is still running. Stop it and quit?").arg(m_backend.shellMode()));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_backend.stopShell();
    }
    // persist the current selections (recording/auxiliary directories, playout
    // file, connector choices) so they are waiting at the next launch
    syncConfigFromUi();
    m_backend.saveConfig(m_config);
    m_backend.stopVideoPreview();
    QString ignored;
    m_backend.stopAllOutputs(m_config, &ignored);
    // a verified update installs once this process has exited (the helper
    // waits for it); it restarts vrecord only for "Install and restart"
    if (m_updates && m_updates->isReady() && (m_installUpdateOnExit || m_restartAfterUpdate)) {
        QString updateError;
        if (!m_updates->launchInstall(QCoreApplication::applicationPid(), m_restartAfterUpdate, &updateError)) {
            QMessageBox::warning(this, QStringLiteral("vrecord update"), updateError);
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::refreshDeckLinkDevices() {
    QString errorMessage;
    m_devices = DeckLinkDiscovery::enumerateDevices(&errorMessage);
    populateDeckLinkCombo(m_devices, m_config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")));
    if (!errorMessage.isEmpty() && m_devices.isEmpty()) {
        showError(errorMessage);
    }
    updateDerivedState();
}

void MainWindow::saveConfig() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.saveConfig(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Settings saved"), 3000);
}

// The shell captures with -format_code from the Standard choice: a deck sending
// a different mode is captured as blank frames ("No input signal"). The
// Standard therefore follows what the card has locked - here right before a
// session starts, and continuously while the live preview runs.
bool MainWindow::confirmInputStandard() {
    if (BackendController::testMode() || m_config.value(QStringLiteral("DEVICE_INPUT_CHOICE"), QStringLiteral("0")) != QStringLiteral("0")) {
        return true;
    }
    const QString device = m_config.value(QStringLiteral("DECKLINK_INPUT_CHOICE")).trimmed();
    if (!device.isEmpty()) {
        followInputMode(DeckLinkDiscovery::detectInputFormatCode(device, nullptr, true));
    }
    return true;
}

void MainWindow::followInputMode(const QString& formatCode) {
    if (formatCode.isEmpty() || m_backend.isShellRunning()) {
        return;
    }
    QString match;
    for (const QString& standard : VrecordOptions::standards()) {
        const QString code = standard == QStringLiteral("NTSC") ? QStringLiteral("ntsc")
                           : standard == QStringLiteral("PAL")  ? QStringLiteral("pal")
                                                                : standard.left(4);
        if (code == formatCode) {
            match = standard;
            break;
        }
    }
    if (match.isEmpty()) {
        return;
    }
    if (m_enforceStandardCheck->isChecked()) {
        // keep the chosen Standard; say once per input change that they differ
        if (match != m_standardCombo->currentText() && match != m_enforcedInputNotice) {
            m_enforcedInputNotice = match;
            appendActivity(QStringLiteral("Input is %1, but the Standard is enforced as %2 (untick \"Enforce manual selection\" to follow the input)")
                               .arg(match, m_standardCombo->currentText()));
            statusBar()->showMessage(QStringLiteral("Input is %1 - Standard enforced as %2").arg(match, m_standardCombo->currentText()), 8000);
        }
        return;
    }
    m_enforcedInputNotice.clear();
    if (match == m_standardCombo->currentText()) {
        return;
    }
    const QString previous = m_standardCombo->currentText();
    m_standardCombo->setCurrentText(match);
    updateDerivedState();
    appendActivity(QStringLiteral("Standard set to %1 to match the input (was %2)").arg(match, previous));
    statusBar()->showMessage(QStringLiteral("Standard follows the input: %1").arg(match), 6000);
}

// WSS on a PAL input (BackendController::readWss): follow it unless enforced
void MainWindow::onInputAspectDetected(const QString& aspect, const QString& detail) {
    if (m_backend.isShellRunning() || aspect == m_aspectRatioCombo->currentText()
        || VrecordOptions::isHdStandard(m_standardCombo->currentText())) {
        return;
    }
    if (m_enforceAspectCheck->isChecked()) {
        appendActivity(QStringLiteral("Input signals %1 (%2), but the aspect ratio is enforced as %3")
                           .arg(aspect, detail, m_aspectRatioCombo->currentText()));
        return;
    }
    const QString previous = m_aspectRatioCombo->currentText();
    setChoice(m_aspectRatioCombo, aspect, previous);
    appendActivity(QStringLiteral("Display aspect ratio set to %1 from the input (%2; was %3)").arg(aspect, detail, previous));
    statusBar()->showMessage(QStringLiteral("Aspect ratio follows the input: %1 (%2)").arg(aspect, detail), 6000);
    updateDerivedState();
    // the preview pane is shaped by the aspect: relaunch it
    if (m_backend.isVideoPreviewActive()) {
        m_backend.stopVideoPreview();
        startPreviewQuietly();
    }
}

void MainWindow::autoAspectForStandard() {
    if (m_enforceAspectCheck->isChecked() || !VrecordOptions::isHdStandard(m_standardCombo->currentText())
        || m_aspectRatioCombo->currentText() == QStringLiteral("16/9")) {
        return;
    }
    const QString previous = m_aspectRatioCombo->currentText();
    setChoice(m_aspectRatioCombo, QStringLiteral("16/9"), previous);
    appendActivity(QStringLiteral("Display aspect ratio set to 16/9 for %1 (was %2)").arg(m_standardCombo->currentText(), previous));
}

void MainWindow::applyAudioConstraints() {
    // Opus muxes only into Matroska / MP4
    const QString container = choiceValue(m_containerCombo);
    for (int i = 0; i < m_audioCodecCombo->count(); ++i) {
        setComboItemEnabled(m_audioCodecCombo, i, VrecordOptions::audioCodecFitsContainer(m_audioCodecCombo->itemData(i).toString(), container));
    }
    if (!VrecordOptions::audioCodecFitsContainer(choiceValue(m_audioCodecCombo), container)) {
        const QString previous = m_audioCodecCombo->currentText();
        setChoice(m_audioCodecCombo, QStringLiteral("24-bit PCM"), QStringLiteral("24-bit PCM"));
        appendActivity(QStringLiteral("%1 audio cannot be recorded to %2: audio codec set to %3")
                           .arg(previous, m_containerCombo->currentText(), m_audioCodecCombo->currentText()));
    }
    m_audioBitDepthCombo->setEnabled(VrecordOptions::audioCodecHasBitDepth(choiceValue(m_audioCodecCombo)));
    m_audioModeBitDepthCombo->setEnabled(VrecordOptions::audioCodecHasBitDepth(choiceValue(m_audioModeCodecCombo)));

    // 2 capture channels: no mapping that reads channels 3 / 4
    const QString channels = m_captureChannelsCombo->currentText();
    const bool twoChannels = channels == QStringLiteral("2");
    for (int i = 0; i < m_audioMappingCombo->count(); ++i) {
        setComboItemEnabled(m_audioMappingCombo, i, !twoChannels || !VrecordOptions::mappingNeedsFourChannels(m_audioMappingCombo->itemText(i)));
    }
    if (twoChannels && VrecordOptions::mappingNeedsFourChannels(m_audioMappingCombo->currentText())) {
        setChoice(m_audioMappingCombo, QStringLiteral("1 Stereo Track (From Channels 1 & 2)"), QStringLiteral("1 Stereo Track (From Channels 1 & 2)"));
        appendActivity(QStringLiteral("2 capture channels: audio channel mapping set to %1").arg(m_audioMappingCombo->currentText()));
    }
    // monitored pair within the captured channels
    if (m_monitorPairCombo) {
        const int used = channels == QStringLiteral("4") ? 4 : qMax(2, channels.toInt());
        for (int i = 0; i < m_monitorPairCombo->count(); ++i) {
            setComboItemEnabled(m_monitorPairCombo, i, (i + 1) * 2 <= used);
        }
        if ((m_monitorPairCombo->currentIndex() + 1) * 2 > used) {
            m_monitorPairCombo->setCurrentIndex(0);
            m_previewMeter->setMonitorPair(0);
        }
    }
}

void MainWindow::startRecord() {
    if (!BackendController::testMode() && m_backend.isShellRunning()) {
        // the button doubles as Stop while a session runs
        m_backend.stopShell();
        return;
    }
    syncConfigFromUi();
    if (!confirmInputStandard()) {
        return;
    }
    QString errorMessage;
    // the backend releases the card (preview, playout, test signal) and runs
    // `vrecord -r`: ffmpeg encodes and ffplay opens as the monitor, exactly
    // as the GTK frontend does
    if (!m_backend.startRecord(m_config, &errorMessage)) {
        showError(errorMessage);
        startPreviewQuietly();
        return;
    }
    if (m_testSignalOutputCheck->isChecked()) {
        m_testSignalOutputCheck->setChecked(false);
    }
    statusBar()->showMessage(QStringLiteral("Recording %1 - close the player window to stop").arg(m_config.outputFilename()), 8000);
}

void MainWindow::startPassthrough() {
    if (!BackendController::testMode() && m_backend.isShellRunning()) {
        m_backend.stopShell();
        return;
    }
    syncConfigFromUi();
    if (!confirmInputStandard()) {
        return;
    }
    QString errorMessage;
    if (!m_backend.startPassthrough(m_config, &errorMessage)) {
        showError(errorMessage);
        startPreviewQuietly();
    }
}

void MainWindow::startAudioCheck() {
    if (!BackendController::testMode() && m_backend.isShellRunning()) {
        m_backend.stopShell();
        return;
    }
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startAudioCheck(m_config, &errorMessage)) {
        showError(errorMessage);
        startPreviewQuietly();
    }
}
void MainWindow::startFileOutput() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startFileOutput(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    updatePreviewButton();
    statusBar()->showMessage(QStringLiteral("Playing %1 to the hardware output (%2)")
                                 .arg(m_playFileEdit->text().trimmed(), m_backend.fileOutputDescription()), 5000);
}

void MainWindow::stopAllOutputs() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.stopAllOutputs(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    if (m_testSignalOutputCheck->isChecked()) {
        m_testSignalOutputCheck->setChecked(false);
    }
    m_outputView->clearFrame(QStringLiteral("No hardware output running"));
    statusBar()->showMessage(QStringLiteral("Stopped all hardware output"), 3000);
}

void MainWindow::setTestSignalOutput() {
    syncConfigFromUi();
    QString errorMessage;
    const bool enabled = m_testSignalOutputCheck->isChecked();
    if (!m_backend.setTestSignalOutput(m_config, enabled, &errorMessage)) {
        m_testSignalOutputCheck->blockSignals(true);
        m_testSignalOutputCheck->setChecked(!enabled);
        m_testSignalOutputCheck->blockSignals(false);
        syncConfigFromUi();
        showError(errorMessage);
        return;
    }
    if (!enabled) {
        m_outputView->clearFrame(QStringLiteral("No hardware output running"));
    }
    updatePreviewButton();
    statusBar()->showMessage(enabled
                                 ? QStringLiteral("Starting test signal output...")
                                 : QStringLiteral("Stopped test signal output"),
                             3000);
}

void MainWindow::togglePreview() {
    if (m_backend.isVideoPreviewActive()) {
        m_backend.stopVideoPreview();
        m_inputView->clearFrame(QStringLiteral("Live preview stopped"));
        updatePreviewButton();
        statusBar()->showMessage(QStringLiteral("Live preview stopped"), 3000);
        return;
    }
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startVideoPreview(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    // the preview takes the card: the output side is now idle
    if (m_testSignalOutputCheck->isChecked()) {
        m_testSignalOutputCheck->blockSignals(true);
        m_testSignalOutputCheck->setChecked(false);
        m_testSignalOutputCheck->blockSignals(false);
    }
    m_outputView->clearFrame(QStringLiteral("No hardware output running"));
    updatePreviewButton();
    statusBar()->showMessage(QStringLiteral("Live input preview starting"), 3000);
}

void MainWindow::togglePlayPause() {
    if (!m_backend.isFileOutputActive()) {
        startFileOutput();
        return;
    }
    if (!m_backend.togglePauseFileOutput()) {
        showError(QStringLiteral("Could not toggle the playout pause state."));
        return;
    }
    updateTransportUi();
}

void MainWindow::stepPlayback(int frames) {
    QString errorMessage;
    if (!m_backend.stepFileOutput(frames, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    updateTransportUi();
}

// playing keeps playing from there; paused stays paused on that frame (card
// and pane); cued / halted moves the cue
void MainWindow::jumpToFrame(qint64 frame) {
    const double fps = m_backend.fileOutputFps() > 0 ? m_backend.fileOutputFps() : 25.0;
    QString errorMessage;
    if (!m_backend.seekFileOutput(double(qMax<qint64>(frame, 0)) / fps, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    updateTransportUi();
}

void MainWindow::seekToSlider() {
    m_sliderDragging = false;
    const double duration = m_backend.fileOutputDurationSeconds();
    if (duration <= 0) {
        return;
    }
    QString errorMessage;
    if (!m_backend.seekFileOutput(m_seekSlider->value() / 10000.0 * duration, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    updateTransportUi();
}

void MainWindow::refreshFeeds() {
    // frames are pushed by the feeds; this tick only drives the transport
    // readout and the button states
    updateTransportUi();
    if (!m_backend.isVideoPreviewActive() && m_previewToggleButton->text() == QStringLiteral("Stop Preview")) {
        updatePreviewButton();
    }
}

void MainWindow::updateTransportUi() {
    const bool active = m_backend.isFileOutputActive();
    const bool paused = m_backend.isFileOutputPaused();
    m_playPauseButton->setIcon(QApplication::style()->standardIcon(active && !paused ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    const double duration = m_backend.fileOutputDurationSeconds();
    const double position = m_backend.fileOutputPositionSeconds();
    const double fps = m_backend.fileOutputFps() > 0 ? m_backend.fileOutputFps() : 25.0;
    const qint64 framesPerSecond = qRound64(fps);
    const qint64 totalFrames = qRound64(position * fps);
    const qint64 secondsTotal = framesPerSecond > 0 ? totalFrames / framesPerSecond : 0;
    const qint64 framePart = framesPerSecond > 0 ? totalFrames % framesPerSecond : 0;
    const auto formatHms = [](qint64 total) {
        return QStringLiteral("%1:%2:%3").arg(total / 3600, 2, 10, QLatin1Char('0'))
            .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(total % 60, 2, 10, QLatin1Char('0'));
    };
    const QString tc = QStringLiteral("%1:%2").arg(formatHms(secondsTotal)).arg(framePart, 2, 10, QLatin1Char('0'));
    const qint64 durationFrames = qRound64(duration * fps);
    const QString durationTc = QStringLiteral("%1:%2").arg(formatHms(framesPerSecond > 0 ? durationFrames / framesPerSecond : 0))
                                   .arg(framesPerSecond > 0 ? durationFrames % framesPerSecond : 0, 2, 10, QLatin1Char('0'));
    // don't overwrite what the user is typing
    if (!m_timecodeEdit->hasFocus()) {
        m_timecodeEdit->setText(tc);
    }
    m_positionLabel->setText(QStringLiteral("/ %1").arg(durationTc));
    if (!m_frameSpin->hasFocus()) {
        m_frameSpin->blockSignals(true);
        m_frameSpin->setRange(0, int(qMax<qint64>(0, durationFrames > 0 ? durationFrames - 1 : totalFrames)));
        m_frameSpin->setValue(int(totalFrames));
        m_frameSpin->blockSignals(false);
    }
    const bool haveFile = duration > 0;
    m_timecodeEdit->setEnabled(haveFile);
    m_frameSpin->setEnabled(haveFile);
    QString state;
    if (active && paused) {
        state = QStringLiteral("PAUSED");
    } else if (active) {
        state = QStringLiteral("PLAYING");
    } else if (m_backend.isFileOutputCued()) {
        state = m_backend.isPreviewRelaying() ? QStringLiteral("HALTED - RELAY ON OUTPUT") : QStringLiteral("CUED");
    } else {
        state = QStringLiteral("STOPPED");
    }
    m_transportStateLabel->setText(state);
    m_transportStateLabel->setToolTip(active && !paused ? QString() : QStringLiteral("Press Play to continue from this frame"));
    m_seekSlider->setEnabled(duration > 0);
    if (!m_sliderDragging && duration > 0) {
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(qRound(10000.0 * position / duration));
        m_seekSlider->blockSignals(false);
    }
}

void MainWindow::applyVideoOutputConnection() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.applyVideoOutputConnection(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    statusBar()->showMessage(m_videoOutputConnectionCombo->currentText() == QStringLiteral("Auto")
                                 ? QStringLiteral("Video output connector: Auto (SDK selection untouched)")
                                 : QStringLiteral("Video output connector: %1 (SDK verified)").arg(m_videoOutputConnectionCombo->currentText()),
                             3000);
}

QStringList MainWindow::runSelfTest() {
    QStringList results;
    // CI runners have no DeckLink card or driver: with
    // VRECORD_SELFTEST_ALLOW_NO_HARDWARE=1 and no device found, the checks
    // that need a card report SKIP instead of FAIL (without the variable a
    // missing card still fails, as it should on a capture machine)
    bool noHardware = false;
    static const QStringList hardwareChecks{
        QStringLiteral("Refresh Devices populates the card list"),
        // Record refuses without a card ("Select a DeckLink device first.")
        QStringLiteral("Record action runs the backend with -r"),
        QStringLiteral("Play File to Output streams the file to the decklink output"),
        QStringLiteral("Player step control seeks the playout"),
        QStringLiteral("Output connector list matches the device hardware"),
        QStringLiteral("Pause toggle holds the playout"),
        QStringLiteral("Resume returns the playout to playing"),
        QStringLiteral("Stop Output stops tracked outputs"),
        QStringLiteral("Test signal checkbox starts the generator on the selected device"),
        QStringLiteral("Test signal checkbox stop kills the tracked process"),
        QStringLiteral("Video Output connector applies through the SDK")};
    const auto check = [&](const QString& name, bool ok, const QString& detail = QString()) {
        if (!ok && noHardware && hardwareChecks.contains(name)) {
            results << QStringLiteral("SKIP: %1 (no DeckLink device - CI mode)").arg(name);
            selfTestTrace(results.last());
            return;
        }
        results << QStringLiteral("%1: %2%3").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                                 name,
                                                 detail.isEmpty() ? QString() : QStringLiteral(" - %1").arg(detail));
        selfTestTrace(results.last());   // progress: a hang shows where
    };

    // run the whole self-test on a THROWAWAY config in the package root:
    // test saves must never touch the user's real configuration
    const VrecordConfig originalConfig = m_config;
    VrecordConfig testConfig = originalConfig;
    testConfig.configPath = QDir(m_backend.packageRoot()).filePath(QStringLiteral("VrecordQt6-selftest.conf"));
    QFile::remove(testConfig.configPath);
    m_config = testConfig;
    BackendController::setTestMode(true);
    BackendController::clearCaptured();

    refreshDeckLinkDevices();
    noHardware = m_deckLinkCombo->count() == 0 && qEnvironmentVariableIntValue("VRECORD_SELFTEST_ALLOW_NO_HARDWARE") == 1;
    check(QStringLiteral("Refresh Devices populates the card list"),
          m_deckLinkCombo->count() > 0,
          QStringLiteral("%1 devices").arg(m_deckLinkCombo->count()));

    // PAL widescreen signalling reader (aspect auto-detection)
    {
        const Wss::Result wide = Wss::decode(Wss::synthesizeLine(QStringLiteral("1110"), 17.3));
        const Wss::Result full = Wss::decode(Wss::synthesizeLine(QStringLiteral("0001"), 24.0));
        const Wss::Result badParity = Wss::decode(Wss::synthesizeLine(QStringLiteral("1111"), 17.3));
        const Wss::Result flat = Wss::decode(QVector<float>(720, 16.f));
        check(QStringLiteral("WSS reader: 16:9 anamorphic, 4:3, bad parity and no WSS"),
              wide.aspect == Wss::Aspect::Wide && full.aspect == Wss::Aspect::FourThree
                  && badParity.aspect == Wss::Aspect::None && flat.aspect == Wss::Aspect::None,
              QStringLiteral("%1 / %2 / %3 / %4").arg(wide.group, full.group, badParity.group, flat.group));
    }
    // audio constraints: Opus only in MKV / MP4, 2 capture channels drop the 3 & 4 mappings
    {
        setChoice(m_containerCombo, QStringLiteral("QuickTime"), QStringLiteral("QuickTime"));
        setChoice(m_audioCodecCombo, QStringLiteral("Opus"), QStringLiteral("Opus"));
        applyAudioConstraints();
        const bool opusRefused = choiceValue(m_audioCodecCombo) == QStringLiteral("24-bit PCM");
        setChoice(m_containerCombo, QStringLiteral("Matroska"), QStringLiteral("Matroska"));
        setChoice(m_audioCodecCombo, QStringLiteral("Opus"), QStringLiteral("Opus"));
        applyAudioConstraints();
        const bool opusKept = choiceValue(m_audioCodecCombo) == QStringLiteral("Opus") && !m_audioBitDepthCombo->isEnabled();
        setChoice(m_captureChannelsCombo, QStringLiteral("2"), QStringLiteral("2"));
        setChoice(m_audioMappingCombo, VrecordOptions::channelMappings().first(), VrecordOptions::channelMappings().first());
        applyAudioConstraints();
        const bool mappingLimited = !VrecordOptions::mappingNeedsFourChannels(m_audioMappingCombo->currentText());
        check(QStringLiteral("Audio options: Opus only in MKV / MP4, no bit depth for Opus, 2 channels limit the mapping"),
              opusRefused && opusKept && mappingLimited,
              QStringLiteral("refused=%1 kept=%2 mapping=%3").arg(opusRefused).arg(opusKept).arg(mappingLimited));
        // the 16-bit / 4-channel settings must still pass the shell's option review below
        setChoice(m_audioBitDepthCombo, QStringLiteral("16-bit"), QStringLiteral("16-bit"));
        setChoice(m_captureChannelsCombo, QStringLiteral("4"), QStringLiteral("4"));
        updateDerivedState();
    }

    saveConfig();
    check(QStringLiteral("Save Settings writes the config file"), QFileInfo::exists(m_config.configPath), m_config.configPath);

    // the shell's _review_option aborts `vrecord -r` (exit 2, before ffmpeg
    // or ffplay start) on any value outside its option arrays
    {
        const VrecordConfig saved = VrecordConfig::load(QStringLiteral("vrecord"), m_config.configPath);
        const QList<QPair<QString, QStringList>> shellLists{
            {QStringLiteral("VIDEO_INPUT_CHOICE"), VrecordOptions::videoInputs()},
            {QStringLiteral("AUDIO_INPUT_CHOICE"), VrecordOptions::audioInputs()},
            {QStringLiteral("VIDEO_BIT_DEPTH_CHOICE"), VrecordOptions::bitDepths()},
            {QStringLiteral("AUDIO_MAPPING_CHOICE"), VrecordOptions::channelMappings()},
            {QStringLiteral("STANDARD_CHOICE"), VrecordOptions::standards()},
            {QStringLiteral("ASPECT_RATIO_CHOICE"), VrecordOptions::aspectRatios()},
            {QStringLiteral("NTSC_43_SAR_CHOICE"), VrecordOptions::ntsc43Sars()},
            {QStringLiteral("NTSC_169_SAR_CHOICE"), VrecordOptions::ntsc169Sars()},
            {QStringLiteral("PAL_43_SAR_CHOICE"), VrecordOptions::pal43Sars()},
            {QStringLiteral("PAL_169_SAR_CHOICE"), VrecordOptions::pal169Sars()},
            {QStringLiteral("WAVEFORM_SCALE_CHOICE"), VrecordOptions::waveformScales()},
            {QStringLiteral("SIGNAL_VIEW_CHOICE"), VrecordOptions::signalViews()},
            {QStringLiteral("SIGNAL_INT_CHOICE"), VrecordOptions::signalInterlacements()},
            {QStringLiteral("MONITOR_AUDIO_CHOICE"), VrecordOptions::monitorAudio()},
            {QStringLiteral("PLAYBACKVIEW_CHOICE"), VrecordOptions::playbackViews()},
            {QStringLiteral("CONTAINER_CHOICE"), VrecordOptions::containers()},
            {QStringLiteral("VIDEO_CODEC_CHOICE"), VrecordOptions::videoCodecs()},
            {QStringLiteral("AUDIO_CODEC_CHOICE"), VrecordOptions::audioCodecs()},
            {QStringLiteral("AUDIO_BIT_DEPTH_CHOICE"), VrecordOptions::audioBitDepths()},
            {QStringLiteral("EMBED_LOGS_CHOICE"), VrecordOptions::embedLogs()},
        };
        QStringList rejected;
        for (const auto& entry : shellLists) {
            const QString value = saved.value(entry.first);
            if (!entry.second.contains(value)) {
                rejected << QStringLiteral("%1=\"%2\"").arg(entry.first, value);
            }
        }
        const bool hdOk = !VrecordOptions::isHdStandard(saved.value(QStringLiteral("STANDARD_CHOICE")))
            || saved.value(QStringLiteral("HD_CHOICE")) == QStringLiteral("true");
        if (!hdOk) {
            rejected << QStringLiteral("HD_CHOICE not true for an HD standard");
        }
        check(QStringLiteral("Saved settings pass the shell backend's option review"), rejected.isEmpty(), rejected.join(QStringLiteral(", ")));
    }

    m_idEdit->setText(QStringLiteral("selftest"));
    BackendController::clearCaptured();
    startRecord();
    check(QStringLiteral("Record action runs the backend with -r"),
          BackendController::capturedAnyContaining(QStringLiteral(" -r ")));

    BackendController::clearCaptured();
    startPassthrough();
    check(QStringLiteral("Passthrough action runs the backend with -p"),
          BackendController::capturedAnyContaining(QStringLiteral(" -p ")));

    BackendController::clearCaptured();
    startAudioCheck();
    check(QStringLiteral("Audio Check action runs the backend with -a"),
          BackendController::capturedAnyContaining(QStringLiteral(" -a ")));

    const QString tempPlayFile = QDir(m_backend.packageRoot()).filePath(QStringLiteral("selftest-play.mkv"));
    {
        QFile placeholder(tempPlayFile);
        placeholder.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }
    m_playFileEdit->setText(QDir::toNativeSeparators(tempPlayFile));
    BackendController::clearCaptured();
    startFileOutput();
    const QString filePlayCapture = BackendController::capturedCommands().join(QLatin1Char(' '));
    check(QStringLiteral("Play File to Output streams the file to the decklink output"),
          filePlayCapture.contains(QStringLiteral("-stream_loop"))
              && filePlayCapture.contains(QStringLiteral("selftest-play.mkv"))
              && filePlayCapture.contains(QStringLiteral("decklink")));
    QFile::remove(tempPlayFile);

    BackendController::clearCaptured();
    m_backend.stepFileOutput(100, nullptr);
    check(QStringLiteral("Player step control seeks the playout"),
          BackendController::capturedAnyContaining(QStringLiteral(" -ss ")));

    // every offered connector must exist on the selected hardware
    bool unsupportedOffered = false;
    const QString selfTestDevice = m_deckLinkCombo->currentData().toString();
    for (const auto& device : m_devices) {
        if (device.modelName != selfTestDevice) {
            continue;
        }
        const QStringList supported = DeckLinkDiscovery::connectionNamesFromMask(device.videoOutputConnections);
        for (int i = 1; i < m_videoOutputConnectionCombo->count(); ++i) {
            if (!supported.contains(m_videoOutputConnectionCombo->itemText(i))) {
                unsupportedOffered = true;
            }
        }
        break;
    }
    check(QStringLiteral("Output connector list matches the device hardware"), !unsupportedOffered);

    m_backend.togglePauseFileOutput();
    check(QStringLiteral("Pause toggle holds the playout"), m_backend.isFileOutputPaused());
    m_backend.togglePauseFileOutput();
    check(QStringLiteral("Resume returns the playout to playing"),
          m_backend.isFileOutputActive() && !m_backend.isFileOutputPaused());

    BackendController::clearCaptured();
    stopAllOutputs();
    check(QStringLiteral("Stop Output stops tracked outputs"),
          BackendController::capturedAnyContaining(QStringLiteral("taskkill")) && !m_testSignalOutputCheck->isChecked());

    m_testSignalOutputCheck->click();
    const QString signalStartCapture = BackendController::capturedCommands().join(QLatin1Char(' '));
    check(QStringLiteral("Test signal checkbox starts the generator on the selected device"),
          m_testSignalOutputCheck->isChecked()
              && signalStartCapture.contains(QStringLiteral("decklink"))
              && signalStartCapture.contains(m_videoOutputDeviceCombo->currentData().toString()),
          signalStartCapture);

    BackendController::clearCaptured();
    m_testSignalOutputCheck->click();
    check(QStringLiteral("Test signal checkbox stop kills the tracked process"),
          !m_testSignalOutputCheck->isChecked() && BackendController::capturedAnyContaining(QStringLiteral("taskkill")));

    const QString originalConnection = originalConfig.value(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), QStringLiteral("Auto"));
    if (originalConnection.isEmpty() || originalConnection == QStringLiteral("Auto")) {
        results << QStringLiteral("SKIP: Video Output connector SDK apply (set to Auto; select a connector to enable this check)");
    } else {
        m_videoOutputConnectionCombo->setCurrentText(originalConnection);
        applyVideoOutputConnection();
        check(QStringLiteral("Video Output connector applies through the SDK"),
              statusBar()->currentMessage().contains(QStringLiteral("SDK verified")),
              statusBar()->currentMessage());
    }

    m_themeCombo->setCurrentText(QStringLiteral("Light"));
    m_themeCombo->setCurrentText(QStringLiteral("Dark"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    check(QStringLiteral("Theme selection applies the dark palette"),
          QApplication::palette().color(QPalette::Window) == darkPalette.color(QPalette::Window));

    {
        // drag and drop: real QDragEnter/QDrop events through the filters
        const QString folder = QDir(m_backend.packageRoot()).absolutePath();
        const QString file = QDir(folder).filePath(QStringLiteral("VrecordQt6.exe"));
        const auto dropOnto = [](QWidget* widget, const QString& path) {
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(path)});
            const QPoint pos = widget->rect().center();
            QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(widget, &enter);
            if (!enter.isAccepted() || enter.dropAction() == Qt::IgnoreAction) {
                return false;
            }
            QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(widget, &drop);
            return drop.isAccepted() && drop.dropAction() != Qt::IgnoreAction;
        };
        const QString savedDir = m_dirEdit->text();
        const QString savedLogDir = m_logDirEdit->text();
        const QString savedPlay = m_playFileEdit->text();
        const auto same = [](const QString& a, const QString& b) {
            return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), Qt::CaseInsensitive) == 0;
        };
        m_dirEdit->clear();
        const bool dirOk = dropOnto(m_dirEdit, folder) && same(m_dirEdit->text(), folder);
        m_dirEdit->clear();
        const bool dirFromFileOk = dropOnto(m_dirEdit, file) && same(m_dirEdit->text(), folder);
        m_logDirEdit->clear();
        const bool logDirOk = dropOnto(m_logDirEdit, folder) && same(m_logDirEdit->text(), folder);
        m_playFileEdit->clear();
        const bool playOk = dropOnto(m_playFileEdit, file) && same(m_playFileEdit->text(), file);
        m_playFileEdit->clear();
        const bool viewOk = dropOnto(m_outputView, file) && same(m_playFileEdit->text(), file);
        const bool folderRefused = !dropOnto(m_playFileEdit, folder);
        m_dirEdit->clear();
        m_playFileEdit->clear();
        const bool windowOk = dropOnto(centralWidget(), folder) && same(m_dirEdit->text(), folder)
                              && dropOnto(centralWidget(), file) && same(m_playFileEdit->text(), file);
        m_dirEdit->setText(savedDir);
        m_logDirEdit->setText(savedLogDir);
        m_playFileEdit->setText(savedPlay);
        check(QStringLiteral("Drag and drop sets folder and file paths"),
              dirOk && dirFromFileOk && logDirOk && playOk && viewOk && folderRefused && windowOk,
              QStringLiteral("dir=%1 dirFromFile=%2 logDir=%3 play=%4 outputView=%5 folderRefused=%6 window=%7")
                  .arg(dirOk).arg(dirFromFileOk).arg(logDirOk).arg(playOk).arg(viewOk).arg(folderRefused).arg(windowOk));
    }

    updateDerivedState();
    check(QStringLiteral("Summary and validation panels populate"),
          !m_summaryText->toPlainText().isEmpty() && !m_validationText->toPlainText().isEmpty());

    m_config = originalConfig;
    populateFromConfig();
    syncConfigFromUi();
    QStringList changed;
    for (auto it = originalConfig.values.constBegin(); it != originalConfig.values.constEnd(); ++it) {
        if (m_config.value(it.key()) != it.value()) {
            changed << QStringLiteral("%1: '%2' -> '%3'").arg(it.key(), it.value(), m_config.value(it.key()));
        }
    }
    check(QStringLiteral("User settings are restored intact after the test"), changed.isEmpty(), changed.join(QStringLiteral("; ")));
    QFile::remove(QDir(m_backend.packageRoot()).filePath(QStringLiteral("VrecordQt6-selftest.conf")));

    BackendController::setTestMode(false);
    BackendController::clearCaptured();

    int failures = 0;
    for (const QString& line : results) {
        if (line.startsWith(QStringLiteral("FAIL"))) {
            ++failures;
        }
    }
    results.prepend(QStringLiteral("SELF TEST: %1 checks, %2 failures").arg(results.size()).arg(failures));
    return results;
}

void MainWindow::setComboValue(QComboBox* combo, const QString& value) {
    const int byText = combo->findText(value);
    if (byText >= 0) {
        combo->setCurrentIndex(byText);
        return;
    }
    const int byData = combo->findData(value);
    if (byData >= 0) {
        combo->setCurrentIndex(byData);
        return;
    }
    if (!value.isEmpty()) {
        combo->addItem(value, value);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

void MainWindow::setChoice(QComboBox* combo, const QString& value, const QString& fallback) {
    // shell option combos never grow: an unknown value falls back to the
    // default instead of being written back to the config
    int index = combo->findData(value);
    if (index < 0) {
        index = combo->findText(value);
    }
    if (index < 0) {
        index = combo->findData(fallback);
    }
    if (index < 0) {
        index = combo->findText(fallback);
    }
    combo->setCurrentIndex(qMax(index, 0));
}

void MainWindow::setRadioValue(const QString& value) {
    if (m_inputChoiceRadios.contains(value)) {
        m_inputChoiceRadios[value]->setChecked(true);
    } else if (!m_inputChoiceRadios.isEmpty()) {
        m_inputChoiceRadios.first()->setChecked(true);
    }
}

QString MainWindow::currentRadioValue() const {
    for (auto it = m_inputChoiceRadios.constBegin(); it != m_inputChoiceRadios.constEnd(); ++it) {
        if (it.value()->isChecked()) {
            return it.key();
        }
    }
    return QStringLiteral("0");
}
void MainWindow::applySelectedTheme() {
    if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance())) {
        if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive)) {
            app->setStyle(QStringLiteral("Fusion"));
        }

        const QString choice = m_themeCombo ? m_themeCombo->currentText() : QStringLiteral("Dark");
        if (choice == QStringLiteral("Light")) {
            app->setPalette(stockLightPalette());
        } else if (choice == QStringLiteral("System")) {
            app->setPalette(systemPrefersDarkTheme() ? stockDarkPalette() : stockLightPalette());
        } else {
            app->setPalette(stockDarkPalette());
        }

        app->setStyleSheet(QStringLiteral(
            "QLineEdit, QTextEdit, QPlainTextEdit {"
            " color: palette(text);"
            " selection-color: palette(highlighted-text);"
            " selection-background-color: palette(highlight);"
            "}"));
    }
}

void MainWindow::updateDerivedState() {
    // while populateFromConfig() fills the widgets one by one, every
    // textChanged lands here: syncing then would copy the not-yet-filled
    // widgets (stale values) back over the config being loaded
    if (m_populating) {
        return;
    }
    applyAudioConstraints();
    autoAspectForStandard();
    syncConfigFromUi();
    const bool ffplayMonitor = m_ffplayMonitorCheck->isChecked();
    m_playbackViewCombo->setEnabled(ffplayMonitor);
    m_playbackPassCombo->setEnabled(ffplayMonitor);
    m_ffplayAspectCombo->setEnabled(ffplayMonitor);
    // relay copies the input to the output, so it overrides the output standard
    const bool relayOn = m_previewRelayCheck->isChecked();
    m_outputStandardCombo->setEnabled(!relayOn);
    m_outputStandardCombo->setToolTip(relayOn ? QStringLiteral("Relay is on: the output follows the input mode.")
                                              : QStringLiteral("Standard sent to the hardware output for file playout and the test signal.\n"
                                                               "Auto plays files in their native mode; independent of the capture Standard."));
    // every change is kept: save shortly after the last edit
    if (!BackendController::testMode() && m_autosaveTimer) {
        m_autosaveTimer->start();
    }
    m_summaryText->setPlainText(m_config.summary());
    const QStringList issues = m_config.validate();
    QString validationText = issues.isEmpty() ? QStringLiteral("OK") : issues.join(QLatin1Char('\n'));
    if (!m_selfTestResults.isEmpty()) {
        validationText += QStringLiteral("\n--- Auto load self test ---\n") + m_selfTestResults.join(QLatin1Char('\n'));
    }
    m_validationText->setPlainText(validationText);
    // the FFmpeg Status box is filled by the component check (refreshComponents)
    QString envText;
    QTextStream envStream(&envText);
    envStream << "Config file: " << QDir::toNativeSeparators(m_config.configPath) << '\n';
    envStream << "Theme: " << m_themeCombo->currentText() << '\n';
    envStream << "Input mode: " << currentRadioValue() << '\n';
    m_environmentText->setPlainText(envText);
    m_previewInfoText->setPlainText(QStringLiteral("Hardware output: %1\nRelay device: %2\nPlay file: %3\nRelay enabled: %4\nAuto-detect format: %5")
                                        .arg(m_videoOutputDeviceCombo->currentText().isEmpty() ? QStringLiteral("(none)") : m_videoOutputDeviceCombo->currentText(),
                                             m_relayDeviceCombo->currentText().isEmpty() ? QStringLiteral("(none)") : m_relayDeviceCombo->currentText(),
                                             m_playFileEdit->text().trimmed().isEmpty() ? QStringLiteral("(none)") : m_playFileEdit->text().trimmed(),
                                             m_previewRelayCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"),
                                             m_filePlayAutoCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no")));
    m_outputInfoText->setPlainText(QStringLiteral("Test signal: %1\nOutput device: %2\nStandard: %3\nPattern: %4\nAudio: %5\nConnector: %6")
                                       .arg(m_testSignalOutputCheck->isChecked() ? QStringLiteral("enabled") : QStringLiteral("disabled"),
                                            m_videoOutputDeviceCombo->currentText().isEmpty() ? QStringLiteral("(none)") : m_videoOutputDeviceCombo->currentText(),
                                            m_standardCombo->currentText().isEmpty() ? QStringLiteral("(none)") : m_standardCombo->currentText(),
                                            m_testPatternCombo->currentText(),
                                            m_testAudioCombo->currentText(),
                                            m_videoOutputConnectionCombo->currentText()));
}

void MainWindow::showError(const QString& message) {
    // the self-test runs unattended (CI: offscreen, no card): a modal box
    // there would wait forever for a click, so log the message instead
    if (BackendController::testMode()) {
        appendActivity(QStringLiteral("error (self-test, not shown): %1").arg(QString(message).replace(QLatin1Char('\n'), QLatin1Char(' '))));
        fprintf(stderr, "self-test error dialog suppressed: %s\n", qPrintable(message));
        fflush(stderr);
        return;
    }
    QMessageBox::warning(this, QStringLiteral("vrecord Qt6"), message);
}
