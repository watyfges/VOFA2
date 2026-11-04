#include "SettingsStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace vofa::app {
namespace {

QStringList splitPath(const QString& path)
{
    return path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

void setNested(QJsonObject& object, const QStringList& parts, const QJsonValue& value)
{
    if (parts.isEmpty())
        return;
    if (parts.size() == 1) {
        object.insert(parts.front(), value);
        return;
    }
    auto child = object.value(parts.front()).toObject();
    setNested(child, parts.mid(1), value);
    object.insert(parts.front(), child);
}

QJsonValue getNested(const QJsonObject& object, const QStringList& parts)
{
    if (parts.isEmpty())
        return {};
    const auto value = object.value(parts.front());
    if (parts.size() == 1)
        return value;
    return getNested(value.toObject(), parts.mid(1));
}

bool writeJsonAtomically(const QString& path, const QJsonObject& data, QString* error)
{
    const auto dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("cannot create directory: %1").arg(dir);
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }

    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

SettingsStore::SettingsStore(QObject* parent)
    : QObject(parent)
{
    autosaveTimer_.setSingleShot(true);
    autosaveTimer_.setInterval(500);
    connect(&autosaveTimer_, &QTimer::timeout, this, &SettingsStore::onAutosaveTimer);
}

QString SettingsStore::configDir() const
{
    // Portable mode: an empty file named "portable" next to the executable
    // keeps settings beside the app.
    const auto exeDir = QCoreApplication::applicationDirPath();
    if (!exeDir.isEmpty()) {
        const auto marker = exeDir + QStringLiteral("/portable");
        if (QFile::exists(marker)) {
            const auto localData = exeDir + QStringLiteral("/data");
            if (!QDir().mkpath(localData))
                qWarning() << "cannot create portable data directory" << localData;
            return localData;
        }
    }

    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (dir.isEmpty())
        dir = QDir::currentPath() + QStringLiteral("/data");
    return dir;
}

QString SettingsStore::filePath(const QString& name) const
{
    return configDir() + QLatin1Char('/') + name;
}

bool SettingsStore::load()
{
    const auto committedPath = filePath(QStringLiteral("workspace.json"));
    const auto autosavePath = filePath(QStringLiteral("workspace.autosave.json"));
    const auto backupPath = filePath(QStringLiteral("workspace.bak"));

    auto readObject = [](const QString& path, QJsonObject& out) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const auto bytes = file.readAll();
        QJsonParseError error {};
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;
        out = document.object();
        return true;
    };

    QJsonObject committed;
    QJsonObject autosave;
    const bool hasCommitted = readObject(committedPath, committed);
    const bool hasAutosave = readObject(autosavePath, autosave);

    const auto committedTime = QFileInfo(committedPath).lastModified();
    const auto autosaveTime = QFileInfo(autosavePath).lastModified();

    if (hasAutosave && (!hasCommitted || autosaveTime > committedTime)) {
        data_ = autosave;
        recovered_ = true;
        emit recoveredFromAutosave();
    } else if (hasCommitted) {
        data_ = committed;
    } else {
        QJsonObject backup;
        if (readObject(backupPath, backup))
            data_ = backup;
        else
            data_ = {};
    }

    if (!data_.contains(QStringLiteral("schema")))
        data_.insert(QStringLiteral("schema"), 1);

    dirty_ = false;
    return !data_.isEmpty();
}

bool SettingsStore::save()
{
    const auto target = filePath(QStringLiteral("workspace.json"));
    const auto backup = filePath(QStringLiteral("workspace.bak"));

    if (QFile::exists(target)) {
        if (QFile::exists(backup))
            QFile::remove(backup);
        QFile::copy(target, backup);
    }

    QString error;
    if (!writeJsonAtomically(target, data_, &error)) {
        emit saveFailed(error);
        return false;
    }

    QFile::remove(filePath(QStringLiteral("workspace.autosave.json")));
    dirty_ = false;
    autosaveTimer_.stop();
    return true;
}

bool SettingsStore::autosave()
{
    if (!dirty_)
        return true;

    QString error;
    const auto path = filePath(QStringLiteral("workspace.autosave.json"));
    if (!writeJsonAtomically(path, data_, &error)) {
        emit saveFailed(error);
        return false;
    }
    dirty_ = false;
    return true;
}

void SettingsStore::scheduleAutosave()
{
    dirty_ = true;
    autosaveTimer_.start();
}

void SettingsStore::onAutosaveTimer()
{
    (void)autosave();
}

void SettingsStore::setValue(const QString& path, const QJsonValue& value)
{
    const auto parts = splitPath(path);
    if (parts.isEmpty())
        return;
    setNested(data_, parts, value);
    scheduleAutosave();
}

QJsonValue SettingsStore::value(const QString& path, const QJsonValue& fallback) const
{
    const auto value = getNested(data_, splitPath(path));
    return value.isUndefined() ? fallback : value;
}

} // namespace vofa::app
