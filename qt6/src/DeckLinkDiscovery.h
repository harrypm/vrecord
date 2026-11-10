#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

struct DeckLinkDeviceInfo {
    QString displayName;
    QString modelName;
    qint64 persistentId = 0;
};

class DeckLinkDiscovery {
public:
    static QVector<DeckLinkDeviceInfo> enumerateDevices(QString* errorMessage = nullptr);
    static bool applyOutputConnection(const QString& modelName, const QString& connectionName, QString* errorMessage = nullptr);
    static bool applyInputConnection(const QString& modelName, const QString& connectionName, QString* errorMessage = nullptr);
    static QString detectInputFormatCode(const QString& modelName, QString* errorMessage = nullptr);

private:
    static bool applyConnection(const QString& modelName, int configurationId, const QString& connectionName, QString* errorMessage);
    static bool applyConnectionInternal(const QString& modelName, std::int64_t configurationId, const QString& connectionName, std::int64_t connectionMask, QString* errorMessage);
};
