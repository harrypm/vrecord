#pragma once

// RS-422 deck control through the packaged `deckcontrol` tool (Blackmagic
// SDK; the GTK version's Deck Control frame). Commands run as background
// processes, never on the GUI thread; while enabled the deck state and
// timecode are polled every 500 ms (one poll in flight at a time).

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

class DeckControl : public QObject {
    Q_OBJECT
public:
    explicit DeckControl(const QString& packageRoot, QObject* parent = nullptr);

    bool isAvailable() const { return !m_program.isEmpty(); }
    QString program() const { return m_program; }
    bool isEnabled() const { return m_enabled; }

    void setEnabled(bool enabled);
    // play, stop, rewind, fastforward, stepforward, stepback, eject,
    // toggleplaystop, gototimecode <tc>, jog <n>, shuttle <n>
    void command(const QString& verb, const QString& parameter = {});

signals:
    void stateChanged(const QString& state);        // "playing", "No deck (...)", ...
    void timecodeChanged(const QString& timecode);  // "01:00:00:00" or "--:--:--:--"
    void commandFinished(const QString& verb, const QString& message);

private:
    void poll();
    void pollTimecode();
    static QString describe(const QString& output);

    QString m_program;
    QString m_packageRoot;
    QTimer* m_timer = nullptr;
    bool m_enabled = false;
    bool m_pollBusy = false;
};
