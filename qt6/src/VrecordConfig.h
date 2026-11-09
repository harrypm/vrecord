#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

struct VrecordConfig {
    QString configPath;
    QMap<QString, QString> values;

    static VrecordConfig load(const QString& scriptName, const QString& explicitPath = {});
    bool save() const;

    QString value(const QString& key, const QString& fallback = {}) const;
    void setValue(const QString& key, const QString& value);

    QString outputExtension() const;
    QString outputFilename() const;
    QStringList validate() const;
    QString summary() const;
};
