#pragma once

#include <QString>
#include <QVector>

struct DeckLinkDeviceInfo {
    QString displayName;
    QString modelName;
    qint64 persistentId = 0;
};

class DeckLinkDiscovery {
public:
    static QVector<DeckLinkDeviceInfo> enumerateDevices(QString* errorMessage = nullptr);
    static bool applyOutputConnection(const QString& modelName, const QString& connectionName, QString* errorMessage = nullptr);
};
