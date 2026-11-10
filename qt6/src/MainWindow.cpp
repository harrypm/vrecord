#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSplitter>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
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
    buildUi();
    populateFromConfig();
    applySelectedTheme();
    refreshDeckLinkDevices();
    m_selfTestResults = runSelfTest();
    updateDerivedState();
    // legacy parity: the live input preview starts automatically with the window
    QString previewError;
    if (m_backend.startVideoPreview(m_config, &previewError)) {
        m_previewToggleButton->setText(QStringLiteral("Stop Preview"));
        if (!previewError.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Live input preview running%1").arg(previewError), 5000);
        }
    } else {
        m_selfTestResults << QStringLiteral("NOTE: auto preview did not start: %1").arg(previewError);
        updateDerivedState();
    }
}
QComboBox* MainWindow::makeCombo(const QStringList& items) {
    auto* combo = new QComboBox(this);
    combo->addItems(items);
    return combo;
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
    m_videoInputCombo = new QComboBox(this);
    for (const auto& entry : {std::pair<QString, QString>{QStringLiteral("SDI"), QStringLiteral("sdi")},
                              std::pair<QString, QString>{QStringLiteral("HDMI"), QStringLiteral("hdmi")},
                              std::pair<QString, QString>{QStringLiteral("Component"), QStringLiteral("component")},
                              std::pair<QString, QString>{QStringLiteral("Composite"), QStringLiteral("composite")},
                              std::pair<QString, QString>{QStringLiteral("S-Video"), QStringLiteral("s-video")}}) {
        m_videoInputCombo->addItem(entry.first, entry.second);
    }
    m_audioInputCombo = makeCombo({QStringLiteral("embedded"), QStringLiteral("aesebu"), QStringLiteral("analog"), QStringLiteral("hdmi")});
    m_audioMappingCombo = makeCombo({QStringLiteral("2 channels"), QStringLiteral("4 channels"), QStringLiteral("8 channels"), QStringLiteral("16 channels")});
    m_standardCombo = makeCombo({QStringLiteral("NTSC"),
                                 QStringLiteral("PAL"),
                                 QStringLiteral("23ps - 1080p23.98"),
                                 QStringLiteral("24ps - 1080p24"),
                                 QStringLiteral("Hp25 - 1080p25"),
                                 QStringLiteral("Hp29 - 1080p29.97"),
                                 QStringLiteral("Hp30 - 1080p30"),
                                 QStringLiteral("Hi50 - 1080i50"),
                                 QStringLiteral("Hi59 - 1080i59.94"),
                                 QStringLiteral("Hi60 - 1080i60"),
                                 QStringLiteral("hp50 - 720p50"),
                                 QStringLiteral("hp59 - 720p59.94"),
                                 QStringLiteral("hp60 - 720p60"),
                                 QStringLiteral("Hp50 - 1080p50"),
                                 QStringLiteral("Hp59 - 1080p59.94"),
                                 QStringLiteral("Hp60 - 1080p60"),
                                 QStringLiteral("2d23 - 2048p23.98"),
                                 QStringLiteral("2d24 - 2048p24"),
                                 QStringLiteral("2d25 - 2048p25"),
                                 QStringLiteral("2d29 - 2048p29.97"),
                                 QStringLiteral("2d30 - 2048p30"),
                                 QStringLiteral("2d50 - 2048p50"),
                                 QStringLiteral("2d59 - 2048p59.94"),
                                 QStringLiteral("2d60 - 2048p60"),
                                 QStringLiteral("4k23 - UHD 23.98"),
                                 QStringLiteral("4k24 - UHD 24"),
                                 QStringLiteral("4k25 - UHD 25"),
                                 QStringLiteral("4k29 - UHD 29.97"),
                                 QStringLiteral("4k30 - UHD 30"),
                                 QStringLiteral("4k50 - UHD 50"),
                                 QStringLiteral("4k59 - UHD 59.94"),
                                 QStringLiteral("4k60 - UHD 60"),
                                 QStringLiteral("4d23 - 4K DCI 23.98"),
                                 QStringLiteral("4d24 - 4K DCI 24"),
                                 QStringLiteral("4d25 - 4K DCI 25"),
                                 QStringLiteral("4d29 - 4K DCI 29.97"),
                                 QStringLiteral("4d30 - 4K DCI 30"),
                                 QStringLiteral("4d50 - 4K DCI 50"),
                                 QStringLiteral("4d59 - 4K DCI 59.94"),
                                 QStringLiteral("4d60 - 4K DCI 60")});
    m_videoOutputConnectionCombo = makeCombo({QStringLiteral("Auto"),
                                              QStringLiteral("SDI"),
                                              QStringLiteral("HDMI"),
                                              QStringLiteral("Component"),
                                              QStringLiteral("Composite"),
                                              QStringLiteral("S-Video")});
    m_bitDepthCombo = makeCombo({QStringLiteral("8 bit"), QStringLiteral("10 bit")});
    m_aspectRatioCombo = makeCombo({QStringLiteral("4/3"), QStringLiteral("16/9")});
    m_embedLogsCombo = makeCombo({QStringLiteral("yes"), QStringLiteral("no")});
    m_containerCombo = makeCombo({QStringLiteral("Matroska"), QStringLiteral("QuickTime"), QStringLiteral("AVI"), QStringLiteral("MXF"), QStringLiteral("MP4")});
    m_videoCodecCombo = makeCombo({QStringLiteral("FFV1 version 3"), QStringLiteral("ProRes"), QStringLiteral("ProRes (HQ)"), QStringLiteral("JPEG2000"), QStringLiteral("h264")});
    m_audioCodecCombo = makeCombo({QStringLiteral("24-bit PCM"), QStringLiteral("24-bit FLAC")});
    m_playbackViewCombo = makeCombo({QStringLiteral("none"), QStringLiteral("mpv"), QStringLiteral("ffplay"), QStringLiteral("system")});
    m_playbackPassCombo = makeCombo({QStringLiteral("none"), QStringLiteral("mpv"), QStringLiteral("ffplay"), QStringLiteral("system")});
    m_mp4Check = makeCheckBox(QStringLiteral("Access MP4"));
    m_frameMd5Check = makeCheckBox(QStringLiteral("FrameMD5s"));
    m_timecodeVttCheck = makeCheckBox(QStringLiteral("Timecode VTT"));
    m_qcToolsCheck = makeCheckBox(QStringLiteral("Vrecord QCTools Report"));

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

    auto* deckLowerRow = new QHBoxLayout();
    auto* playbackBox = new QGroupBox(QStringLiteral("Playback options"), deckTab);
    auto* playbackLayout = new QFormLayout(playbackBox);
    playbackLayout->addRow(QStringLiteral("View (recording)"), m_playbackViewCombo);
    playbackLayout->addRow(QStringLiteral("View (passthrough)"), m_playbackPassCombo);
    auto* sidecarBox = new QGroupBox(QStringLiteral("Sidecar file options"), deckTab);
    auto* sidecarLayout = new QVBoxLayout(sidecarBox);
    sidecarLayout->addWidget(m_mp4Check);
    sidecarLayout->addWidget(m_frameMd5Check);
    sidecarLayout->addWidget(m_timecodeVttCheck);
    sidecarLayout->addWidget(m_qcToolsCheck);
    auto* rs422Box = new QGroupBox(QStringLiteral("RS422 deck control and timecode"), deckTab);
    auto* rs422Layout = new QFormLayout(rs422Box);
    rs422Layout->addRow(QStringLiteral("Status"), new QLabel(QStringLiteral("Managed by backend / legacy UI"), rs422Box));
    rs422Layout->addRow(QStringLiteral("Timecode"), new QLabel(QStringLiteral("Managed by backend / legacy UI"), rs422Box));
    deckLowerRow->addWidget(playbackBox);
    deckLowerRow->addWidget(sidecarBox);
    deckLowerRow->addWidget(rs422Box);
    deckRoot->addWidget(deckInputBox);
    deckRoot->addWidget(deckOutputBox);
    deckRoot->addLayout(deckLowerRow);
    deckRoot->addStretch();
    m_tabs->addTab(deckTab, QStringLiteral("Decklink"));

    m_dvDeviceCombo = makeCombo({QStringLiteral("auto"), QStringLiteral("device 0")});
    m_dvContainerCombo = makeCombo({QStringLiteral("dv"), QStringLiteral("mov"), QStringLiteral("mkv")});
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
    dvControlLayout->addWidget(new QLabel(QStringLiteral("DV deck control remains backend-managed on Windows; use Record / Passthrough / legacy UI for transport actions."), dvControlBox));
    dvRoot->addWidget(dvInputBox);
    dvRoot->addWidget(dvControlBox);
    dvRoot->addStretch();
    m_tabs->addTab(dvTab, QStringLiteral("DV"));

    m_audioDeviceCombo = makeCombo({QStringLiteral("default"), QStringLiteral("Blackmagic"), QStringLiteral("Line In"), QStringLiteral("Microphone")});
    m_audioModeCodecCombo = makeCombo({QStringLiteral("24-bit PCM"), QStringLiteral("24-bit FLAC")});
    m_audioChannelsCombo = makeCombo({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("8")});
    m_audioSampleRateCombo = makeCombo({QStringLiteral("32000"), QStringLiteral("44100"), QStringLiteral("48000"), QStringLiteral("96000")});
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
    audioRoot->addWidget(audioBox);
    audioRoot->addStretch();
    m_tabs->addTab(audioTab, QStringLiteral("Audio"));

    m_waveformScaleCombo = makeCombo({QStringLiteral("IRE"), QStringLiteral("mV"), QStringLiteral("Digital")});
    m_signalViewCombo = makeCombo({QStringLiteral("Color"), QStringLiteral("Luma"), QStringLiteral("Waveform"), QStringLiteral("Vectorscope")});
    m_signalIntCombo = makeCombo({QStringLiteral("Auto"), QStringLiteral("Interlaced"), QStringLiteral("Progressive")});
    m_monitorAudioCombo = makeCombo({QStringLiteral("yes"), QStringLiteral("no")});
    m_ntsc43SarCombo = makeCombo({QStringLiteral("8:9"), QStringLiteral("10:11")});
    m_ntsc169SarCombo = makeCombo({QStringLiteral("32:27"), QStringLiteral("40:33")});
    m_pal43SarCombo = makeCombo({QStringLiteral("16:15"), QStringLiteral("12:11")});
    m_pal169SarCombo = makeCombo({QStringLiteral("64:45"), QStringLiteral("16:11")});
    m_ccVttCheck = makeCheckBox(QStringLiteral("Make a webVTT"));
    m_ccSrtCheck = makeCheckBox(QStringLiteral("Make an SRT"));
    m_capToMp4Check = makeCheckBox(QStringLiteral("Wrap captions into Access MP4 file"));
    m_mediaIdTypeEdit = makeLineEdit();
    m_webVttExtraEdit = makeLineEdit();
    m_colorSafeCheck = makeCheckBox(QStringLiteral("Enable color free alerts"));
    m_themeCombo = makeCombo({QStringLiteral("Dark"), QStringLiteral("Light"), QStringLiteral("System")});
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
    playerForm->addRow(QStringLiteral("Monitor Audio?"), m_monitorAudioCombo);
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

    auto* lowerConfigRow = new QHBoxLayout();
    auto* invertBox = new QGroupBox(QStringLiteral("Invert Audio Phase"), configTab);
    auto* invertLayout = new QVBoxLayout(invertBox);
    invertLayout->addWidget(new QLabel(QStringLiteral("WARNING: Do not use this option unless required"), invertBox));
    invertLayout->addWidget(m_invertPhase2Check);
    invertLayout->addWidget(m_invertPhase4Check);
    auto* toolsBox = new QGroupBox(QStringLiteral("Optional Tools"), configTab);
    auto* toolsLayout = new QVBoxLayout(toolsBox);
    toolsLayout->addWidget(new QLabel(QStringLiteral("deckcontrol, gnuplot, mediaconch, mkvpropedit, mpv, and dvpackager availability are still detected by the backend."), toolsBox));
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
    m_previewInfoText = new QPlainTextEdit(this);
    m_previewInfoText->setReadOnly(true);
    m_feedTimer = new QTimer(this);
    m_feedTimer->setInterval(250);
    auto* previewTab = new QWidget(this);
    auto* previewRoot = new QHBoxLayout(previewTab);
    auto* livePreviewBox = new QGroupBox(QStringLiteral("Live input preview"), previewTab);
    auto* livePreviewLayout = new QVBoxLayout(livePreviewBox);
    auto* outputDeviceBox = new QGroupBox(QStringLiteral("Output device feed"), previewTab);
    auto* outputDeviceLayout = new QVBoxLayout(outputDeviceBox);
    m_livePreviewLabel = new QLabel(livePreviewBox);
    m_livePreviewLabel->setMinimumHeight(240);
    m_livePreviewLabel->setAlignment(Qt::AlignCenter);
    m_outputFeedLabel = new QLabel(outputDeviceBox);
    m_outputFeedLabel->setMinimumHeight(240);
    m_outputFeedLabel->setAlignment(Qt::AlignCenter);
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
    m_positionLabel = new QLabel(QStringLiteral("00:00:00:00"), outputDeviceBox);
    m_previewToggleButton = new QPushButton(styleIcon(QStyle::SP_MediaPlay), QStringLiteral("Start Preview"), livePreviewBox);
    m_previewToggleButton->setIconSize(QSize(20, 20));
    livePreviewLayout->addWidget(new QLabel(QStringLiteral("Record: input preview"), livePreviewBox));
    livePreviewLayout->addWidget(m_livePreviewLabel, 1);
    livePreviewLayout->addWidget(m_previewToggleButton);
    livePreviewLayout->addWidget(m_previewInfoText);
    outputDeviceLayout->addWidget(new QLabel(QStringLiteral("Hardware video output device"), outputDeviceBox));
    outputDeviceLayout->addWidget(m_videoOutputDeviceCombo);
    outputDeviceLayout->addWidget(m_previewRelayCheck);
    outputDeviceLayout->addWidget(new QLabel(QStringLiteral("Relay output device"), outputDeviceBox));
    outputDeviceLayout->addWidget(m_relayDeviceCombo);
    outputDeviceLayout->addWidget(m_filePlayAutoCheck);
    outputDeviceLayout->addWidget(m_outputFeedLabel, 1);
    auto* transportRow = new QHBoxLayout();
    transportRow->addWidget(btnBack100);
    transportRow->addWidget(btnBack1);
    transportRow->addWidget(m_playPauseButton);
    transportRow->addWidget(btnFwd1);
    transportRow->addWidget(btnFwd100);
    outputDeviceLayout->addLayout(transportRow);
    outputDeviceLayout->addWidget(m_seekSlider);
    outputDeviceLayout->addWidget(m_positionLabel);
    outputDeviceLayout->addWidget(new QLabel(QStringLiteral("Preview / playout notes"), outputDeviceBox));
    outputDeviceLayout->addWidget(m_previewInfoText);
    previewRoot->addWidget(livePreviewBox, 2);
    previewRoot->addWidget(outputDeviceBox, 1);
    m_tabs->addTab(previewTab, QStringLiteral("Video Preview"));

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
    auto* outputTab = new QWidget(this);
    auto* outputRoot = new QVBoxLayout(outputTab);
    auto* testBox = new QGroupBox(QStringLiteral("Output options"), outputTab);
    auto* testLayout = new QVBoxLayout(testBox);
    testLayout->addWidget(new QLabel(QStringLiteral("Check the device chain with a test signal routed to the selected hardware output device and shaped by the selected standard."), testBox));
    testLayout->addWidget(m_testSignalOutputCheck);
    auto* testForm = new QFormLayout();
    testForm->addRow(QStringLiteral("Test pattern"), m_testPatternCombo);
    testForm->addRow(QStringLiteral("Test audio"), m_testAudioCombo);
    testLayout->addLayout(testForm);
    testLayout->addWidget(m_outputInfoText);
    outputRoot->addWidget(testBox);
    outputRoot->addStretch();
    m_tabs->addTab(outputTab, QStringLiteral("Output"));

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
    auto* passthroughButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo_playback.png")), QStringLiteral("Passthrough"), central);
    auto* audioButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo_audio.png")), QStringLiteral("Audio Check"), central);
    auto* fileOutputButton = new QPushButton(styleIcon(QStyle::SP_MediaPlay), QStringLiteral("Play File to Output"), central);
    auto* stopOutputButton = new QPushButton(styleIcon(QStyle::SP_MediaStop), QStringLiteral("Stop Output"), central);
    auto* legacyButton = new QPushButton(logoIcon(QStringLiteral("vrecord_logo_edit.png")), QStringLiteral("Open Legacy UI"), central);
    for (QPushButton* button : {refreshDevicesButton, saveButton, recordButton, passthroughButton,
                                audioButton, fileOutputButton, stopOutputButton, legacyButton}) {
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
    actionBar->addWidget(legacyButton);
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
    logsSplitter->addWidget(summaryBox);
    logsSplitter->addWidget(validationBox);
    logsSplitter->setStretchFactor(0, 1);
    logsSplitter->setStretchFactor(1, 1);
    logsRoot->addWidget(logsSplitter);
    m_tabs->addTab(logsTab, QStringLiteral("Logs"));

    setCentralWidget(central);
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
                        m_testPatternCombo, m_testAudioCombo, m_videoOutputConnectionCombo}) {
        connect(combo, &QComboBox::currentTextChanged, this, updateSlots);
    }
    for (auto* check : {m_enableSuffixCheck, m_mp4Check, m_frameMd5Check, m_timecodeVttCheck, m_qcToolsCheck,
                        m_dvOptionSCheck, m_dvOptionDCheck, m_dvOptionTCheck, m_dvOptionTcCheck, m_ccVttCheck,
                        m_ccSrtCheck, m_capToMp4Check, m_colorSafeCheck, m_invertPhase2Check, m_invertPhase4Check,
                        m_previewRelayCheck, m_filePlayAutoCheck, m_testSignalOutputCheck}) {
        connect(check, &QCheckBox::checkStateChanged, this, updateSlots);
    }
    for (auto* radio : m_inputChoiceRadios) {
        connect(radio, &QRadioButton::toggled, this, updateSlots);
    }
    connect(m_idEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_prefixEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_suffixEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_enableSuffixCheck, &QCheckBox::checkStateChanged, this, [outputNameLabel, this] {
        syncConfigFromUi();
        outputNameLabel->setText(m_config.outputFilename());
    });
    connect(m_dirEdit, &QLineEdit::textChanged, this, [outputNameLabel, this] {
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
    connect(legacyButton, &QPushButton::clicked, this, &MainWindow::openLegacyUi);
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
    connect(m_deckLinkCombo, &QComboBox::activated, this, [this](int) {
        rebuildConnectorCombos();
        updateDerivedState();
    });
    connect(m_videoInputCombo, &QComboBox::activated, this, [this](int) {
        QString errorMessage;
        if (!m_backend.applyVideoInputConnection(m_config, &errorMessage)) {
            showError(errorMessage);
            return;
        }
        if (m_backend.isVideoPreviewActive()) {
            m_backend.stopVideoPreview();
            QString restartError;
            if (m_backend.startVideoPreview(m_config, &restartError)) {
                statusBar()->showMessage(QStringLiteral("Live input preview restarted on %1").arg(m_videoInputCombo->currentText()), 5000);
            }
        }
        updateDerivedState();
    });
    connect(m_previewToggleButton, &QPushButton::clicked, this, &MainWindow::togglePreview);
    connect(m_playPauseButton, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    connect(btnBack100, &QPushButton::clicked, this, [this] { stepPlayback(-100); });
    connect(btnBack1, &QPushButton::clicked, this, [this] { stepPlayback(-1); });
    connect(btnFwd1, &QPushButton::clicked, this, [this] { stepPlayback(1); });
    connect(btnFwd100, &QPushButton::clicked, this, [this] { stepPlayback(100); });
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::seekToSlider);
    connect(m_feedTimer, &QTimer::timeout, this, &MainWindow::refreshFeeds);
    m_feedTimer->start();

    syncConfigFromUi();
    outputNameLabel->setText(m_config.outputFilename());
}

void MainWindow::populateFromConfig() {
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
    setComboValue(m_videoInputCombo, m_config.value(QStringLiteral("VIDEO_INPUT_CHOICE"), QStringLiteral("sdi")));
    setComboValue(m_audioInputCombo, m_config.value(QStringLiteral("AUDIO_INPUT_CHOICE"), QStringLiteral("embedded")));
    setComboValue(m_audioMappingCombo, m_config.value(QStringLiteral("AUDIO_MAPPING_CHOICE"), QStringLiteral("2 channels")));
    setComboValue(m_standardCombo, m_config.value(QStringLiteral("STANDARD_CHOICE"), QStringLiteral("NTSC")));
    setComboValue(m_embedLogsCombo, m_config.value(QStringLiteral("EMBED_LOGS_CHOICE"), QStringLiteral("yes")));
    setComboValue(m_containerCombo, m_config.value(QStringLiteral("CONTAINER_CHOICE"), QStringLiteral("Matroska")));
    setComboValue(m_videoCodecCombo, m_config.value(QStringLiteral("VIDEO_CODEC_CHOICE"), QStringLiteral("FFV1 version 3")));
    setComboValue(m_audioCodecCombo, m_config.value(QStringLiteral("AUDIO_CODEC_CHOICE"), QStringLiteral("24-bit PCM")));
    setComboValue(m_playbackViewCombo, m_config.value(QStringLiteral("PLAYBACKVIEW_CHOICE"), QStringLiteral("none")));
    setComboValue(m_playbackPassCombo, m_config.value(QStringLiteral("PLAYBACKVIEW_CHOICE_PASS"), QStringLiteral("none")));
    setComboValue(m_bitDepthCombo, m_config.value(QStringLiteral("VIDEO_BIT_DEPTH_CHOICE"), QStringLiteral("10 bit")));
    setComboValue(m_aspectRatioCombo, m_config.value(QStringLiteral("ASPECT_RATIO_CHOICE"), QStringLiteral("4/3")));
    setComboValue(m_dvDeviceCombo, m_config.value(QStringLiteral("DVRESCUE_INPUT_CHOICE"), QStringLiteral("auto")));
    setComboValue(m_dvContainerCombo, m_config.value(QStringLiteral("DV_CONTAINER_CHOICE"), QStringLiteral("dv")));
    setComboValue(m_audioDeviceCombo, m_config.value(QStringLiteral("AUDIO_DEV_CHOICE"), QStringLiteral("default")));
    setComboValue(m_audioModeCodecCombo, m_config.value(QStringLiteral("AUDIO_MODE_CODEC_CHOICE"), QStringLiteral("24-bit PCM")));
    setComboValue(m_audioChannelsCombo, m_config.value(QStringLiteral("AUDIO_CHANNEL_CHOICE"), QStringLiteral("2")));
    setComboValue(m_audioSampleRateCombo, m_config.value(QStringLiteral("AUDIO_MODE_SR_CHOICE"), QStringLiteral("48000")));
    setComboValue(m_waveformScaleCombo, m_config.value(QStringLiteral("WAVEFORM_SCALE_CHOICE"), QStringLiteral("IRE")));
    setComboValue(m_signalViewCombo, m_config.value(QStringLiteral("SIGNAL_VIEW_CHOICE"), QStringLiteral("Color")));
    setComboValue(m_signalIntCombo, m_config.value(QStringLiteral("SIGNAL_INT_CHOICE"), QStringLiteral("Auto")));
    setComboValue(m_monitorAudioCombo, m_config.value(QStringLiteral("MONITOR_AUDIO_CHOICE"), QStringLiteral("yes")));
    setComboValue(m_ntsc43SarCombo, m_config.value(QStringLiteral("NTSC_43_SAR_CHOICE"), QStringLiteral("8:9")));
    setComboValue(m_ntsc169SarCombo, m_config.value(QStringLiteral("NTSC_169_SAR_CHOICE"), QStringLiteral("32:27")));
    setComboValue(m_pal43SarCombo, m_config.value(QStringLiteral("PAL_43_SAR_CHOICE"), QStringLiteral("16:15")));
    setComboValue(m_pal169SarCombo, m_config.value(QStringLiteral("PAL_169_SAR_CHOICE"), QStringLiteral("64:45")));
    setComboValue(m_themeCombo, m_config.value(QStringLiteral("THEME_CHOICE"), QStringLiteral("Dark")));
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
    m_config.setValue(QStringLiteral("VIDEO_INPUT_CHOICE"),
                      m_videoInputCombo->currentData().toString().isEmpty()
                          ? m_videoInputCombo->currentText().toLower()
                          : m_videoInputCombo->currentData().toString());
    m_config.setValue(QStringLiteral("AUDIO_INPUT_CHOICE"), m_audioInputCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MAPPING_CHOICE"), m_audioMappingCombo->currentText());
    m_config.setValue(QStringLiteral("STANDARD_CHOICE"), m_standardCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_OUTPUT_CONNECTION_CHOICE"), m_videoOutputConnectionCombo->currentText());
    m_config.setValue(QStringLiteral("EMBED_LOGS_CHOICE"), m_embedLogsCombo->currentText());
    m_config.setValue(QStringLiteral("CONTAINER_CHOICE"), m_containerCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_CODEC_CHOICE"), m_videoCodecCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_CODEC_CHOICE"), m_audioCodecCombo->currentText());
    m_config.setValue(QStringLiteral("PLAYBACKVIEW_CHOICE"), m_playbackViewCombo->currentText());
    m_config.setValue(QStringLiteral("PLAYBACKVIEW_CHOICE_PASS"), m_playbackPassCombo->currentText());
    m_config.setValue(QStringLiteral("VIDEO_BIT_DEPTH_CHOICE"), m_bitDepthCombo->currentText());
    m_config.setValue(QStringLiteral("ASPECT_RATIO_CHOICE"), m_aspectRatioCombo->currentText());
    m_config.setValue(QStringLiteral("DVRESCUE_INPUT_CHOICE"), m_dvDeviceCombo->currentText());
    m_config.setValue(QStringLiteral("DV_CONTAINER_CHOICE"), m_dvContainerCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_DEV_CHOICE"), m_audioDeviceCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MODE_CODEC_CHOICE"), m_audioModeCodecCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_CHANNEL_CHOICE"), m_audioChannelsCombo->currentText());
    m_config.setValue(QStringLiteral("AUDIO_MODE_SR_CHOICE"), m_audioSampleRateCombo->currentText());
    m_config.setValue(QStringLiteral("WAVEFORM_SCALE_CHOICE"), m_waveformScaleCombo->currentText());
    m_config.setValue(QStringLiteral("SIGNAL_VIEW_CHOICE"), m_signalViewCombo->currentText());
    m_config.setValue(QStringLiteral("SIGNAL_INT_CHOICE"), m_signalIntCombo->currentText());
    m_config.setValue(QStringLiteral("MONITOR_AUDIO_CHOICE"), m_monitorAudioCombo->currentText());
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
    m_config.setValue(QStringLiteral("FILEPLAY_AUTO_CHOICE"), m_filePlayAutoCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("TEST_SIGNAL_OUTPUT_CHOICE"), m_testSignalOutputCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    m_config.setValue(QStringLiteral("MEDIA_ID_TYPE"), m_mediaIdTypeEdit->text().trimmed());
    m_config.setValue(QStringLiteral("WEBVTT_EXTRA"), m_webVttExtraEdit->text().trimmed());
}

void MainWindow::populateDeckLinkCombo(const QVector<DeckLinkDeviceInfo>& devices, const QString& selectedValue) {
    m_deckLinkCombo->clear();
    m_videoOutputDeviceCombo->clear();
    m_relayDeviceCombo->clear();
    for (const auto& device : devices) {
        m_deckLinkCombo->addItem(device.displayName, device.modelName);
        m_videoOutputDeviceCombo->addItem(device.displayName, device.modelName);
        m_relayDeviceCombo->addItem(device.displayName, device.modelName);
    }
    const int idx = m_deckLinkCombo->findData(selectedValue);
    if (idx >= 0) {
        m_deckLinkCombo->setCurrentIndex(idx);
    } else if (!devices.isEmpty()) {
        m_deckLinkCombo->setCurrentIndex(0);
    }
    const int outputIdx = m_videoOutputDeviceCombo->findData(m_config.value(QStringLiteral("VIDEO_OUTPUT_DEVICE_CHOICE")));
    if (outputIdx >= 0) {
        m_videoOutputDeviceCombo->setCurrentIndex(outputIdx);
    } else if (!devices.isEmpty()) {
        m_videoOutputDeviceCombo->setCurrentIndex(0);
    }
    const int relayIdx = m_relayDeviceCombo->findData(m_config.value(QStringLiteral("PREVIEW_RELAY_DEV_CHOICE")));
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
        if (device.modelName == selectedDevice) {
            inputMask = device.videoInputConnections;
            outputMask = device.videoOutputConnections;
            break;
        }
    }

    // input connectors (config stores the lowercase names)
    const QString currentInput = m_videoInputCombo->currentData().toString();
    m_videoInputCombo->clear();
    const QStringList inputNames = DeckLinkDiscovery::connectionNamesFromMask(inputMask);
    for (const QString& name : inputNames) {
        m_videoInputCombo->addItem(name, name.toLower());
    }
    if (m_videoInputCombo->findData(currentInput) < 0 && m_videoInputCombo->count() > 0) {
        m_videoInputCombo->setCurrentIndex(0);
    }

    // output connectors: Auto always allowed, hardware list otherwise
    const QString currentOutput = m_videoOutputConnectionCombo->currentText();
    m_videoOutputConnectionCombo->clear();
    m_videoOutputConnectionCombo->addItem(QStringLiteral("Auto"));
    const QStringList outputNames = DeckLinkDiscovery::connectionNamesFromMask(outputMask);
    for (const QString& name : outputNames) {
        m_videoOutputConnectionCombo->addItem(name);
    }
    if (m_videoOutputConnectionCombo->findText(currentOutput) < 0) {
        m_videoOutputConnectionCombo->setCurrentIndex(0);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // persist the current selections (recording/auxiliary directories, playout
    // file, connector choices) so they are waiting at the next launch
    syncConfigFromUi();
    m_config.save();
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

void MainWindow::startRecord() {
    syncConfigFromUi();
    // the recorder needs the card input: drop the live preview first
    m_backend.stopVideoPreview();
    m_previewToggleButton->setText(QStringLiteral("Start Preview"));
    m_livePreviewLabel->clear();
    QString errorMessage;
    if (!m_backend.startRecord(m_config, &errorMessage)) {
        showError(errorMessage);
    }
}

void MainWindow::startPassthrough() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startPassthrough(m_config, &errorMessage)) {
        showError(errorMessage);
    }
}

void MainWindow::startAudioCheck() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startAudioCheck(m_config, &errorMessage)) {
        showError(errorMessage);
    }
}
void MainWindow::startFileOutput() {
    syncConfigFromUi();
    QString errorMessage;
    if (!m_backend.startFileOutput(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Playing %1 to the hardware output").arg(m_playFileEdit->text().trimmed()), 3000);
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
    m_outputFeedLabel->clear();
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
    statusBar()->showMessage(enabled
                                 ? QStringLiteral("Started test signal output")
                                 : QStringLiteral("Stopped test signal output"),
                             3000);
}

void MainWindow::togglePreview() {
    QString errorMessage;
    if (m_backend.isVideoPreviewActive()) {
        m_backend.stopVideoPreview();
        m_previewToggleButton->setText(QStringLiteral("Start Preview"));
        m_previewToggleButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        m_livePreviewLabel->clear();
        statusBar()->showMessage(QStringLiteral("Live preview stopped"), 3000);
        return;
    }
    syncConfigFromUi();
    QString inputApplyError;
    if (!m_backend.applyVideoInputConnection(m_config, &inputApplyError)) {
        showError(inputApplyError);
        return;
    }
    if (!m_backend.startVideoPreview(m_config, &errorMessage)) {
        showError(errorMessage);
        return;
    }
    m_previewToggleButton->setText(QStringLiteral("Stop Preview"));
    m_previewToggleButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaStop));
    statusBar()->showMessage(errorMessage.isEmpty()
                                 ? QStringLiteral("Live input preview running")
                                 : QStringLiteral("Live input preview running%1").arg(errorMessage),
                             5000);
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
    const QPixmap preview(m_backend.previewImagePath());
    if (!preview.isNull()) {
        m_livePreviewLabel->setPixmap(preview.scaled(m_livePreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    const QPixmap feed(m_backend.outputFeedImagePath());
    if (!feed.isNull()) {
        m_outputFeedLabel->setPixmap(feed.scaled(m_outputFeedLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    updateTransportUi();
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
    if (active && paused) {
        m_positionLabel->setText(QStringLiteral("frozen at %1 - press Play to continue").arg(tc));
    } else if (m_backend.isFileOutputCued()) {
        m_positionLabel->setText(QStringLiteral("cued at %1 - press Play to start there").arg(tc));
    } else if (duration > 0) {
        m_positionLabel->setText(QStringLiteral("%1 / %2").arg(tc, formatHms(qRound64(duration))));
    } else {
        m_positionLabel->setText(tc);
    }
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
    const auto check = [&](const QString& name, bool ok, const QString& detail = QString()) {
        results << QStringLiteral("%1: %2%3").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                                 name,
                                                 detail.isEmpty() ? QString() : QStringLiteral(" - %1").arg(detail));
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
    check(QStringLiteral("Refresh Devices populates the card list"),
          m_deckLinkCombo->count() > 0,
          QStringLiteral("%1 devices").arg(m_deckLinkCombo->count()));

    saveConfig();
    check(QStringLiteral("Save Settings writes the config file"), QFileInfo::exists(m_config.configPath), m_config.configPath);

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

    updateDerivedState();
    check(QStringLiteral("Summary and validation panels populate"),
          !m_summaryText->toPlainText().isEmpty() && !m_validationText->toPlainText().isEmpty());

    m_config = originalConfig;
    populateFromConfig();
    syncConfigFromUi();
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

void MainWindow::openLegacyUi() {
    QString errorMessage;
    if (!m_backend.openLegacyUi(&errorMessage)) {
        showError(errorMessage);
    }
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
    syncConfigFromUi();
    m_summaryText->setPlainText(m_config.summary());
    const QStringList issues = m_config.validate();
    QString validationText = issues.isEmpty() ? QStringLiteral("OK") : issues.join(QLatin1Char('\n'));
    if (!m_selfTestResults.isEmpty()) {
        validationText += QStringLiteral("\n--- Auto load self test ---\n") + m_selfTestResults.join(QLatin1Char('\n'));
    }
    m_validationText->setPlainText(validationText);
    m_ffmpegStatusText->setPlainText(QStringLiteral("Use the legacy UI for live FFmpeg probe output.\nCurrent backend entrypoint: %1").arg(m_backend.packageRoot()));
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
    QMessageBox::warning(this, QStringLiteral("vrecord Qt6"), message);
}
