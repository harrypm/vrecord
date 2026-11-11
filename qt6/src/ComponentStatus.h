#pragma once

// What the package / system provides, for the Config tab's indicator lights
// (the GTK version's "Optional Tools" ticks, extended to the required parts).
// checkComponents() starts each tool once for its version, so it takes a
// second or two: call it off the GUI thread.

#include <QString>
#include <QVector>

struct ComponentStatus {
    QString name;          // "ffmpeg-ma", "deckcontrol", ...
    QString purpose;       // what vrecord uses it for (tooltip)
    bool required = false; // record / passthrough cannot work without it
    bool found = false;
    bool degraded = false; // found, but missing a feature (amber light)
    QString path;          // where it was found
    QString version;       // first version line, or a short status
    QString detail;        // extra note (missing feature, how to get it)
};

QVector<ComponentStatus> checkComponents(const QString& packageRoot);
