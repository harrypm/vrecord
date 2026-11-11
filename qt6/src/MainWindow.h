#pragma once

#include "BackendController.h"
#include "DeckLinkDiscovery.h"
#include "VrecordConfig.h"
#include <QCheckBox>
#include <QComboBox>

#include <QMainWindow>
#include <QMap>

class QButtonGroup;
class QCloseEvent;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QTabWidget;
class QTimer;
class VideoView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& packageRoot, QWidget* parent = nullptr);
    QStringList runSelfTest();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refreshDeckLinkDevices();
    void saveConfig();
    void startRecord();
    void startPassthrough();
    bool confirmInputStandard();
    void followInputMode(const QString& formatCode);
    void startAudioCheck();
    void startFileOutput();
    void stopAllOutputs();
    void setTestSignalOutput();
    void applyVideoOutputConnection();
    void openLegacyUi();
    void updateDerivedState();

private:
    void buildUi();
    void populateFromConfig();
    void syncConfigFromUi();
    void populateDeckLinkCombo(const QVector<DeckLinkDeviceInfo>& devices, const QString& selectedValue);
    void rebuildConnectorCombos();
    QComboBox* makeCombo(const QStringList& items);
    QCheckBox* makeCheckBox(const QString& label);
    QLineEdit* makeLineEdit();
    void setComboValue(QComboBox* combo, const QString& value);
    void setChoice(QComboBox* combo, const QString& value, const QString& fallback);
    void labelContainerItems(QComboBox* combo);
    static QString choiceValue(const QComboBox* combo);
    void startPreviewQuietly();
    void updatePreviewButton();
    void appendActivity(const QString& line);
    void setRadioValue(const QString& value);
    QString currentRadioValue() const;
    void applySelectedTheme();
    void showError(const QString& message);
    void togglePreview();
    void togglePlayPause();
    void stepPlayback(int frames);
    void seekToSlider();
    void jumpToFrame(qint64 frame);
    // drag and drop onto the folder / file boxes
    struct DropTarget {
        QLineEdit* target = nullptr;
        bool wantsFolder = false;
    };
    void registerDropTarget(QWidget* widget, QLineEdit* target, bool wantsFolder);
    QString dropPathFor(QObject* watched, const class QMimeData* mime) const;
    QMap<QObject*, DropTarget> m_dropTargets;
    void refreshFeeds();
    void updateTransportUi();

    VrecordConfig m_config;
    BackendController m_backend;
    QVector<DeckLinkDeviceInfo> m_devices;
    QTabWidget* m_tabs = nullptr;

    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QLineEdit* m_logDirEdit = nullptr;
    QLineEdit* m_playFileEdit = nullptr;
    QLineEdit* m_technicianEdit = nullptr;
    QLineEdit* m_durationHoursEdit = nullptr;
    QLineEdit* m_durationMinutesEdit = nullptr;
    QLineEdit* m_durationSecondsEdit = nullptr;
    QLineEdit* m_prefixEdit = nullptr;
    QLineEdit* m_suffixEdit = nullptr;
    QComboBox* m_deckLinkCombo = nullptr;
    QComboBox* m_videoInputCombo = nullptr;
    QComboBox* m_audioInputCombo = nullptr;
    QComboBox* m_audioMappingCombo = nullptr;
    QComboBox* m_standardCombo = nullptr;
    QComboBox* m_embedLogsCombo = nullptr;
    QComboBox* m_containerCombo = nullptr;
    QComboBox* m_videoCodecCombo = nullptr;
    QComboBox* m_audioCodecCombo = nullptr;
    QComboBox* m_playbackViewCombo = nullptr;
    QComboBox* m_playbackPassCombo = nullptr;
    QComboBox* m_bitDepthCombo = nullptr;
    QComboBox* m_aspectRatioCombo = nullptr;
    QComboBox* m_dvDeviceCombo = nullptr;
    QComboBox* m_dvContainerCombo = nullptr;
    QComboBox* m_audioDeviceCombo = nullptr;
    QComboBox* m_audioModeCodecCombo = nullptr;
    QComboBox* m_audioChannelsCombo = nullptr;
    QComboBox* m_audioSampleRateCombo = nullptr;
    QComboBox* m_waveformScaleCombo = nullptr;
    QComboBox* m_signalViewCombo = nullptr;
    QComboBox* m_signalIntCombo = nullptr;
    QComboBox* m_monitorAudioCombo = nullptr;
    QComboBox* m_ntsc43SarCombo = nullptr;
    QComboBox* m_ntsc169SarCombo = nullptr;
    QComboBox* m_pal43SarCombo = nullptr;
    QComboBox* m_pal169SarCombo = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_videoOutputDeviceCombo = nullptr;
    QComboBox* m_videoOutputConnectionCombo = nullptr;
    QComboBox* m_relayDeviceCombo = nullptr;
    QComboBox* m_testPatternCombo = nullptr;
    QComboBox* m_testAudioCombo = nullptr;
    QCheckBox* m_enableSuffixCheck = nullptr;
    QCheckBox* m_mp4Check = nullptr;
    QCheckBox* m_frameMd5Check = nullptr;
    QCheckBox* m_timecodeVttCheck = nullptr;
    QCheckBox* m_qcToolsCheck = nullptr;
    QCheckBox* m_dvOptionSCheck = nullptr;
    QCheckBox* m_dvOptionDCheck = nullptr;
    QCheckBox* m_dvOptionTCheck = nullptr;
    QCheckBox* m_dvOptionTcCheck = nullptr;
    QCheckBox* m_ccVttCheck = nullptr;
    QCheckBox* m_ccSrtCheck = nullptr;
    QCheckBox* m_capToMp4Check = nullptr;
    QCheckBox* m_colorSafeCheck = nullptr;
    QCheckBox* m_invertPhase2Check = nullptr;
    QCheckBox* m_invertPhase4Check = nullptr;
    QCheckBox* m_previewRelayCheck = nullptr;
    QCheckBox* m_filePlayAutoCheck = nullptr;
    QCheckBox* m_testSignalOutputCheck = nullptr;
    QLineEdit* m_mediaIdTypeEdit = nullptr;
    QLineEdit* m_webVttExtraEdit = nullptr;
    QPlainTextEdit* m_ffmpegStatusText = nullptr;
    QPlainTextEdit* m_environmentText = nullptr;
    QPlainTextEdit* m_previewInfoText = nullptr;
    QPlainTextEdit* m_outputInfoText = nullptr;
    QPlainTextEdit* m_summaryText = nullptr;
    QPlainTextEdit* m_validationText = nullptr;
    QMap<QString, QRadioButton*> m_inputChoiceRadios;
    QStringList m_selfTestResults;
    bool m_populating = false;
    QComboBox* m_captureChannelsCombo = nullptr;
    QCheckBox* m_ffplayMonitorCheck = nullptr;
    QComboBox* m_ffplayAspectCombo = nullptr;
    class LevelMeter* m_outputMeter = nullptr;
    QComboBox* m_monitorPairCombo = nullptr;
    class LevelMeter* m_previewMeter = nullptr;
    bool m_outputMirrorsInput = false;
    QComboBox* m_outputStandardCombo = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    VideoView* m_inputView = nullptr;
    VideoView* m_outputView = nullptr;
    QPlainTextEdit* m_activityText = nullptr;
    QPushButton* m_recordButton = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_transportStateLabel = nullptr;
    QLineEdit* m_timecodeEdit = nullptr;
    class QSpinBox* m_frameSpin = nullptr;
    QLabel* m_outputNameLabel = nullptr;
    QSlider* m_seekSlider = nullptr;
    QPushButton* m_playPauseButton = nullptr;
    QPushButton* m_previewToggleButton = nullptr;
    QTimer* m_feedTimer = nullptr;
    bool m_sliderDragging = false;
};
