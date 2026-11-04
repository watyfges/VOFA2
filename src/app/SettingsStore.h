#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>

namespace vofa::app {

// Atomic, crash-tolerant workspace store.
//
// Files (in QStandardPaths::AppConfigLocation):
//   workspace.json          committed state
//   workspace.autosave.json latest edit, written with a short debounce
//   workspace.bak           previous committed state
//
// On startup an autosave newer than the committed file is loaded and
// recovered() returns true.
class SettingsStore final : public QObject {
    Q_OBJECT

public:
    explicit SettingsStore(QObject* parent = nullptr);

    [[nodiscard]] bool load();
    [[nodiscard]] bool save();
    [[nodiscard]] bool autosave();
    void scheduleAutosave();

    [[nodiscard]] bool recovered() const noexcept { return recovered_; }
    [[nodiscard]] QString configDir() const;
    [[nodiscard]] QJsonObject data() const { return data_; }

    void setValue(const QString& path, const QJsonValue& value);
    [[nodiscard]] QJsonValue value(const QString& path,
                                   const QJsonValue& fallback = {}) const;

    [[nodiscard]] QString filePath(const QString& name) const;

signals:
    void recoveredFromAutosave();
    void saveFailed(const QString& message);

private:
    void onAutosaveTimer();

    QJsonObject data_;
    QTimer autosaveTimer_;
    bool dirty_ = false;
    bool recovered_ = false;
};

} // namespace vofa::app
