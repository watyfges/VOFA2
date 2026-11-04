#include "AppController.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QSaveFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QRegularExpression>
#include <QScreen>
#include <QSerialPort>
#include <QWindow>
#include <QSerialPortInfo>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#endif

namespace vofa::app {
namespace {

constexpr std::array<const char*, 8> kChannelColors {
    "#1abc9c", "#e67e22", "#3498db", "#9b59b6",
    "#e74c3c", "#f1c40f", "#2ecc71", "#ecf0f1",
};

constexpr double kPi = 3.14159265358979323846;

// Command groups form a tree represented as slash-separated paths
// ("底盘/PID/速度环"). Existing flat names keep working unchanged.
QString normalizeGroupPath(const QString& raw)
{
    QStringList parts;
    for (const auto& part : raw.trimmed().split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const auto cleaned = part.trimmed();
        if (!cleaned.isEmpty())
            parts.push_back(cleaned);
    }
    return parts.join(QLatin1Char('/'));
}

bool isGroupPathOrChild(const QString& path, const QString& parent)
{
    return path == parent
        || path.startsWith(parent + QLatin1Char('/'));
}

QString replaceGroupPrefix(const QString& path,
                           const QString& from,
                           const QString& to)
{
    if (path == from)
        return to;
    if (path.startsWith(from + QLatin1Char('/')))
        return to + path.mid(from.size());
    return path;
}

QStringList groupAncestors(const QString& path)
{
    QStringList result;
    QString current;
    const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); ++i) {
        current = current.isEmpty() ? parts[i] : current + QLatin1Char('/') + parts[i];
        result.push_back(current);
    }
    return result;
}

void fftRadix2(std::vector<float>& real, std::vector<float>& imag)
{
    const std::size_t n = real.size();
    if (n < 2 || (n & (n - 1)) != 0 || imag.size() != n)
        return;

    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(length);
        const double wlenReal = std::cos(angle);
        const double wlenImag = std::sin(angle);
        for (std::size_t i = 0; i < n; i += length) {
            double wReal = 1.0;
            double wImag = 0.0;
            for (std::size_t j = 0; j < length / 2; ++j) {
                const std::size_t u = i + j;
                const std::size_t v = i + j + length / 2;
                const double vr = real[v] * wReal - imag[v] * wImag;
                const double vi = real[v] * wImag + imag[v] * wReal;
                real[v] = static_cast<float>(real[u] - vr);
                imag[v] = static_cast<float>(imag[u] - vi);
                real[u] = static_cast<float>(real[u] + vr);
                imag[u] = static_cast<float>(imag[u] + vi);
                const double nextReal = wReal * wlenReal - wImag * wlenImag;
                wImag = wReal * wlenImag + wImag * wlenReal;
                wReal = nextReal;
            }
        }
    }
}

int screenAvailableWidth()
{
    const auto* screen = QGuiApplication::primaryScreen();
    return screen != nullptr ? screen->availableGeometry().width() : 0;
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , transport_(&store_, this)
{
    imageProvider_ = new ImageProvider();
    QObject::connect(imageProvider_, &ImageProvider::imageDecoded, this,
                     [this](int imageId, quint64 sequence) {
                         const auto info = pendingImageInfo_.take(sequence);
                         latestImageId_ = imageId;
                         ++imageCount_;
                         ++imageRevision_;
                         if (!info.isEmpty())
                             imageInfo_ = info;
                         emit imageUpdated();
                     });
    statsWindowStartMs_ = QDateTime::currentMSecsSinceEpoch();

    connect(&transport_, &transport::TransportController::stateChanged,
            this, &AppController::onStateChanged);
    connect(&transport_, &transport::TransportController::received,
            this, &AppController::onReceived);
    connect(&transport_, &transport::TransportController::clientsChanged,
            this, &AppController::onClientsChanged);
    connect(&transport_, &transport::TransportController::streamReset,
            this, &AppController::onStreamReset);
    connect(&transport_, &transport::TransportController::sendFailed,
            this, &AppController::onSendFailed);
    connect(&transport_, &transport::TransportController::errorOccurred,
            this, &AppController::onErrorOccurred);
    connect(&transport_, &transport::TransportController::imageReceived,
            this, &AppController::onImageReceived);
    connect(&transport_, &transport::TransportController::listening, this,
            [this](quint16 port) {
                listeningPort_ = port;
                emit connectionStateChanged();
            });

    // ~60 Hz view refresh. The scene-graph plot uploads only line vertices,
    // so a 16 ms tick stays cheap while matching the 60 FPS target.
    updateTimer_.setInterval(16);
    connect(&updateTimer_, &QTimer::timeout, this, &AppController::onUpdateTimer);
    updateTimer_.start();

    oneClickTimer_.setSingleShot(true);
    connect(&oneClickTimer_, &QTimer::timeout, this, &AppController::sendNextOneClickStep);

    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(1500);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this]() {
        if (autoReconnect_ && lastConfigValid_ && !userClosed_)
            transport_.open(lastConfig_);
    });

    (void)settings_.load();
    oneClickIntervalMs_ = std::clamp(
        settings_.value(QStringLiteral("oneClick/intervalMs"), 100).toInt(), 10, 5000);
    sendOnMove_ = settings_.value(QStringLiteral("slider/sendOnMove"), true).toBool();
    loadCommandGroups();
    loadOneClickBindings();
    waveformPaused_ = settings_.value(QStringLiteral("waveform/paused"), false).toBool();
    waveformMode_ = std::clamp(
        settings_.value(QStringLiteral("waveform/mode"), 0).toInt(), 0, 3);
    triggerChannel_ = std::clamp(
        settings_.value(QStringLiteral("trigger/channel"), 0).toInt(), 0,
        static_cast<int>(limits::kMaxChannels) - 1);
    triggerLevel_ = settings_.value(QStringLiteral("trigger/level"), 0.0).toDouble();
    triggerEdge_ = std::clamp(
        settings_.value(QStringLiteral("trigger/edge"), 0).toInt(), 0, 1);
    triggerCaptureTimer_.setSingleShot(true);
    triggerCaptureTimer_.setInterval(300);
    connect(&triggerCaptureTimer_, &QTimer::timeout, this, [this]() {
        triggerReady_ = true;
        ++triggerRevision_;
        emit triggerChanged();
    });
    replayRate_ = std::clamp(
        settings_.value(QStringLiteral("replay/rate"), 1000.0).toDouble(1000.0),
        1.0, 1000000.0);
    replayTimer_.setInterval(50);
    connect(&replayTimer_, &QTimer::timeout, this, [this]() {
        if (!replayActive_)
            return;
        const auto rows = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::llround(replayRate_ * 0.05)));
        feedReplayFrames(rows);
        if (replayPosition_ >= replayRows_) {
            replayActive_ = false;
            replayTimer_.stop();
        }
        emit replayChanged();
    });
    receivePaused_ = settings_.value(QStringLiteral("receive/paused"), false).toBool();
    receiveTimestamps_ =
        settings_.value(QStringLiteral("receive/timestamps"), false).toBool();
    receiveHex_ = settings_.value(QStringLiteral("receive/hex"), false).toBool();
    receiveAnsi_ = settings_.value(QStringLiteral("receive/ansi"), false).toBool();
    receiveFilter_ = settings_.value(QStringLiteral("receive/filter")).toString();
    receiveFilterRegex_ =
        settings_.value(QStringLiteral("receive/filterRegex"), false).toBool();
    receiveFilterDirection_ = std::clamp(
        settings_.value(QStringLiteral("receive/filterDirection"), 0).toInt(), 0, 2);
    receiveTxEcho_ =
        settings_.value(QStringLiteral("receive/txEcho"), false).toBool();
    rebuildReceiveFilterExpression();
    receiveEncoding_ = std::clamp(
        settings_.value(QStringLiteral("receive/encoding"), 0).toInt(), 0, 1);
    textDecoder_ = QStringDecoder(receiveEncoding_ == 0 ? QStringConverter::Utf8
                                                       : QStringConverter::System);
    autoReconnect_ = settings_.value(QStringLiteral("connection/autoReconnect"), false).toBool();
    waveformWindowSamples_ = std::clamp(
        settings_.value(QStringLiteral("waveform/windowSamples"), 20000).toInt(),
        0, 262144);
    waveformWindowSeconds_ = std::clamp(
        settings_.value(QStringLiteral("waveform/windowSeconds"), 10.0).toDouble(10.0),
        0.0, 3600.0);

    tcpClientHost_ = settings_.value(QStringLiteral("connection/tcpClient/host"),
                                     QStringLiteral("127.0.0.1")).toString();
    tcpClientPort_ = settings_.value(QStringLiteral("connection/tcpClient/port"), 1346).toInt();
    tcpClientBanner_ = settings_.value(QStringLiteral("connection/tcpClient/banner"),
                                       QStringLiteral("plot0")).toString();
    tcpServerPort_ = settings_.value(QStringLiteral("connection/tcpServer/port"), 1347).toInt();

    const auto savedPreset = settings_.value(QStringLiteral("ui/preset"), -1).toInt();
    uiDensity_ = std::clamp(
        settings_.value(QStringLiteral("ui/density"), 1.0).toDouble(1.0), 0.60, 1.40);
    uiFontScale_ = std::clamp(
        settings_.value(QStringLiteral("ui/fontScale"), 1.0).toDouble(1.0), 0.60, 1.40);

    if (savedPreset >= 0) {
        uiPreset_ = std::clamp(savedPreset, 0, 2);
        // Re-derive from the preset so preset improvements apply to existing
        // installations instead of freezing old stored values.
        switch (uiPreset_) {
        case 0:
            uiDensity_ = 0.70;
            uiFontScale_ = 0.75;
            break;
        case 2:
            uiDensity_ = 1.25;
            uiFontScale_ = 1.18;
            break;
        default:
            uiDensity_ = 1.0;
            uiFontScale_ = 1.0;
            break;
        }
    } else if (!settings_.value(QStringLiteral("ui/density")).isUndefined()
               || !settings_.value(QStringLiteral("ui/fontScale")).isUndefined()) {
        // User-adjusted (custom) density; keep both values.
        uiPreset_ = -1;
    } else {
        const auto* screen = QGuiApplication::primaryScreen();
        const auto ratio = screen != nullptr ? screen->devicePixelRatio() : 1.0;
        uiPreset_ = ratio >= 1.4 ? 0 : 1;
        if (uiPreset_ == 0) {
            uiDensity_ = 0.70;
            uiFontScale_ = 0.75;
        }
    }

    uiTheme_ = std::clamp(
        settings_.value(QStringLiteral("ui/theme"), 0).toInt(), 0, 2);

    showConnectionPanel_ =
        settings_.value(QStringLiteral("ui/showConnectionPanel"), true).toBool();
    showSliderPanel_ = settings_.value(
        QStringLiteral("ui/showSliderPanel"),
        screenAvailableWidth() >= 1200).toBool();

    loadRecentSends();
    sendFilter_ = settings_.value(QStringLiteral("send/filter")).toString();
    loadActionBindings();

    // Remote tuning: restore the LAN server settings. The listener only starts
    // when the user has left the option enabled.
    remoteServerName_ = settings_
                            .value(QStringLiteral("remote/name"),
                                   QSysInfo::machineHostName())
                            .toString()
                            .trimmed()
                            .left(64);
    if (remoteServerName_.isEmpty())
        remoteServerName_ = QStringLiteral("VOFA2");
    remotePort_ = static_cast<quint16>(std::clamp(
        settings_.value(QStringLiteral("remote/port"),
                        static_cast<int>(remote::kDefaultControlPort))
            .toInt(),
        1024, 65535));
    remotePin_ = settings_.value(QStringLiteral("remote/pin")).toString().trimmed().left(12);
    remoteEnabled_ = settings_.value(QStringLiteral("remote/enabled"), false).toBool();

    remoteStateTimer_.setSingleShot(true);
    remoteStateTimer_.setInterval(80);
    connect(&remoteStateTimer_, &QTimer::timeout, this, &AppController::broadcastRemoteState);

    remoteValueTimer_.setSingleShot(true);
    remoteValueTimer_.setInterval(33);
    connect(&remoteValueTimer_, &QTimer::timeout, this, &AppController::flushRemoteValues);

    // A connection storm must not rebuild the QML client list thousands of
    // times per second; coalesce the refresh.
    remoteClientsTimer_.setSingleShot(true);
    remoteClientsTimer_.setInterval(120);
    connect(&remoteClientsTimer_, &QTimer::timeout, this, [this]() {
        refreshRemoteClients();
        updateRemoteStatus();
    });
    connect(&remote_, &remote::RemoteControlServer::clientsChanged, this, [this]() {
        if (!remoteClientsTimer_.isActive())
            remoteClientsTimer_.start();
    });
    connect(&remote_, &remote::RemoteControlServer::stateRequested, this,
            &AppController::sendRemoteStateTo);
    connect(&remote_, &remote::RemoteControlServer::valueRequested, this,
            [this](quint64 session, quint64 bindingId, double value, bool commit, bool exact) {
                onRemoteValue(session, bindingId, value, commit, exact);
            });
    connect(&remote_, &remote::RemoteControlServer::actionRequested, this,
            [this](quint64 session, quint64 actionId, bool on, bool trigger) {
                onRemoteAction(session, actionId, on, trigger);
            });
    connect(&remote_, &remote::RemoteControlServer::inclusionRequested, this,
            [this](quint64 session, quint64 bindingId, bool included) {
                onRemoteInclusion(session, bindingId, included);
            });
    connect(&remote_, &remote::RemoteControlServer::oneClickRequested, this,
            [this](quint64 session, const QString& group, int intervalMs) {
                onRemoteOneClick(session, group, intervalMs);
            });
    connect(&remote_, &remote::RemoteControlServer::oneClickStopRequested, this,
            [this](quint64) { cancelOneClickSend(); });
    connect(&remote_, &remote::RemoteControlServer::logLine, this,
            [](const QString& line) { qInfo().noquote() << "[remote]" << line; });

    if (remoteEnabled_)
        startRemoteServer();
    else
        updateRemoteStatus();

    applyUiFont();

    const auto savedProtocol =
        settings_.value(QStringLiteral("connection/protocol"), QStringLiteral("JustFloat"))
            .toString();
    setProtocol(savedProtocol);

    refreshSerialPorts();
}

AppController::~AppController()
{
    updateTimer_.stop();
    oneClickTimer_.stop();
    remoteStateTimer_.stop();
    remoteValueTimer_.stop();
    remoteClientsTimer_.stop();
    remote_.stop();
    transport_.close();
    (void)settings_.save();
    if (ownsImageProvider_)
        delete imageProvider_;
}

QString AppController::connectionState() const
{
    switch (state_) {
    case transport::ConnectionState::Disconnected:
        return QStringLiteral("未连接");
    case transport::ConnectionState::Opening:
        return QStringLiteral("连接中");
    case transport::ConnectionState::Connected:
        if (transportKind_ == 2 && activeClientId_ == 0 && listeningPort_ != 0)
            return QStringLiteral("监听中 :%1").arg(listeningPort_);
        return QStringLiteral("已连接");
    case transport::ConnectionState::Closing:
        return QStringLiteral("关闭中");
    case transport::ConnectionState::Error:
        return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

QString AppController::protocol() const
{
    return protocol_;
}

void AppController::setProtocol(const QString& protocol)
{
    const auto text = protocol.trimmed();
    protocol::ProtocolKind kind = protocol::ProtocolKind::JustFloat;
    QString canonical = QStringLiteral("JustFloat");

    if (text.compare(QStringLiteral("RawData"), Qt::CaseInsensitive) == 0) {
        kind = protocol::ProtocolKind::RawData;
        canonical = QStringLiteral("RawData");
    } else if (text.compare(QStringLiteral("FireWater"), Qt::CaseInsensitive) == 0) {
        kind = protocol::ProtocolKind::FireWater;
        canonical = QStringLiteral("FireWater");
    }

    if (protocolKind_ == kind && protocol_ == canonical)
        return;

    protocolKind_ = kind;
    protocol_ = canonical;
    settings_.setValue(QStringLiteral("connection/protocol"), canonical);
    emit protocolChanged();
}

void AppController::setOneClickIntervalMs(int value)
{
    const auto clamped = std::clamp(value, 10, 5000);
    if (oneClickIntervalMs_ == clamped)
        return;

    oneClickIntervalMs_ = clamped;
    settings_.setValue(QStringLiteral("oneClick/intervalMs"), clamped);
    emit oneClickIntervalMsChanged();
    scheduleRemoteState();
}

void AppController::setUiDensity(double value)
{
    const auto clamped = std::clamp(value, 0.60, 1.40);
    if (qFuzzyCompare(uiDensity_, clamped))
        return;

    uiDensity_ = clamped;
    uiPreset_ = -1;
    settings_.setValue(QStringLiteral("ui/density"), uiDensity_);
    settings_.setValue(QStringLiteral("ui/preset"), uiPreset_);
    emit uiDensityChanged();
    emit uiPresetChanged();
}

void AppController::setUiFontScale(double value)
{
    const auto clamped = std::clamp(value, 0.60, 1.40);
    if (qFuzzyCompare(uiFontScale_, clamped))
        return;

    uiFontScale_ = clamped;
    uiPreset_ = -1;
    settings_.setValue(QStringLiteral("ui/fontScale"), uiFontScale_);
    settings_.setValue(QStringLiteral("ui/preset"), uiPreset_);
    applyUiFont();
    emit uiFontScaleChanged();
    emit uiPresetChanged();
}

void AppController::setUiPreset(int preset)
{
    preset = std::clamp(preset, 0, 2);
    double density = 1.0;
    double fontScale = 1.0;

    switch (preset) {
    case 0:
        density = 0.70;
        fontScale = 0.75;
        break;
    case 1:
        density = 1.0;
        fontScale = 1.0;
        break;
    case 2:
        density = 1.25;
        fontScale = 1.18;
        break;
    }

    uiPreset_ = preset;
    uiDensity_ = density;
    uiFontScale_ = fontScale;
    settings_.setValue(QStringLiteral("ui/preset"), preset);
    settings_.setValue(QStringLiteral("ui/density"), density);
    settings_.setValue(QStringLiteral("ui/fontScale"), fontScale);
    applyUiFont();
    emit uiPresetChanged();
    emit uiDensityChanged();
    emit uiFontScaleChanged();
}

void AppController::setUiTheme(int theme)
{
    theme = std::clamp(theme, 0, 2);
    if (uiTheme_ == theme)
        return;
    uiTheme_ = theme;
    settings_.setValue(QStringLiteral("ui/theme"), theme);
    emit uiThemeChanged();
}

void AppController::setShowConnectionPanel(bool visible)
{
    if (showConnectionPanel_ == visible)
        return;
    showConnectionPanel_ = visible;
    settings_.setValue(QStringLiteral("ui/showConnectionPanel"), visible);
    emit panelsChanged();
}

void AppController::setShowSliderPanel(bool visible)
{
    if (showSliderPanel_ == visible)
        return;
    showSliderPanel_ = visible;
    settings_.setValue(QStringLiteral("ui/showSliderPanel"), visible);
    emit panelsChanged();
}

void AppController::loadCommandGroups()
{
    commandGroups_.clear();
    const auto groups = settings_.value(QStringLiteral("commands/groups")).toArray();
    for (const auto& value : groups) {
        const auto name = normalizeGroupPath(value.toString());
        if (name.isEmpty() || commandGroups_.contains(name))
            continue;
        // Older files may contain a child path without its parent.
        for (const auto& ancestor : groupAncestors(name)) {
            if (!commandGroups_.contains(ancestor))
                commandGroups_.push_back(ancestor);
        }
        commandGroups_.push_back(name);
    }
    if (commandGroups_.isEmpty())
        commandGroups_.push_back(QStringLiteral("默认"));

    const auto savedName =
        normalizeGroupPath(settings_.value(QStringLiteral("commands/currentGroupName"))
                               .toString());
    const int byName = savedName.isEmpty()
        ? -1
        : static_cast<int>(commandGroups_.indexOf(savedName));
    const auto savedIndex =
        settings_.value(QStringLiteral("commands/currentGroup"), 0).toInt();
    currentGroup_ = std::clamp(byName >= 0 ? byName : savedIndex,
                               0, static_cast<int>(commandGroups_.size()) - 1);
}

void AppController::persistCommandGroups()
{
    QJsonArray groups;
    for (const auto& name : commandGroups_)
        groups.append(name);
    settings_.setValue(QStringLiteral("commands/groups"), QJsonValue(groups));
    settings_.setValue(QStringLiteral("commands/currentGroup"), currentGroup_);
    settings_.setValue(QStringLiteral("commands/currentGroupName"), currentGroupName());
}

bool AppController::hasCommandGroup(const QString& name) const
{
    return commandGroups_.contains(name);
}

AppController::EditSnapshot AppController::currentEditSnapshot() const
{
    EditSnapshot snapshot;
    snapshot.oneClick = settings_.value(QStringLiteral("oneClick/items")).toArray();
    snapshot.actions = settings_.value(QStringLiteral("actions/items")).toArray();
    snapshot.groups = settings_.value(QStringLiteral("commands/groups")).toArray();
    snapshot.currentGroup = currentGroup_;
    return snapshot;
}

void AppController::captureEdit()
{
    undoStack_.push_back(currentEditSnapshot());
    constexpr std::size_t kMaxUndoDepth = 50;
    if (undoStack_.size() > kMaxUndoDepth)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
    emit editHistoryChanged();
}

void AppController::restoreEditSnapshot(const EditSnapshot& snapshot)
{
    settings_.setValue(QStringLiteral("oneClick/items"), QJsonValue(snapshot.oneClick));
    settings_.setValue(QStringLiteral("actions/items"), QJsonValue(snapshot.actions));
    settings_.setValue(QStringLiteral("commands/groups"), QJsonValue(snapshot.groups));
    settings_.setValue(QStringLiteral("commands/currentGroup"),
                       QJsonValue(snapshot.currentGroup));

    loadCommandGroups();
    if (!commandGroups_.isEmpty()) {
        currentGroup_ = std::clamp(
            snapshot.currentGroup, 0, static_cast<int>(commandGroups_.size()) - 1);
    }
    // Do not re-seed demo commands when undoing back to an empty workspace.
    loadOneClickBindings(false);
    loadActionBindings();
    persistCommandGroups();
    persistOneClickBindings();
    persistActionBindings();
    emit commandGroupsChanged();
    emit currentGroupChanged();
    emit editHistoryChanged();
}

void AppController::undo()
{
    if (undoStack_.empty())
        return;
    auto snapshot = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(currentEditSnapshot());
    restoreEditSnapshot(snapshot);
}

void AppController::redo()
{
    if (redoStack_.empty())
        return;
    auto snapshot = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(currentEditSnapshot());
    restoreEditSnapshot(snapshot);
}

void AppController::clearEditHistory()
{
    if (undoStack_.empty() && redoStack_.empty())
        return;
    undoStack_.clear();
    redoStack_.clear();
    emit editHistoryChanged();
}

void AppController::setCurrentGroup(int index)
{
    if (commandGroups_.isEmpty())
        return;
    index = std::clamp(index, 0, static_cast<int>(commandGroups_.size()) - 1);
    if (index == currentGroup_)
        return;

    currentGroup_ = index;
    persistCommandGroups();
    rebuildOneClickBindings();
    rebuildActionBindings();
    emit currentGroupChanged();
    scheduleRemoteState();
}

bool AppController::addCommandGroup(const QString& name)
{
    const auto trimmed = normalizeGroupPath(name);
    if (trimmed.isEmpty() || trimmed.size() > 128 || hasCommandGroup(trimmed))
        return false;

    captureEdit();
    // "底盘/PID" creates the missing "底盘" parent as well.
    for (const auto& ancestor : groupAncestors(trimmed)) {
        if (!commandGroups_.contains(ancestor))
            commandGroups_.push_back(ancestor);
    }
    commandGroups_.push_back(trimmed);
    persistCommandGroups();
    emit commandGroupsChanged();
    return true;
}

bool AppController::renameCommandGroup(int index, const QString& name)
{
    const auto trimmed = normalizeGroupPath(name);
    if (index < 0 || index >= commandGroups_.size() || trimmed.isEmpty()
        || trimmed.size() > 128) {
        return false;
    }

    const auto oldName = commandGroups_[index];
    if (oldName == trimmed)
        return true;
    // Moving a group under itself would create a cycle.
    if (isGroupPathOrChild(trimmed, oldName))
        return false;

    // The renamed subtree must not collide with groups outside it.
    QSet<QString> outside;
    for (const auto& group : commandGroups_) {
        if (!isGroupPathOrChild(group, oldName))
            outside.insert(group);
    }
    QStringList renamed;
    for (const auto& group : commandGroups_) {
        if (isGroupPathOrChild(group, oldName))
            renamed.push_back(replaceGroupPrefix(group, oldName, trimmed));
    }
    for (const auto& group : renamed) {
        if (outside.contains(group))
            return false;
    }

    const auto currentName = currentGroupName();
    captureEdit();

    // Rebuild the flat path list: untouched groups first, then the ancestors
    // and the renamed subtree.
    QStringList rebuilt;
    QSet<QString> seen;
    const auto appendUnique = [&rebuilt, &seen](const QString& group) {
        if (!group.isEmpty() && !seen.contains(group)) {
            seen.insert(group);
            rebuilt.push_back(group);
        }
    };
    for (const auto& group : commandGroups_) {
        if (!isGroupPathOrChild(group, oldName))
            appendUnique(group);
    }
    for (const auto& ancestor : groupAncestors(trimmed))
        appendUnique(ancestor);
    for (const auto& group : renamed)
        appendUnique(group);
    commandGroups_ = rebuilt;

    for (auto& binding : commandStore_.bindings()) {
        const auto group = QString::fromStdString(binding.group);
        if (isGroupPathOrChild(group, oldName))
            binding.group = replaceGroupPrefix(group, oldName, trimmed).toStdString();
    }
    for (auto& action : actions_) {
        if (isGroupPathOrChild(action.group, oldName))
            action.group = replaceGroupPrefix(action.group, oldName, trimmed);
    }

    auto newCurrent = currentName;
    if (isGroupPathOrChild(currentName, oldName))
        newCurrent = replaceGroupPrefix(currentName, oldName, trimmed);
    const auto currentIndex = commandGroups_.indexOf(newCurrent);
    currentGroup_ = currentIndex >= 0
        ? currentIndex
        : std::clamp(currentGroup_, 0, static_cast<int>(commandGroups_.size()) - 1);

    persistCommandGroups();
    persistOneClickBindings();
    persistActionBindings();
    rebuildOneClickBindings();
    rebuildActionBindings();
    emit commandGroupsChanged();
    emit currentGroupChanged();
    return true;
}

bool AppController::removeCommandGroup(int index)
{
    if (commandGroups_.size() <= 1 || index < 0 || index >= commandGroups_.size())
        return false;

    const auto removed = commandGroups_[index];
    const auto currentName = currentGroupName();

    // Deleting a parent removes its whole subtree; contents move to the first
    // surviving group rather than being dropped.
    QSet<QString> removedNames;
    QStringList remaining;
    for (const auto& group : commandGroups_) {
        if (isGroupPathOrChild(group, removed))
            removedNames.insert(group);
        else
            remaining.push_back(group);
    }
    if (remaining.isEmpty())
        remaining.push_back(QStringLiteral("默认"));

    captureEdit();
    const auto fallback = remaining.first();
    commandGroups_ = remaining;

    for (auto& binding : commandStore_.bindings()) {
        if (removedNames.contains(QString::fromStdString(binding.group)))
            binding.group = fallback.toStdString();
    }
    for (auto& action : actions_) {
        if (removedNames.contains(action.group))
            action.group = fallback;
    }

    const auto newCurrent = removedNames.contains(currentName) ? fallback
                                                               : currentName;
    const auto currentIndex = commandGroups_.indexOf(newCurrent);
    currentGroup_ = currentIndex >= 0
        ? currentIndex
        : std::clamp(currentGroup_, 0, static_cast<int>(commandGroups_.size()) - 1);

    persistCommandGroups();
    persistOneClickBindings();
    persistActionBindings();
    rebuildOneClickBindings();
    rebuildActionBindings();
    emit commandGroupsChanged();
    emit currentGroupChanged();
    return true;
}

bool AppController::moveCurrentGroupContents(int targetIndex)
{
    if (targetIndex < 0 || targetIndex >= commandGroups_.size()
        || targetIndex == currentGroup_) {
        return false;
    }

    const auto source = currentGroupName();
    const auto target = commandGroups_[targetIndex];
    if (isGroupPathOrChild(target, source))
        return false; // do not move a group's contents into its own subtree

    captureEdit();
    for (auto& binding : commandStore_.bindings()) {
        if (QString::fromStdString(binding.group) == source)
            binding.group = target.toStdString();
    }
    for (auto& action : actions_) {
        if (action.group == source)
            action.group = target;
    }

    currentGroup_ = targetIndex;
    persistCommandGroups();
    persistOneClickBindings();
    persistActionBindings();
    rebuildOneClickBindings();
    rebuildActionBindings();
    emit currentGroupChanged();
    return true;
}

void AppController::applyUiFont()
{
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr)
        return;

    auto font = QGuiApplication::font();
    font.setPointSizeF(9.0 * uiFontScale_);
    QGuiApplication::setFont(font);
}

QVariantList AppController::channelSettings() const
{
    QVariantList result;
    const auto count = std::min<std::size_t>(
        channelConfigs_.size(), static_cast<std::size_t>(std::max(channelCount_, 0)));
    result.reserve(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const auto& config = channelConfigs_[i];
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(i));
        item.insert(QStringLiteral("name"), config.name);
        item.insert(QStringLiteral("color"), config.color);
        item.insert(QStringLiteral("visible"), config.visible);
        item.insert(QStringLiteral("scale"), config.scale);
        item.insert(QStringLiteral("offset"), config.offset);
        item.insert(QStringLiteral("decimals"), config.decimals);
        item.insert(QStringLiteral("autoScale"), config.autoScale);
        item.insert(QStringLiteral("rangeMin"), config.rangeMin);
        item.insert(QStringLiteral("rangeMax"), config.rangeMax);
        result.push_back(item);
    }
    return result;
}

void AppController::ensureChannelConfigs(std::size_t count)
{
    count = std::min<std::size_t>(count, limits::kMaxChannels);

    if (channelConfigs_.empty() && count > 0) {
        const auto saved = settings_.value(QStringLiteral("channels")).toArray();
        for (std::size_t i = 0; i < count; ++i) {
            ChannelConfig config;
            config.name = QStringLiteral("CH%1").arg(i);
            config.color = QString::fromLatin1(kChannelColors[i % kChannelColors.size()]);
            if (i < static_cast<std::size_t>(saved.size())) {
                const auto item = saved[static_cast<int>(i)].toObject();
                config.name = item.value(QStringLiteral("name")).toString(config.name);
                config.color = item.value(QStringLiteral("color")).toString(config.color);
                config.visible = item.value(QStringLiteral("visible")).toBool(true);
                config.scale = item.value(QStringLiteral("scale")).toDouble(1.0);
                config.offset = item.value(QStringLiteral("offset")).toDouble(0.0);
                config.decimals = item.value(QStringLiteral("decimals")).toInt(3);
                config.autoScale = item.value(QStringLiteral("autoScale")).toBool(true);
                config.rangeMin = item.value(QStringLiteral("rangeMin")).toDouble(-5.0);
                config.rangeMax = item.value(QStringLiteral("rangeMax")).toDouble(5.0);
            }
            channelConfigs_.push_back(std::move(config));
        }
    } else {
        channelConfigs_.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto& config = channelConfigs_[i];
            if (config.name.isEmpty())
                config.name = QStringLiteral("CH%1").arg(i);
            if (config.color.isEmpty())
                config.color = QString::fromLatin1(kChannelColors[i % kChannelColors.size()]);
        }
    }

    emit channelSettingsChanged();
}

void AppController::persistChannelConfigs()
{
    QJsonArray channels;
    for (const auto& config : channelConfigs_) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), config.name);
        item.insert(QStringLiteral("color"), config.color);
        item.insert(QStringLiteral("visible"), config.visible);
        item.insert(QStringLiteral("scale"), config.scale);
        item.insert(QStringLiteral("offset"), config.offset);
        item.insert(QStringLiteral("decimals"), config.decimals);
        item.insert(QStringLiteral("autoScale"), config.autoScale);
        item.insert(QStringLiteral("rangeMin"), config.rangeMin);
        item.insert(QStringLiteral("rangeMax"), config.rangeMax);
        channels.append(item);
    }
    settings_.setValue(QStringLiteral("channels"), QJsonValue(channels));
}

void AppController::clearWaveform()
{
    stopReplay();
    store_.clearHistories();
    lastFramesStored_ = 0;
    waveformPanSamples_ = 0;
    replayPosition_ = 0;
    clearTrigger();
    emit replayChanged();
    ++waveformRevision_;
    emit waveformPanChanged();
    emit waveformChanged();
}

void AppController::setWaveformWindowSamples(int samples)
{
    samples = std::clamp(samples, 0, 262144);
    if (samples == waveformWindowSamples_)
        return;

    waveformWindowSamples_ = samples;
    waveformWindowSeconds_ = 0.0;
    settings_.setValue(QStringLiteral("waveform/windowSeconds"), 0.0);
    const bool hadPan = waveformPanSamples_ != 0;
    waveformPanSamples_ = 0;
    settings_.setValue(QStringLiteral("waveform/windowSamples"), samples);
    ++waveformRevision_;
    emit waveformWindowChanged();
    if (hadPan)
        emit waveformPanChanged();
    emit waveformChanged();
}

int AppController::effectiveWindowSamples() const
{
    if (waveformWindowSeconds_ > 0.0) {
        const double rate = samplesPerSecond_ > 1.0 ? samplesPerSecond_ : 1000.0;
        const auto samples = static_cast<long long>(
            std::llround(waveformWindowSeconds_ * rate));
        return static_cast<int>(std::clamp<long long>(samples, 100, 262144));
    }
    // Manual sample-window mode (used by tests); 0 means "all samples".
    return std::clamp(waveformWindowSamples_, 0, 262144);
}

void AppController::setWaveformWindowSeconds(double seconds)
{
    seconds = std::clamp(seconds, 0.0, 3600.0);
    if (std::abs(waveformWindowSeconds_ - seconds) < 1.0e-9)
        return;

    waveformWindowSeconds_ = seconds;
    if (seconds == 0.0)
        waveformWindowSamples_ = 0; // "all data"
    const bool hadPan = waveformPanSamples_ != 0;
    waveformPanSamples_ = 0;
    settings_.setValue(QStringLiteral("waveform/windowSeconds"), seconds);
    if (seconds == 0.0)
        settings_.setValue(QStringLiteral("waveform/windowSamples"), 0);
    ++waveformRevision_;
    emit waveformWindowChanged();
    if (hadPan)
        emit waveformPanChanged();
    emit waveformChanged();
}

void AppController::setWaveformMode(int mode)
{
    mode = std::clamp(mode, 0, 3);
    if (waveformMode_ == mode)
        return;
    waveformMode_ = mode;
    settings_.setValue(QStringLiteral("waveform/mode"), mode);
    ++waveformRevision_;
    emit waveformModeChanged();
    emit waveformChanged();
}

void AppController::setTriggerChannel(int channel)
{
    channel = std::clamp(channel, 0, static_cast<int>(limits::kMaxChannels) - 1);
    if (triggerChannel_ == channel)
        return;
    triggerChannel_ = channel;
    settings_.setValue(QStringLiteral("trigger/channel"), channel);
    emit triggerChanged();
}

void AppController::setTriggerLevel(double level)
{
    if (!std::isfinite(level) || qFuzzyCompare(triggerLevel_, level))
        return;
    triggerLevel_ = level;
    settings_.setValue(QStringLiteral("trigger/level"), level);
    emit triggerChanged();
}

void AppController::setTriggerEdge(int edge)
{
    edge = std::clamp(edge, 0, 1);
    if (triggerEdge_ == edge)
        return;
    triggerEdge_ = edge;
    settings_.setValue(QStringLiteral("trigger/edge"), edge);
    emit triggerChanged();
}

void AppController::armTrigger()
{
    triggerArmed_ = true;
    triggerReady_ = false;
    triggerSample_ = 0;
    triggerCaptureTimer_.stop();
    const auto* history =
        store_.channel(static_cast<std::size_t>(triggerChannel_));
    triggerScanAbsolute_ = history != nullptr ? history->absoluteEnd() : 0;
    ++triggerRevision_;
    emit triggerChanged();
}

void AppController::clearTrigger()
{
    triggerCaptureTimer_.stop();
    triggerArmed_ = false;
    triggerReady_ = false;
    triggerSample_ = 0;
    triggerScanAbsolute_ = 0;
    ++triggerRevision_;
    emit triggerChanged();
}

QString AppController::triggerStatus() const
{
    if (triggerArmed_)
        return QStringLiteral("等待触发…");
    if (triggerSample_ > 0 && !triggerReady_)
        return QStringLiteral("已触发，采集后段数据…");
    if (triggerReady_)
        return QStringLiteral("已捕获（第 %1 次）").arg(triggerCount_);
    return QStringLiteral("未启用");
}

void AppController::checkTrigger()
{
    if (!triggerArmed_)
        return;

    const auto* history =
        store_.channel(static_cast<std::size_t>(triggerChannel_));
    if (history == nullptr)
        return;

    const auto end = history->absoluteEnd();
    if (end <= triggerScanAbsolute_)
        return;

    // Only scan the newest window. A backlog this large means the UI fell
    // behind; resyncing at the newest samples is better than stalling.
    auto start = triggerScanAbsolute_;
    constexpr std::uint64_t kMaxScanSamples = 4096;
    if (end - start > kMaxScanSamples)
        start = end - kMaxScanSamples;

    const auto copyStart = start > 0 ? start - 1 : 0;
    const auto count = static_cast<std::size_t>(end - copyStart);
    std::vector<float> samples(count);
    if (!history->copyRange(copyStart, count, samples.data())) {
        triggerScanAbsolute_ = end;
        return;
    }

    for (std::size_t i = 1; i < samples.size(); ++i) {
        const auto previous = samples[i - 1];
        const auto current = samples[i];
        const bool crossed = triggerEdge_ == 0
            ? (previous < triggerLevel_ && current >= triggerLevel_)
            : (previous > triggerLevel_ && current <= triggerLevel_);
        if (!crossed)
            continue;

        triggerSample_ = copyStart + i;
        triggerArmed_ = false;
        triggerReady_ = false;
        ++triggerCount_;
        // Capture a short post-trigger window before freezing the view.
        triggerCaptureTimer_.start();
        ++triggerRevision_;
        emit triggerChanged();
        return;
    }
    triggerScanAbsolute_ = end;
}

QString AppController::chooseCsvToImport()
{
#ifdef Q_OS_WIN
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    if (auto* window = QGuiApplication::focusWindow())
        dialog.hwndOwner = reinterpret_cast<HWND>(window->winId());
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"CSV 录波\0*.csv\0所有文件\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != 0)
        return QString::fromWCharArray(filename);
#endif
    return {};
}

QString AppController::loadCsvRecording(const QString& path)
{
    if (path.isEmpty())
        return QStringLiteral("未选择文件");
    if (state_ != transport::ConnectionState::Disconnected)
        return QStringLiteral("请先断开连接，再导入 CSV 录波");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("无法读取文件：%1").arg(path);

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    QString header;
    while (!in.atEnd()) {
        header = in.readLine().trimmed();
        if (!header.isEmpty())
            break;
    }
    if (header.startsWith(QChar(0xFEFF)))
        header.remove(0, 1);
    auto columns = header.split(QLatin1Char(','));
    for (auto& column : columns)
        column = column.trimmed();
    if (columns.size() < 2)
        return QStringLiteral("CSV 至少需要一列索引和一列数据");

    const auto channels = static_cast<std::size_t>(columns.size() - 1);
    if (channels > limits::kMaxChannels)
        return QStringLiteral("通道数超过 %1").arg(limits::kMaxChannels);

    constexpr std::size_t kMaxRows = 200000;
    constexpr std::size_t kMaxValues = 4000000;
    std::vector<float> samples;
    samples.reserve(std::min<std::size_t>(kMaxRows, 65536) * channels);
    std::size_t rows = 0;
    int skipped = 0;

    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const auto parts = line.split(QLatin1Char(','));
        if (parts.size() != columns.size()) {
            ++skipped;
            continue;
        }

        bool valid = true;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok = false;
            const auto value = parts[i].trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(value)) {
                valid = false;
                break;
            }
            samples.push_back(static_cast<float>(value));
        }
        if (!valid) {
            samples.resize(rows * channels);
            ++skipped;
            continue;
        }

        ++rows;
        if (rows > kMaxRows || samples.size() > kMaxValues)
            return QStringLiteral("录波过大（上限 %1 行 / %2 个值）")
                .arg(kMaxRows)
                .arg(kMaxValues);
    }

    if (rows == 0)
        return QStringLiteral("CSV 中没有有效数据行");

    stopReplay();
    (void)store_.drain(1 << 20);
    store_.clearHistories();
    clearTrigger();

    replaySamples_ = std::move(samples);
    replayChannels_ = channels;
    replayRows_ = rows;
    replayPosition_ = 0;
    hasReplayRecording_ = true;
    samplesPerSecond_ = replayRate_;

    // Show the whole recording immediately; 播放 replays it from the start.
    feedReplayFrames(rows);
    replayPosition_ = rows;
    ++waveformRevision_;
    emit replayChanged();
    emit statsChanged();
    emit waveformChanged();

    auto message = QStringLiteral("已载入 %1 行 × %2 通道")
                       .arg(rows)
                       .arg(channels);
    if (skipped > 0)
        message += QStringLiteral("（跳过 %1 行）").arg(skipped);
    return message;
}

void AppController::setReplayRate(double rowsPerSecond)
{
    if (!std::isfinite(rowsPerSecond))
        return;
    rowsPerSecond = std::clamp(rowsPerSecond, 1.0, 1000000.0);
    if (qFuzzyCompare(replayRate_, rowsPerSecond))
        return;
    replayRate_ = rowsPerSecond;
    settings_.setValue(QStringLiteral("replay/rate"), replayRate_);
    emit replayChanged();
}

void AppController::startReplay()
{
    if (!hasReplayRecording_)
        return;

    if (replayPosition_ >= replayRows_) {
        (void)store_.drain(1 << 20);
        store_.clearHistories();
        replayPosition_ = 0;
        clearTrigger();
    }
    replayActive_ = true;
    samplesPerSecond_ = replayRate_;
    replayTimer_.start();
    emit replayChanged();
}

void AppController::stopReplay()
{
    replayTimer_.stop();
    if (!replayActive_)
        return;
    replayActive_ = false;
    emit replayChanged();
}

void AppController::resetReplay()
{
    stopReplay();
    (void)store_.drain(1 << 20);
    store_.clearHistories();
    replayPosition_ = 0;
    clearTrigger();
    ++waveformRevision_;
    emit replayChanged();
    emit waveformChanged();
}

void AppController::feedReplayFrames(std::size_t maxRows)
{
    if (!hasReplayRecording_ || replayChannels_ == 0)
        return;

    std::vector<float> row(replayChannels_);
    const auto rate = std::max(1.0, replayRate_);
    std::size_t fed = 0;
    int stalls = 0;

    while (fed < maxRows && replayPosition_ < replayRows_) {
        const auto base = replayPosition_ * replayChannels_;
        std::copy_n(replaySamples_.data() + base, replayChannels_, row.data());
        const auto timestamp = static_cast<std::int64_t>(
            static_cast<double>(replayPosition_) * 1.0e9 / rate);

        if (!store_.pushFrame(
                std::span<const float>(row.data(), replayChannels_), timestamp)) {
            (void)store_.drain(4096);
            if (++stalls > 8)
                break; // defensive; a UI-thread consumer always drains
            continue;
        }
        stalls = 0;
        ++replayPosition_;
        ++fed;
        if ((fed & 511u) == 0u)
            (void)store_.drain(8192);
    }
    (void)store_.drain(8192);
}

void AppController::buildSpectrum(int channel, int bins, std::vector<float>& out) const
{
    out.clear();
    bins = std::clamp(bins, 8, 1024);
    const auto* history = store_.channel(static_cast<std::size_t>(std::max(channel, 0)));
    if (history == nullptr || history->size() < 64)
        return;
    if (channel < 0 || static_cast<std::size_t>(channel) >= channelConfigs_.size())
        return;
    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];

    std::size_t n = 64;
    const std::size_t maxSize = std::min<std::size_t>(history->size(), 4096);
    while (n * 2 <= maxSize)
        n *= 2;
    if (n < 64)
        return;

    std::vector<float> real(n, 0.0f);
    std::vector<float> imag(n, 0.0f);
    if (!history->copyRange(history->absoluteEnd() - n, n, real.data()))
        return;

    double mean = 0.0;
    for (const float value : real)
        mean += value;
    mean /= static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i)
                                                   / static_cast<double>(n - 1));
        real[i] = static_cast<float>((real[i] - mean) * window);
    }

    fftRadix2(real, imag);

    out.assign(static_cast<std::size_t>(bins), 0.0f);
    const double scale = std::abs(config.scale);
    for (std::size_t k = 0; k <= n / 2; ++k) {
        const double magnitude = std::sqrt(static_cast<double>(real[k]) * real[k]
                                           + static_cast<double>(imag[k]) * imag[k])
            * 2.0 / static_cast<double>(n) * scale;
        const auto bin = static_cast<std::size_t>(
            static_cast<double>(k) * static_cast<double>(bins)
            / static_cast<double>(n / 2 + 1));
        if (bin < out.size())
            out[bin] = std::max(out[bin], static_cast<float>(magnitude));
    }
}

void AppController::buildHistogram(int channel,
                                   int bins,
                                   float& minValue,
                                   float& maxValue,
                                   std::vector<float>& out) const
{
    out.clear();
    bins = std::clamp(bins, 8, 512);
    if (channel < 0 || static_cast<std::size_t>(channel) >= channelConfigs_.size())
        return;
    const auto* history = store_.channel(static_cast<std::size_t>(channel));
    if (history == nullptr || history->size() == 0)
        return;
    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];

    auto viewEnd = history->absoluteEnd();
    if (waveformPanSamples_ > 0) {
        viewEnd -= std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), history->size());
    }
    std::size_t count = viewEnd - history->absoluteStart();
    const int windowSamples = effectiveWindowSamples();
    if (windowSamples > 0)
        count = std::min(count, static_cast<std::size_t>(windowSamples));
    count = std::min<std::size_t>(count, 65536);
    if (count < 2)
        return;
    const auto start = viewEnd - count;

    std::vector<float> values(count);
    if (!history->copyRange(start, count, values.data()))
        return;

    float low = 0.0f;
    float high = 0.0f;
    if (channelFixedRange(channel, low, high)) {
        minValue = low;
        maxValue = high;
    } else {
        low = values[0] * static_cast<float>(config.scale)
            + static_cast<float>(config.offset);
        high = low;
        for (const float raw : values) {
            const float value = raw * static_cast<float>(config.scale)
                + static_cast<float>(config.offset);
            if (!std::isfinite(value))
                continue;
            low = std::min(low, value);
            high = std::max(high, value);
        }
        if (high - low < 1.0e-9f) {
            low -= 0.5f;
            high += 0.5f;
        }
        minValue = low;
        maxValue = high;
    }

    const float span = std::max(high - low, 1.0e-9f);
    out.assign(static_cast<std::size_t>(bins), 0.0f);
    for (const float raw : values) {
        const float value = raw * static_cast<float>(config.scale)
            + static_cast<float>(config.offset);
        if (!std::isfinite(value))
            continue;
        int bin = static_cast<int>((value - low) / span * bins);
        bin = std::clamp(bin, 0, bins - 1);
        out[static_cast<std::size_t>(bin)] += 1.0f;
    }
}

void AppController::buildXY(int xChannel,
                            int yChannel,
                            int maxPoints,
                            std::vector<float>& out) const
{
    out.clear();
    if (xChannel < 0 || yChannel < 0 || xChannel == yChannel)
        return;
    if (static_cast<std::size_t>(xChannel) >= channelConfigs_.size()
        || static_cast<std::size_t>(yChannel) >= channelConfigs_.size()) {
        return;
    }
    const auto* xHistory = store_.channel(static_cast<std::size_t>(xChannel));
    const auto* yHistory = store_.channel(static_cast<std::size_t>(yChannel));
    if (xHistory == nullptr || yHistory == nullptr)
        return;

    auto start = std::max(xHistory->absoluteStart(), yHistory->absoluteStart());
    auto end = std::min(xHistory->absoluteEnd(), yHistory->absoluteEnd());
    if (waveformPanSamples_ > 0) {
        const auto offset = std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), end - start);
        end -= offset;
    }
    if (end <= start)
        return;
    maxPoints = std::clamp(maxPoints, 16, 8192);
    auto count = std::min<std::size_t>(
        static_cast<std::size_t>(maxPoints), end - start);
    start = end - count;

    std::vector<float> xs(count);
    std::vector<float> ys(count);
    if (!xHistory->copyRange(start, count, xs.data())
        || !yHistory->copyRange(start, count, ys.data())) {
        return;
    }

    const auto& xConfig = channelConfigs_[static_cast<std::size_t>(xChannel)];
    const auto& yConfig = channelConfigs_[static_cast<std::size_t>(yChannel)];
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        const float x = xs[i] * static_cast<float>(xConfig.scale)
            + static_cast<float>(xConfig.offset);
        const float y = ys[i] * static_cast<float>(yConfig.scale)
            + static_cast<float>(yConfig.offset);
        if (!std::isfinite(x) || !std::isfinite(y))
            continue;
        out.push_back(x);
        out.push_back(y);
    }
}

void AppController::setReceivePaused(bool paused)
{
    if (receivePaused_ == paused)
        return;
    receivePaused_ = paused;
    settings_.setValue(QStringLiteral("receive/paused"), paused);
    emit receiveOptionsChanged();
}

void AppController::setReceiveTimestamps(bool enabled)
{
    if (receiveTimestamps_ == enabled)
        return;
    receiveTimestamps_ = enabled;
    settings_.setValue(QStringLiteral("receive/timestamps"), enabled);
    emit receiveOptionsChanged();
}

void AppController::setReceiveHex(bool enabled)
{
    if (receiveHex_ == enabled)
        return;
    receiveHex_ = enabled;
    settings_.setValue(QStringLiteral("receive/hex"), enabled);
    emit receiveOptionsChanged();
    emit receiveTextChanged();
}

void AppController::setReceiveEncoding(int encoding)
{
    encoding = std::clamp(encoding, 0, 1);
    if (receiveEncoding_ == encoding)
        return;
    receiveEncoding_ = encoding;
    textDecoder_ = QStringDecoder(encoding == 0 ? QStringConverter::Utf8
                                               : QStringConverter::System);
    settings_.setValue(QStringLiteral("receive/encoding"), encoding);
    emit receiveOptionsChanged();
}

void AppController::setReceiveAnsi(bool enabled)
{
    if (receiveAnsi_ == enabled)
        return;
    receiveAnsi_ = enabled;
    settings_.setValue(QStringLiteral("receive/ansi"), enabled);
    emit receiveOptionsChanged();
    emit receiveTextChanged();
}

void AppController::setReceiveFilter(const QString& filter)
{
    if (receiveFilter_ == filter)
        return;
    receiveFilter_ = filter;
    filterLineBuffer_.clear();
    rebuildReceiveFilterExpression();
    settings_.setValue(QStringLiteral("receive/filter"), filter);
    emit receiveOptionsChanged();
}

void AppController::setReceiveFilterRegex(bool enabled)
{
    if (receiveFilterRegex_ == enabled)
        return;
    receiveFilterRegex_ = enabled;
    rebuildReceiveFilterExpression();
    settings_.setValue(QStringLiteral("receive/filterRegex"), enabled);
    emit receiveOptionsChanged();
}

void AppController::setReceiveFilterDirection(int direction)
{
    direction = std::clamp(direction, 0, 2);
    if (receiveFilterDirection_ == direction)
        return;
    receiveFilterDirection_ = direction;
    settings_.setValue(QStringLiteral("receive/filterDirection"), direction);
    emit receiveOptionsChanged();
}

void AppController::setReceiveTxEcho(bool enabled)
{
    if (receiveTxEcho_ == enabled)
        return;
    receiveTxEcho_ = enabled;
    settings_.setValue(QStringLiteral("receive/txEcho"), enabled);
    emit receiveOptionsChanged();
}

void AppController::rebuildReceiveFilterExpression()
{
    receiveFilterExpression_ = QRegularExpression();
    if (receiveFilterRegex_ && !receiveFilter_.isEmpty())
        receiveFilterExpression_ = QRegularExpression(receiveFilter_);
}

bool AppController::lineMatchesReceiveFilter(const QString& line) const
{
    if (receiveFilter_.isEmpty())
        return true;
    if (receiveFilterRegex_ && receiveFilterExpression_.isValid()
        && !receiveFilterExpression_.pattern().isEmpty()) {
        return receiveFilterExpression_.match(line).hasMatch();
    }
    // Invalid patterns degrade to a literal contains match instead of hiding
    // every line.
    return line.contains(receiveFilter_, Qt::CaseInsensitive);
}

void AppController::appendTxEcho(const QString& text, bool hex)
{
    if (receivePaused_ || receiveHex_)
        return;
    if (receiveFilterDirection_ == 1) // receive only
        return;
    if (!receiveTxEcho_ && receiveFilterDirection_ != 2)
        return;

    QString line = hex ? QStringLiteral("HEX ") + text : text;
    line.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    line.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (line.isEmpty() || !lineMatchesReceiveFilter(line))
        return;

    QString prefix;
    if (receiveTimestamps_) {
        prefix = QLatin1Char('[')
            + QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
            + QStringLiteral("] ");
    }
    prefix += QStringLiteral("TX ▸ ");

    if (receiveAnsi_) {
        pendingAnsi_ += QStringLiteral("<span style=\"color:#7f8c8d\">")
            + (prefix + line).toHtmlEscaped() + QStringLiteral("</span>\n");
    } else {
        pendingReceive_ += prefix + line + QLatin1Char('\n');
    }
}

void AppController::resetAnsiState()
{
    ansiColor_.clear();
    ansiBold_ = false;
}

QString AppController::ansiToHtml(const QString& text)
{
    const bool dark = uiTheme_ == 1;
    const auto colorFor = [dark](int code) -> QString {
        switch (code) {
        case 30: return dark ? QStringLiteral("#9aa7b4") : QStringLiteral("#1f2d3d");
        case 31: return QStringLiteral("#e06c75");
        case 32: return QStringLiteral("#2ecc71");
        case 33: return QStringLiteral("#e5c07b");
        case 34: return QStringLiteral("#61afef");
        case 35: return QStringLiteral("#c678dd");
        case 36: return QStringLiteral("#56b6c2");
        case 37: return dark ? QStringLiteral("#ffffff") : QStringLiteral("#4a5563");
        case 90: return QStringLiteral("#7f8c8d");
        case 91: return QStringLiteral("#ff6b6b");
        case 92: return QStringLiteral("#2ecc71");
        case 93: return QStringLiteral("#f1c40f");
        case 94: return QStringLiteral("#3498db");
        case 95: return QStringLiteral("#9b59b6");
        case 96: return QStringLiteral("#1abc9c");
        case 97: return dark ? QStringLiteral("#ffffff") : QStringLiteral("#1f2d3d");
        default: return {};
        }
    };

    QString html;
    html.reserve(text.size() + 32);

    const auto emitRun = [this, &html](QString run) {
        run.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        run.remove(QLatin1Char('\r'));
        if (run.isEmpty())
            return;
        run = run.toHtmlEscaped();
        run.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
        run.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
        if (ansiColor_.isEmpty() && !ansiBold_) {
            html += run;
            return;
        }
        QString style;
        if (!ansiColor_.isEmpty())
            style += QStringLiteral("color:%1;").arg(ansiColor_);
        if (ansiBold_)
            style += QStringLiteral("font-weight:bold;");
        html += QStringLiteral("<span style=\"%1\">").arg(style) + run
            + QStringLiteral("</span>");
    };

    int i = 0;
    while (i < text.size()) {
        if (text.at(i) == QChar(0x1B) && i + 1 < text.size()
            && text.at(i + 1) == QLatin1Char('[')) {
            int j = i + 2;
            while (j < text.size()
                   && (text.at(j).isDigit() || text.at(j) == QLatin1Char(';'))) {
                ++j;
            }
            if (j < text.size() && text.at(j) == QLatin1Char('m')) {
                const auto params =
                    text.mid(i + 2, j - i - 2).split(QLatin1Char(';'), Qt::SkipEmptyParts);
                if (params.isEmpty()) {
                    resetAnsiState();
                }
                for (const auto& param : params) {
                    bool ok = false;
                    const int value = param.toInt(&ok);
                    if (!ok)
                        continue;
                    if (value == 0) {
                        resetAnsiState();
                    } else if (value == 1) {
                        ansiBold_ = true;
                    } else if (value == 22) {
                        ansiBold_ = false;
                    } else if (value == 39) {
                        ansiColor_.clear();
                    } else {
                        const auto colour = colorFor(value);
                        if (!colour.isEmpty())
                            ansiColor_ = colour;
                    }
                }
                i = j + 1;
                continue;
            }
            i = j;
            continue;
        }

        const int start = i;
        while (i < text.size()
               && !(text.at(i) == QChar(0x1B) && i + 1 < text.size()
                    && text.at(i + 1) == QLatin1Char('['))) {
            ++i;
        }
        emitRun(text.mid(start, i - start));
    }
    return html;
}

void AppController::setWaveformPaused(bool paused)
{
    if (waveformPaused_ == paused)
        return;
    waveformPaused_ = paused;
    settings_.setValue(QStringLiteral("waveform/paused"), paused);
    if (!paused) {
        const bool hadPan = waveformPanSamples_ != 0;
        waveformPanSamples_ = 0;
        ++waveformRevision_;
        emit waveformChanged();
        if (hadPan)
            emit waveformPanChanged();
    }
    emit waveformPausedChanged();
}

int AppController::waveformVisibleSampleCount() const
{
    const auto* history = store_.channel(0);
    if (history == nullptr || history->size() == 0)
        return 0;

    auto viewEnd = history->absoluteEnd();
    if (waveformPanSamples_ > 0) {
        viewEnd -= std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), history->size());
    }
    auto count = viewEnd - history->absoluteStart();
    const int windowSamples = effectiveWindowSamples();
    if (windowSamples > 0)
        count = std::min(count, static_cast<std::size_t>(windowSamples));
    return static_cast<int>(std::min<std::size_t>(count, 10'000'000));
}

void AppController::panWaveform(int deltaSamples)
{
    if (deltaSamples == 0)
        return;
    const auto* history = store_.channel(0);
    if (history == nullptr || history->size() == 0)
        return;

    const auto total = static_cast<int>(
        std::min<std::size_t>(history->size(), 10'000'000));
    const int windowSamples = effectiveWindowSamples();
    const int window = windowSamples > 0
        ? std::min(windowSamples, total)
        : total;
    const int maxPan = std::max(0, total - window);

    const int next = std::clamp(waveformPanSamples_ + deltaSamples, 0, maxPan);
    if (next == waveformPanSamples_)
        return;

    waveformPanSamples_ = next;
    bool pausedChanged = false;
    if (next > 0 && !waveformPaused_) {
        waveformPaused_ = true;
        pausedChanged = true;
    }
    ++waveformRevision_;
    emit waveformPanChanged();
    emit waveformChanged();
    if (pausedChanged)
        emit waveformPausedChanged();
}

void AppController::zoomWaveformAt(double factor, double anchorFraction)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    const auto* history = store_.channel(0);
    if (history == nullptr || history->size() < 2)
        return;

    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const auto total = history->size();
    const auto absoluteEnd = history->absoluteEnd();

    int oldCount = effectiveWindowSamples();
    if (oldCount <= 0)
        oldCount = static_cast<int>(std::min<std::size_t>(total, 262144));
    oldCount = std::min(oldCount, static_cast<int>(total));

    auto viewEnd = absoluteEnd;
    if (waveformPanSamples_ > 0) {
        viewEnd -= std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), total);
    }
    const auto oldStart = viewEnd - static_cast<std::size_t>(oldCount);
    const double anchorSample = static_cast<double>(oldStart)
        + anchorFraction * static_cast<double>(oldCount);

    int newCount = oldCount;
    if (waveformWindowSeconds_ > 0.0) {
        const double rate = samplesPerSecond_ > 1.0 ? samplesPerSecond_ : 1000.0;
        const double seconds = std::clamp(waveformWindowSeconds_ * factor, 0.05, 3600.0);
        newCount = static_cast<int>(std::llround(seconds * rate));
        waveformWindowSeconds_ = seconds;
        settings_.setValue(QStringLiteral("waveform/windowSeconds"), seconds);
    } else {
        const int base = waveformWindowSamples_ > 0
            ? waveformWindowSamples_
            : oldCount;
        newCount = static_cast<int>(std::llround(static_cast<double>(base) * factor));
        waveformWindowSamples_ = std::clamp(newCount, 16, 262144);
        settings_.setValue(QStringLiteral("waveform/windowSamples"),
                           waveformWindowSamples_);
    }
    newCount = std::clamp(newCount, 16, 262144);
    newCount = std::min(newCount, static_cast<int>(total));

    const double requestedEnd =
        anchorSample + (1.0 - anchorFraction) * static_cast<double>(newCount);
    const auto minimumEnd = history->absoluteStart()
        + static_cast<std::size_t>(newCount);
    const double clampedEnd = std::clamp(
        requestedEnd,
        static_cast<double>(minimumEnd),
        static_cast<double>(absoluteEnd));
    const int maxPan = std::max(0, static_cast<int>(total) - newCount);
    const int nextPan = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(absoluteEnd) - clampedEnd)),
        0, maxPan);

    waveformPanSamples_ = nextPan;
    if (nextPan > 0)
        waveformPaused_ = true;
    ++waveformRevision_;
    emit waveformWindowChanged();
    emit waveformPanChanged();
    emit waveformPausedChanged();
    emit waveformChanged();
}

void AppController::zoomWaveformToFractions(double startFraction,
                                            double endFraction)
{
    const auto* history = store_.channel(0);
    if (history == nullptr || history->size() < 2)
        return;
    if (endFraction < startFraction)
        std::swap(startFraction, endFraction);
    startFraction = std::clamp(startFraction, 0.0, 1.0);
    endFraction = std::clamp(endFraction, 0.0, 1.0);
    if (endFraction - startFraction < 0.001)
        return;

    const auto total = history->size();
    const auto absoluteEnd = history->absoluteEnd();

    int oldCount = effectiveWindowSamples();
    if (oldCount <= 0)
        oldCount = static_cast<int>(std::min<std::size_t>(total, 262144));
    oldCount = std::min(oldCount, static_cast<int>(total));

    auto viewEnd = absoluteEnd;
    if (waveformPanSamples_ > 0) {
        viewEnd -= std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), total);
    }
    const auto oldStart = viewEnd - static_cast<std::size_t>(oldCount);
    const auto selectedStart = oldStart + static_cast<std::size_t>(
        std::llround(startFraction * oldCount));
    const auto selectedEnd = oldStart + static_cast<std::size_t>(
        std::llround(endFraction * oldCount));
    const int newCount = std::clamp(
        static_cast<int>(selectedEnd - selectedStart), 16,
        static_cast<int>(total));

    if (waveformWindowSeconds_ > 0.0) {
        const double rate = samplesPerSecond_ > 1.0 ? samplesPerSecond_ : 1000.0;
        waveformWindowSeconds_ = std::clamp(
            static_cast<double>(newCount) / rate, 0.05, 3600.0);
        settings_.setValue(QStringLiteral("waveform/windowSeconds"),
                           waveformWindowSeconds_);
    } else {
        waveformWindowSamples_ = newCount;
        settings_.setValue(QStringLiteral("waveform/windowSamples"), newCount);
    }

    const int maxPan = std::max(0, static_cast<int>(total) - newCount);
    const int nextPan = std::clamp(
        static_cast<int>(absoluteEnd - selectedEnd), 0, maxPan);
    waveformPanSamples_ = nextPan;
    if (nextPan > 0)
        waveformPaused_ = true;
    ++waveformRevision_;
    emit waveformWindowChanged();
    emit waveformPanChanged();
    emit waveformPausedChanged();
    emit waveformChanged();
}

void AppController::resetWaveformPan()
{
    const bool hadPan = waveformPanSamples_ != 0;
    const bool wasPaused = waveformPaused_;
    waveformPanSamples_ = 0;
    waveformPaused_ = false;
    settings_.setValue(QStringLiteral("waveform/paused"), false);
    ++waveformRevision_;
    if (hadPan)
        emit waveformPanChanged();
    if (wasPaused)
        emit waveformPausedChanged();
    emit waveformChanged();
}

void AppController::setChannelVisible(int index, bool visible)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (config.visible == visible)
        return;
    config.visible = visible;
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::setChannelColor(int index, const QString& color)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()) || color.isEmpty())
        return;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (config.color == color)
        return;
    config.color = color;
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::setChannelScale(int index, double scale)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    if (qFuzzyIsNull(scale))
        scale = 1.0;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (qFuzzyCompare(config.scale, scale))
        return;
    config.scale = scale;
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::setChannelOffset(int index, double offset)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (qFuzzyCompare(config.offset, offset))
        return;
    config.offset = offset;
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::setChannelDecimals(int index, int decimals)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    decimals = std::clamp(decimals, 0, 9);
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (config.decimals == decimals)
        return;
    config.decimals = decimals;
    persistChannelConfigs();
    emit channelSettingsChanged();
}

void AppController::setChannelAutoScale(int index, bool autoScale)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (config.autoScale == autoScale)
        return;
    config.autoScale = autoScale;
    if (!autoScale && config.rangeMax - config.rangeMin < 1.0e-9) {
        config.rangeMin = -5.0;
        config.rangeMax = 5.0;
    }
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::setChannelRange(int index, double minValue, double maxValue)
{
    if (index < 0 || index >= static_cast<int>(channelConfigs_.size()))
        return;
    auto& config = channelConfigs_[static_cast<std::size_t>(index)];
    if (maxValue < minValue)
        std::swap(minValue, maxValue);
    if (maxValue - minValue < 1.0e-9) {
        minValue -= 0.5;
        maxValue += 0.5;
    }
    if (config.rangeMin == minValue && config.rangeMax == maxValue)
        return;
    config.rangeMin = minValue;
    config.rangeMax = maxValue;
    config.autoScale = false;
    persistChannelConfigs();
    ++waveformRevision_;
    emit channelSettingsChanged();
    emit waveformChanged();
}

bool AppController::channelFixedRange(int channel,
                                     float& minValue,
                                     float& maxValue) const
{
    if (channel < 0
        || static_cast<std::size_t>(channel) >= channelConfigs_.size()) {
        return false;
    }
    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];
    if (config.autoScale)
        return false;

    minValue = static_cast<float>(config.rangeMin * config.scale + config.offset);
    maxValue = static_cast<float>(config.rangeMax * config.scale + config.offset);
    if (minValue > maxValue)
        std::swap(minValue, maxValue);
    return true;
}

QString AppController::configDir() const
{
    return settings_.configDir();
}

void AppController::refreshSerialPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    ports.reserve(infos.size());
    for (const auto& info : infos)
        ports.push_back(info.portName());
    ports.sort();

    if (serialPorts_ != ports) {
        serialPorts_ = std::move(ports);
        emit serialPortsChanged();
    }
}

void AppController::openSerial(const QString& port, int baudRate)
{
    openSerialAdvanced(port, baudRate,
                       static_cast<int>(QSerialPort::Data8),
                       static_cast<int>(QSerialPort::NoParity),
                       static_cast<int>(QSerialPort::OneStop),
                       static_cast<int>(QSerialPort::NoFlowControl),
                       false, false);
}

void AppController::openSerialAdvanced(const QString& port,
                                       int baudRate,
                                       int dataBits,
                                       int parity,
                                       int stopBits,
                                       int flowControl,
                                       bool dtr,
                                       bool rts)
{
    if (transportKind_ != 0) {
        transportKind_ = 0;
        emit transportKindChanged();
    }

    transport::TransportConfig config;
    config.kind = transport::TransportKind::Serial;
    config.protocol = protocolKind_;
    config.serial.portName = port;
    config.serial.baudRate = baudRate;
    config.serial.dataBits = static_cast<QSerialPort::DataBits>(dataBits);
    config.serial.parity = static_cast<QSerialPort::Parity>(parity);
    config.serial.stopBits = static_cast<QSerialPort::StopBits>(stopBits);
    config.serial.flowControl = static_cast<QSerialPort::FlowControl>(flowControl);
    config.serial.dtr = dtr;
    config.serial.rts = rts;

    settings_.setValue(QStringLiteral("connection/kind"), QStringLiteral("serial"));
    settings_.setValue(QStringLiteral("connection/serial/port"), port);
    settings_.setValue(QStringLiteral("connection/serial/baud"), baudRate);
    settings_.setValue(QStringLiteral("connection/serial/dataBits"), dataBits);
    settings_.setValue(QStringLiteral("connection/serial/parity"), parity);
    settings_.setValue(QStringLiteral("connection/serial/stopBits"), stopBits);
    settings_.setValue(QStringLiteral("connection/serial/flowControl"), flowControl);
    settings_.setValue(QStringLiteral("connection/serial/dtr"), dtr);
    settings_.setValue(QStringLiteral("connection/serial/rts"), rts);

    lastConfig_ = config;
    lastConfigValid_ = true;
    userClosed_ = false;
    reconnectTimer_.stop();
    transport_.open(config);
}

void AppController::openDemo()
{
    if (transportKind_ != 4) {
        transportKind_ = 4;
        emit transportKindChanged();
    }

    transport::TransportConfig config;
    config.kind = transport::TransportKind::Demo;
    config.protocol = protocolKind_;
    settings_.setValue(QStringLiteral("connection/kind"), QStringLiteral("demo"));
    lastConfig_ = config;
    lastConfigValid_ = true;
    userClosed_ = false;
    reconnectTimer_.stop();
    transport_.open(config);
}

void AppController::openTcpClient(const QString& host, int port, const QString& banner)
{
    if (transportKind_ != 1) {
        transportKind_ = 1;
        emit transportKindChanged();
    }

    tcpClientHost_ = host;
    tcpClientPort_ = port;
    tcpClientBanner_ = banner;
    emit connectionParamsChanged();

    transport::TransportConfig config;
    config.kind = transport::TransportKind::TcpClient;
    config.protocol = protocolKind_;
    config.tcpClient.host = host;
    config.tcpClient.port = static_cast<quint16>(port);
    config.tcpClient.banner = banner.toUtf8();

    settings_.setValue(QStringLiteral("connection/kind"), QStringLiteral("tcpClient"));
    settings_.setValue(QStringLiteral("connection/tcpClient/host"), host);
    settings_.setValue(QStringLiteral("connection/tcpClient/port"), port);
    settings_.setValue(QStringLiteral("connection/tcpClient/banner"), banner);

    lastConfig_ = config;
    lastConfigValid_ = true;
    userClosed_ = false;
    reconnectTimer_.stop();
    transport_.open(config);
}

void AppController::openTcpServer(int port)
{
    if (transportKind_ != 2) {
        transportKind_ = 2;
        emit transportKindChanged();
    }

    transport::TransportConfig config;
    config.kind = transport::TransportKind::TcpServer;
    config.protocol = protocolKind_;
    config.tcpServer.listenPort = static_cast<quint16>(port);

    tcpServerPort_ = port;
    listeningPort_ = 0;
    settings_.setValue(QStringLiteral("connection/kind"), QStringLiteral("tcpServer"));
    settings_.setValue(QStringLiteral("connection/tcpServer/port"), port);
    emit connectionParamsChanged();

    lastConfig_ = config;
    lastConfigValid_ = true;
    userClosed_ = false;
    reconnectTimer_.stop();
    transport_.open(config);
}

void AppController::openUdp(const QString& remoteHost, int remotePort, int localPort)
{
    if (transportKind_ != 3) {
        transportKind_ = 3;
        emit transportKindChanged();
    }

    transport::TransportConfig config;
    config.kind = transport::TransportKind::Udp;
    config.protocol = protocolKind_;
    config.udp.remoteHost = remoteHost;
    config.udp.remotePort = static_cast<quint16>(remotePort);
    config.udp.localPort = static_cast<quint16>(localPort);

    settings_.setValue(QStringLiteral("connection/kind"), QStringLiteral("udp"));
    settings_.setValue(QStringLiteral("connection/udp/remoteHost"), remoteHost);
    settings_.setValue(QStringLiteral("connection/udp/remotePort"), remotePort);
    settings_.setValue(QStringLiteral("connection/udp/localPort"), localPort);

    lastConfig_ = config;
    lastConfigValid_ = true;
    userClosed_ = false;
    reconnectTimer_.stop();
    transport_.open(config);
}

void AppController::setAutoReconnect(bool enabled)
{
    if (autoReconnect_ == enabled)
        return;
    autoReconnect_ = enabled;
    settings_.setValue(QStringLiteral("connection/autoReconnect"), enabled);
    if (!enabled)
        reconnectTimer_.stop();
    emit autoReconnectChanged();
}

void AppController::closeTransport()
{
    userClosed_ = true;
    reconnectTimer_.stop();
    transport_.close();
}

void AppController::sendText(const QString& text)
{
    if (text.isEmpty())
        return;
    const auto bytes = text.toUtf8();
    totalTxBytes_ += static_cast<qulonglong>(bytes.size());
    transport_.send(bytes);
    addRecentSend(text, false);
    appendTxEcho(text, false);
    emit statsChanged();
}

void AppController::sendHex(const QString& hex)
{
    QString cleaned = hex;
    cleaned.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (cleaned.isEmpty())
        return;

    if (cleaned.size() % 2 != 0) {
        setError(QStringLiteral("HEX 长度必须是偶数"));
        return;
    }

    const auto bytes = QByteArray::fromHex(cleaned.toLatin1());
    if (bytes.isEmpty()) {
        setError(QStringLiteral("HEX 格式错误"));
        return;
    }

    totalTxBytes_ += static_cast<qulonglong>(bytes.size());
    transport_.send(bytes);
    addRecentSend(cleaned, true);
    appendTxEcho(cleaned, true);
    emit statsChanged();
}

void AppController::sendTextTo(qulonglong clientId, const QString& text)
{
    const auto bytes = text.toUtf8();
    totalTxBytes_ += static_cast<qulonglong>(bytes.size());
    transport_.sendTo(clientId, bytes);
    appendTxEcho(text, false);
    emit statsChanged();
}

void AppController::setActiveClient(qulonglong clientId)
{
    transport_.setActiveClient(clientId);
}

void AppController::clearReceive()
{
    pendingReceive_.clear();
    receiveText_.clear();
    pendingHex_.clear();
    receiveHexText_.clear();
    receiveAnsiText_.clear();
    pendingAnsi_.clear();
    filterLineBuffer_.clear();
    resetAnsiState();
    hexColumn_ = 0;
    hexOffset_ = 0;
    textDecoder_.resetState();
    emit receiveTextChanged();
}

void AppController::appendHexChunk(const QByteArray& data)
{
    constexpr int kBytesPerLine = 16;
    pendingHex_.reserve(pendingHex_.size() + data.size() * 3 + 16);

    for (const char raw : data) {
        const auto byte = static_cast<unsigned char>(raw);
        if (hexColumn_ == 0) {
            pendingHex_ += QStringLiteral("%1  ")
                               .arg(hexOffset_, 8, 16, QLatin1Char('0'))
                               .toUpper();
        }
        pendingHex_ += QStringLiteral("%1 ")
                           .arg(byte, 2, 16, QLatin1Char('0'))
                           .toUpper();
        ++hexOffset_;
        if (++hexColumn_ >= kBytesPerLine) {
            pendingHex_ += QLatin1Char('\n');
            hexColumn_ = 0;
        }
    }

    if (pendingHex_.size() > 262144)
        pendingHex_ = pendingHex_.right(262144);
}

void AppController::loadRecentSends()
{
    recentSends_.clear();
    const auto items = settings_.value(QStringLiteral("send/recent")).toArray();
    for (const auto& entry : items) {
        const auto object = entry.toObject();
        const auto text = object.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("text"), text);
        item.insert(QStringLiteral("hex"), object.value(QStringLiteral("hex")).toBool());
        recentSends_.push_back(item);
    }
}

void AppController::persistRecentSends()
{
    QJsonArray items;
    for (const auto& value : recentSends_) {
        const auto item = value.toMap();
        QJsonObject object;
        object.insert(QStringLiteral("text"), item.value(QStringLiteral("text")).toString());
        object.insert(QStringLiteral("hex"), item.value(QStringLiteral("hex")).toBool());
        items.append(object);
    }
    settings_.setValue(QStringLiteral("send/recent"), QJsonValue(items));
}

void AppController::addRecentSend(const QString& text, bool hex)
{
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    // Move an identical entry to the front instead of duplicating it.
    for (int i = static_cast<int>(recentSends_.size()) - 1; i >= 0; --i) {
        const auto item = recentSends_[i].toMap();
        if (item.value(QStringLiteral("text")).toString() == trimmed
            && item.value(QStringLiteral("hex")).toBool() == hex) {
            recentSends_.removeAt(i);
        }
    }

    QVariantMap item;
    item.insert(QStringLiteral("text"), trimmed);
    item.insert(QStringLiteral("hex"), hex);
    recentSends_.prepend(item);
    while (recentSends_.size() > 20)
        recentSends_.removeLast();

    persistRecentSends();
    emit recentSendsChanged();
}

void AppController::repeatRecentSend(int index)
{
    if (index < 0 || index >= recentSends_.size())
        return;
    const auto item = recentSends_[index].toMap();
    const auto text = item.value(QStringLiteral("text")).toString();
    if (item.value(QStringLiteral("hex")).toBool())
        sendHex(text);
    else
        sendText(text);
}

void AppController::repeatRecentSendEntry(const QString& text, bool hex)
{
    // Used by the filtered history list: its row index is not a valid index
    // into the unfiltered recentSends_ array.
    if (hex)
        sendHex(text);
    else
        sendText(text);
}

QVariantList AppController::filteredRecentSends() const
{
    if (sendFilter_.isEmpty())
        return recentSends_;

    QVariantList result;
    for (const auto& value : recentSends_) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("text"))
                .toString()
                .contains(sendFilter_, Qt::CaseInsensitive))
            result.push_back(item);
    }
    return result;
}

void AppController::setSendFilter(const QString& filter)
{
    if (sendFilter_ == filter)
        return;
    sendFilter_ = filter;
    settings_.setValue(QStringLiteral("send/filter"), filter);
    emit recentSendsChanged();
}

void AppController::clearRecentSends()
{
    if (recentSends_.isEmpty())
        return;
    recentSends_.clear();
    persistRecentSends();
    emit recentSendsChanged();
}

void AppController::loadActionBindings()
{
    actions_.clear();
    const auto items = settings_.value(QStringLiteral("actions/items")).toArray();
    for (const auto& value : items) {
        if (actions_.size() >= 64)
            break;
        const auto item = value.toObject();
        ActionEntry entry;
        entry.id = static_cast<quint64>(item.value(QStringLiteral("id")).toDouble());
        if (entry.id == 0)
            continue;
        entry.label = item.value(QStringLiteral("label")).toString();
        entry.group = item.value(QStringLiteral("group")).toString();
        if (entry.group.isEmpty() || !commandGroups_.contains(entry.group))
            entry.group = commandGroups_.value(0);
        entry.onTemplate = item.value(QStringLiteral("on")).toString();
        entry.offTemplate = item.value(QStringLiteral("off")).toString();
        entry.kind = std::clamp(item.value(QStringLiteral("kind")).toInt(0), 0, 1);
        entry.state = item.value(QStringLiteral("state")).toBool(false);
        if (entry.label.isEmpty())
            entry.label = entry.onTemplate;
        if (entry.onTemplate.isEmpty())
            continue;
        actions_.push_back(std::move(entry));
    }
    rebuildActionBindings();
}

void AppController::persistActionBindings()
{
    QJsonArray items;
    for (const auto& entry : actions_) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), static_cast<double>(entry.id));
        item.insert(QStringLiteral("label"), entry.label);
        item.insert(QStringLiteral("group"), entry.group);
        item.insert(QStringLiteral("on"), entry.onTemplate);
        item.insert(QStringLiteral("off"), entry.offTemplate);
        item.insert(QStringLiteral("kind"), entry.kind);
        item.insert(QStringLiteral("state"), entry.state);
        items.append(item);
    }
    settings_.setValue(QStringLiteral("actions/items"), QJsonValue(items));
}

void AppController::rebuildActionBindings()
{
    QVariantList list;
    list.reserve(static_cast<int>(actions_.size()));
    const auto activeGroup = currentGroupName();
    for (const auto& entry : actions_) {
        if (entry.group != activeGroup)
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(entry.id));
        item.insert(QStringLiteral("label"), entry.label);
        item.insert(QStringLiteral("group"), entry.group);
        item.insert(QStringLiteral("on"), entry.onTemplate);
        item.insert(QStringLiteral("off"), entry.offTemplate);
        item.insert(QStringLiteral("kind"), entry.kind);
        item.insert(QStringLiteral("state"), entry.state);
        list.push_back(item);
    }
    actionBindings_ = std::move(list);
    emit actionBindingsChanged();
    scheduleRemoteState();
}

void AppController::sendRawTemplate(const QString& text)
{
    if (text.isEmpty())
        return;

    const auto bytes = text.toUtf8();
    auto formatter = commands::TemplateFormatter::parse(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.constData()),
            static_cast<std::size_t>(bytes.size())));
    if (!formatter || formatter->slotCount() != 0) {
        setError(QStringLiteral("动作命令必须是无参数命令：%1").arg(text));
        return;
    }

    std::vector<std::byte> payload;
    if (!formatter->render({}, payload)) {
        setError(QStringLiteral("动作命令渲染失败：%1").arg(text));
        return;
    }

    const QByteArray data(reinterpret_cast<const char*>(payload.data()),
                          static_cast<int>(payload.size()));
    totalTxBytes_ += static_cast<qulonglong>(data.size());
    transport_.send(data);
    addRecentSend(text, false);
    appendTxEcho(text, false);
    emit statsChanged();
}

void AppController::triggerAction(qulonglong bindingId)
{
    triggerActionFrom(bindingId, QStringLiteral("pc"));
}

void AppController::triggerActionFrom(quint64 bindingId, const QString& actor)
{
    const auto it = std::find_if(
        actions_.begin(), actions_.end(),
        [bindingId](const ActionEntry& entry) { return entry.id == bindingId; });
    if (it == actions_.end())
        return;

    if (it->kind == 1) {
        applyActionState(bindingId, !it->state, actor);
        return;
    }
    sendRawTemplate(it->onTemplate);
    // Buttons carry no state; the event still lets other clients show the
    // press so two people do not fight over the same control.
    remote_.broadcastAction(bindingId, it->kind, false, actor, true);
}

void AppController::setActionState(qulonglong bindingId, bool on)
{
    applyActionState(bindingId, on, QStringLiteral("pc"));
}

void AppController::applyActionState(quint64 bindingId, bool on, const QString& actor)
{
    const auto it = std::find_if(
        actions_.begin(), actions_.end(),
        [bindingId](const ActionEntry& entry) { return entry.id == bindingId; });
    if (it == actions_.end() || it->kind != 1 || it->state == on)
        return;

    it->state = on;
    const auto& text = on ? it->onTemplate : it->offTemplate;
    if (!text.isEmpty())
        sendRawTemplate(text);
    persistActionBindings();
    rebuildActionBindings();
    remote_.broadcastAction(bindingId, it->kind, on, actor, false);
}

bool AppController::addActionBinding(const QString& label,
                                     const QString& onTemplate,
                                     const QString& offTemplate,
                                     int kind,
                                     const QString& group)
{
    const auto cleanLabel = label.trimmed();
    const auto cleanOn = onTemplate.trimmed();
    const auto cleanOff = offTemplate.trimmed();
    kind = std::clamp(kind, 0, 1);
    if (cleanLabel.isEmpty() || cleanOn.isEmpty() || actions_.size() >= 64)
        return false;

    const auto isRawCommand = [](const QString& text) {
        if (text.isEmpty())
            return true;
        const auto bytes = text.toUtf8();
        const auto formatter = commands::TemplateFormatter::parse(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.constData()),
                static_cast<std::size_t>(bytes.size())));
        return formatter && formatter->slotCount() == 0;
    };
    if (!isRawCommand(cleanOn) || (kind == 1 && !isRawCommand(cleanOff)))
        return false;

    captureEdit();
    ActionEntry entry;
    entry.id = nextActionId();
    entry.label = cleanLabel;
    entry.group = hasCommandGroup(group) ? group : currentGroupName();
    entry.onTemplate = cleanOn;
    entry.offTemplate = cleanOff;
    entry.kind = kind;
    actions_.push_back(std::move(entry));

    persistActionBindings();
    rebuildActionBindings();
    return true;
}

bool AppController::updateActionBinding(qulonglong bindingId,
                                        const QString& label,
                                        const QString& onTemplate,
                                        const QString& offTemplate,
                                        int kind,
                                        const QString& group)
{
    const auto it = std::find_if(
        actions_.begin(), actions_.end(),
        [bindingId](const ActionEntry& entry) { return entry.id == bindingId; });
    if (it == actions_.end())
        return false;

    const auto cleanLabel = label.trimmed();
    const auto cleanOn = onTemplate.trimmed();
    const auto cleanOff = offTemplate.trimmed();
    if (cleanLabel.isEmpty() || cleanOn.isEmpty())
        return false;

    const auto isRawCommand = [](const QString& text) {
        if (text.isEmpty())
            return true;
        const auto bytes = text.toUtf8();
        const auto formatter = commands::TemplateFormatter::parse(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.constData()),
                static_cast<std::size_t>(bytes.size())));
        return formatter && formatter->slotCount() == 0;
    };
    kind = std::clamp(kind, 0, 1);
    if (!isRawCommand(cleanOn) || (kind == 1 && !isRawCommand(cleanOff)))
        return false;

    captureEdit();
    it->label = cleanLabel;
    it->group = hasCommandGroup(group) ? group : currentGroupName();
    it->onTemplate = cleanOn;
    it->offTemplate = cleanOff;
    it->kind = kind;
    persistActionBindings();
    rebuildActionBindings();
    return true;
}

bool AppController::removeActionBinding(qulonglong bindingId)
{
    const auto it = std::find_if(
        actions_.begin(), actions_.end(),
        [bindingId](const ActionEntry& entry) { return entry.id == bindingId; });
    if (it == actions_.end())
        return false;
    captureEdit();
    actions_.erase(it);
    persistActionBindings();
    rebuildActionBindings();
    return true;
}

quint64 AppController::nextActionId() const
{
    quint64 next = 2000;
    for (const auto& entry : actions_)
        next = std::max<quint64>(next, entry.id + 1);
    return next;
}

void AppController::saveWindow(int x, int y, int width, int height)
{
    settings_.setValue(QStringLiteral("window/x"), x);
    settings_.setValue(QStringLiteral("window/y"), y);
    settings_.setValue(QStringLiteral("window/width"), width);
    settings_.setValue(QStringLiteral("window/height"), height);
}

QVariantMap AppController::savedPanelWidths() const
{
    QVariantMap result;
    const auto left = settings_.value(QStringLiteral("ui/panelLeftWidth"), 0).toInt();
    const auto right = settings_.value(QStringLiteral("ui/panelRightWidth"), 0).toInt();
    if (left > 0) {
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("left"), left);
    }
    if (right > 0) {
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("right"), right);
    }
    return result;
}

void AppController::savePanelWidths(int left, int right)
{
    if (left > 0)
        settings_.setValue(QStringLiteral("ui/panelLeftWidth"), left);
    if (right > 0)
        settings_.setValue(QStringLiteral("ui/panelRightWidth"), right);
}

QVariantMap AppController::savedWindowGeometry() const
{
    QVariantMap result;
    const auto width = settings_.value(QStringLiteral("window/width"), 0).toInt();
    const auto height = settings_.value(QStringLiteral("window/height"), 0).toInt();
    if (width <= 0 || height <= 0)
        return result;

    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("x"), settings_.value(QStringLiteral("window/x"), 0).toInt());
    result.insert(QStringLiteral("y"), settings_.value(QStringLiteral("window/y"), 0).toInt());
    result.insert(QStringLiteral("width"), width);
    result.insert(QStringLiteral("height"), height);
    return result;
}

void AppController::loadOneClickBindings(bool seedDefaults)
{
    commandStore_.clear();

    const auto items = settings_.value(QStringLiteral("oneClick/items")).toArray();
    if (items.isEmpty() && seedDefaults) {
        auto addDefault = [this](quint64 id,
                                 const QString& label,
                                 const QString& text,
                                 double value,
                                 double minValue,
                                 double maxValue,
                                 double step,
                                 bool include,
                                 int order) {
            const auto bytes = text.toUtf8();
            auto formatter = commands::TemplateFormatter::parse(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(bytes.constData()),
                    static_cast<std::size_t>(bytes.size())));
            if (!formatter)
                return;

            commandStore_.addCommand(id,
                                     label.toStdString(),
                                     text.toStdString(),
                                     std::move(*formatter));

            commands::SendBinding binding;
            binding.bindingId = id;
            binding.commandId = id;
            binding.label = label.toStdString();
            binding.group = QStringLiteral("默认").toStdString();
            binding.kind = commands::ControlKind::Slider;
            binding.includeInOneClickSend = include;
            binding.order = order;
            binding.minValue = minValue;
            binding.maxValue = maxValue;
            binding.stepSize = step;
            binding.args = {commands::CommandArg::fromFloat(value)};
            commandStore_.addBinding(std::move(binding));
        };

        addDefault(100, QStringLiteral("kp1"), QStringLiteral("kp1%.3f"),
                   0.75, 0.0, 5.0, 0.01, true, 1);
        addDefault(101, QStringLiteral("kp2"), QStringLiteral("kp2%.3f"),
                   0.01, 0.0, 5.0, 0.01, true, 2);
        addDefault(102, QStringLiteral("kd"), QStringLiteral("kd%.3f"),
                   0.10, 0.0, 5.0, 0.01, true, 3);
        addDefault(103, QStringLiteral("speed"), QStringLiteral("speed%.3f"),
                   1.20, 0.0, 10.0, 0.01, false, 4);
    } else {
        for (const auto& value : items) {
            const auto item = value.toObject();
            const auto id = static_cast<quint64>(item.value(QStringLiteral("id")).toDouble());
            if (id == 0)
                continue;

            const auto label = item.value(QStringLiteral("label")).toString();
            const auto text = item.value(QStringLiteral("template")).toString();
            const auto bytes = text.toUtf8();
            auto formatter = commands::TemplateFormatter::parse(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(bytes.constData()),
                    static_cast<std::size_t>(bytes.size())));
            if (!formatter)
                continue;

            commandStore_.addCommand(id,
                                     label.toStdString(),
                                     text.toStdString(),
                                     std::move(*formatter));

            commands::SendBinding binding;
            binding.bindingId = id;
            binding.commandId = id;
            binding.label = label.toStdString();
            auto group = item.value(QStringLiteral("group")).toString();
            if (group.isEmpty() || !commandGroups_.contains(group))
                group = commandGroups_.value(0);
            binding.group = group.toStdString();
            binding.kind = commands::ControlKind::Slider;
            binding.includeInOneClickSend =
                item.value(QStringLiteral("include")).toBool(true);
            binding.order = item.value(QStringLiteral("order")).toInt();
            binding.minValue = item.value(QStringLiteral("min")).toDouble(0.0);
            binding.maxValue = item.value(QStringLiteral("max")).toDouble(1.0);
            binding.stepSize = item.value(QStringLiteral("step")).toDouble(0.01);
            binding.args = {commands::CommandArg::fromFloat(
                item.value(QStringLiteral("value")).toDouble(0.0))};
            commandStore_.addBinding(std::move(binding));
        }
    }

    rebuildOneClickBindings();
}

quint64 AppController::nextBindingId() const
{
    quint64 next = 1000;
    for (const auto& binding : commandStore_.bindings())
        next = std::max<quint64>(next, static_cast<quint64>(binding.bindingId) + 1);
    return next;
}

bool AppController::addOneClickBinding(const QString& label,
                                       const QString& templateText,
                                       double minValue,
                                       double maxValue,
                                       double stepSize,
                                       double value,
                                       bool included,
                                       const QString& group)
{
    const auto bytes = templateText.toUtf8();
    auto formatter = commands::TemplateFormatter::parse(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.constData()),
            static_cast<std::size_t>(bytes.size())));
    if (!formatter)
        return false;

    captureEdit();
    const auto id = nextBindingId();
    commandStore_.addCommand(id,
                             label.toStdString(),
                             templateText.toStdString(),
                             std::move(*formatter));

    commands::SendBinding binding;
    binding.bindingId = id;
    binding.commandId = id;
    binding.label = label.toStdString();
    binding.group = (hasCommandGroup(group) ? group : currentGroupName()).toStdString();
    binding.kind = commands::ControlKind::Slider;
    binding.includeInOneClickSend = included;
    binding.order = static_cast<int>(commandStore_.bindings().size()) + 1;
    binding.minValue = std::min(minValue, maxValue);
    binding.maxValue = std::max(minValue, maxValue);
    binding.stepSize = std::max(0.0, stepSize);
    binding.args = {commands::CommandArg::fromFloat(
        std::clamp(value, binding.minValue, binding.maxValue))};
    commandStore_.addBinding(std::move(binding));

    rebuildOneClickBindings();
    persistOneClickBindings();
    return true;
}

bool AppController::updateOneClickBinding(qulonglong bindingId,
                                          const QString& label,
                                          const QString& templateText,
                                          double minValue,
                                          double maxValue,
                                          double stepSize,
                                          double value,
                                          bool included,
                                          const QString& group)
{
    auto* binding = commandStore_.findBinding(bindingId);
    if (binding == nullptr)
        return false;

    const auto bytes = templateText.toUtf8();
    auto formatter = commands::TemplateFormatter::parse(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.constData()),
            static_cast<std::size_t>(bytes.size())));
    if (!formatter)
        return false;

    captureEdit();
    if (commandStore_.findCommand(binding->commandId) != nullptr) {
        (void)commandStore_.updateCommand(binding->commandId,
                                          label.toStdString(),
                                          templateText.toStdString(),
                                          std::move(*formatter));
    } else {
        commandStore_.addCommand(binding->commandId,
                                 label.toStdString(),
                                 templateText.toStdString(),
                                 std::move(*formatter));
    }

    binding->label = label.toStdString();
    binding->group = (hasCommandGroup(group) ? group : currentGroupName()).toStdString();
    binding->includeInOneClickSend = included;
    binding->minValue = std::min(minValue, maxValue);
    binding->maxValue = std::max(minValue, maxValue);
    binding->stepSize = std::max(0.0, stepSize);
    binding->args = {commands::CommandArg::fromFloat(
        std::clamp(value, binding->minValue, binding->maxValue))};

    rebuildOneClickBindings();
    persistOneClickBindings();
    return true;
}

bool AppController::removeOneClickBinding(qulonglong bindingId)
{
    if (commandStore_.findBinding(bindingId) == nullptr)
        return false;
    captureEdit();
    if (!commandStore_.removeBinding(bindingId))
        return false;
    rebuildOneClickBindings();
    persistOneClickBindings();
    return true;
}

void AppController::rebuildOneClickBindings()
{
    QVariantList bindings;
    bindings.reserve(static_cast<int>(commandStore_.bindings().size()));
    const auto activeGroup = currentGroupName();
    for (const auto& binding : commandStore_.bindings()) {
        if (!commands::isValueControl(binding.kind))
            continue;
        if (QString::fromStdString(binding.group) != activeGroup)
            continue;

        const auto* command = commandStore_.findCommand(binding.commandId);

        QVariantMap item;
        item.insert(QStringLiteral("id"),
                    QVariant::fromValue<qulonglong>(binding.bindingId));
        item.insert(QStringLiteral("label"), QString::fromStdString(binding.label));
        item.insert(QStringLiteral("group"), QString::fromStdString(binding.group));
        item.insert(QStringLiteral("template"),
                    command != nullptr
                        ? QString::fromStdString(command->templateText)
                        : QString());
        item.insert(QStringLiteral("value"),
                    binding.args.empty() ? 0.0 : binding.args[0].floatValue);
        item.insert(QStringLiteral("min"), binding.minValue);
        item.insert(QStringLiteral("max"), binding.maxValue);
        item.insert(QStringLiteral("step"), binding.stepSize);
        item.insert(QStringLiteral("checked"), binding.includeInOneClickSend);
        item.insert(QStringLiteral("order"), binding.order);
        bindings.push_back(item);
    }

    oneClickBindings_ = std::move(bindings);
    emit oneClickBindingsChanged();
    scheduleRemoteState();
}

void AppController::persistOneClickBindings()
{
    QJsonArray items;
    for (const auto& binding : commandStore_.bindings()) {
        const auto* command = commandStore_.findCommand(binding.commandId);
        if (command == nullptr)
            continue;

        QJsonObject item;
        item.insert(QStringLiteral("id"), static_cast<double>(binding.bindingId));
        item.insert(QStringLiteral("label"), QString::fromStdString(binding.label));
        item.insert(QStringLiteral("group"), QString::fromStdString(binding.group));
        item.insert(QStringLiteral("template"), QString::fromStdString(command->templateText));
        item.insert(QStringLiteral("value"),
                    binding.args.empty() ? 0.0 : binding.args[0].floatValue);
        item.insert(QStringLiteral("min"), binding.minValue);
        item.insert(QStringLiteral("max"), binding.maxValue);
        item.insert(QStringLiteral("step"), binding.stepSize);
        item.insert(QStringLiteral("include"), binding.includeInOneClickSend);
        item.insert(QStringLiteral("order"), binding.order);
        items.append(item);
    }

    settings_.setValue(QStringLiteral("oneClick/items"), QJsonValue(items));
}

void AppController::sendBindingCommand(quint64 bindingId)
{
    const auto* binding = commandStore_.findBinding(bindingId);
    if (binding == nullptr)
        return;
    const auto* command = commandStore_.findCommand(binding->commandId);
    if (command == nullptr) {
        setError(QStringLiteral("绑定的命令不存在"));
        return;
    }

    std::vector<std::byte> payload;
    if (!command->formatter.render(binding->args, payload)) {
        setError(QStringLiteral("命令模板渲染失败：%1")
                     .arg(QString::fromStdString(command->templateText)));
        return;
    }

    const QByteArray data(reinterpret_cast<const char*>(payload.data()),
                          static_cast<int>(payload.size()));
    totalTxBytes_ += static_cast<qulonglong>(data.size());
    transport_.send(data);
    emit statsChanged();
}

void AppController::setBindingValue(qulonglong bindingId, double value)
{
    if (!commandStore_.setBindingValue(bindingId, value))
        return;

    auto* binding = commandStore_.findBinding(bindingId);
    const auto clamped = binding != nullptr ? binding->args.empty()
                                                  ? value
                                                  : binding->args[0].floatValue
                                            : value;

    for (auto& item : oneClickBindings_) {
        auto map = item.toMap();
        if (map.value(QStringLiteral("id")).toULongLong() == bindingId) {
            map.insert(QStringLiteral("value"), clamped);
            item = map;
            break;
        }
    }

    // Live tuning: send at most one command per 33 ms per drag. The final
    // value is sent unconditionally from commitBindingValue() on release.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (sendOnMove_
        && (lastSliderBinding_ != bindingId || now - lastSliderSendMs_ >= 33)) {
        lastSliderBinding_ = bindingId;
        lastSliderSendMs_ = now;
        sendBindingCommand(bindingId);
    }

    // Mirror the value to connected phones/tablets (coalesced to ~30 Hz).
    queueRemoteValue(bindingId, clamped);
}

bool AppController::setBindingValueExact(qulonglong bindingId, double value)
{
    if (!std::isfinite(value))
        return false;
    auto* binding = commandStore_.findBinding(bindingId);
    if (binding == nullptr)
        return false;

    if (binding->args.empty())
        binding->args.push_back(commands::CommandArg::fromFloat(value));
    else {
        binding->args[0].type = commands::CommandArg::Type::Float;
        binding->args[0].floatValue = value;
    }

    rebuildOneClickBindings();
    persistOneClickBindings();
    sendBindingCommand(bindingId);
    queueRemoteValue(bindingId, value);
    return true;
}

void AppController::commitBindingValue(qulonglong bindingId)
{
    if (commandStore_.findBinding(bindingId) == nullptr)
        return;
    persistOneClickBindings();
    sendBindingCommand(bindingId);
}

void AppController::setSendOnMove(bool enabled)
{
    if (sendOnMove_ == enabled)
        return;
    sendOnMove_ = enabled;
    settings_.setValue(QStringLiteral("slider/sendOnMove"), enabled);
    emit sendOnMoveChanged();
}

void AppController::setSerialBreak(bool enabled)
{
    if (serialBreak_ == enabled)
        return;
    serialBreak_ = enabled;
    // BREAK only exists while a serial port is open. For every other transport
    // the UI disables the control, so this is just a defensive no-op.
    if (transportKind_ == 0 && state_ == transport::ConnectionState::Connected)
        transport_.setBreak(enabled);
    emit serialBreakChanged();
}

void AppController::setBindingIncluded(qulonglong bindingId, bool included)
{
    const auto* existing = commandStore_.findBinding(bindingId);
    if (existing == nullptr || existing->includeInOneClickSend == included)
        return;
    captureEdit();
    if (!commandStore_.setBindingIncluded(bindingId, included))
        return;

    for (auto& item : oneClickBindings_) {
        auto map = item.toMap();
        if (map.value(QStringLiteral("id")).toULongLong() == bindingId) {
            map.insert(QStringLiteral("checked"), included);
            item = map;
            break;
        }
    }
    persistOneClickBindings();
    scheduleRemoteState();
}

void AppController::startOneClickSend()
{
    (void)startOneClickSendForGroup(currentGroupName(), nullptr);
}

bool AppController::startOneClickSendForGroup(const QString& group, QString* error)
{
    if (oneClickSending_) {
        if (error != nullptr)
            *error = QStringLiteral("一键发送正在进行中");
        return false;
    }

    // Never arm a run on a transport that cannot carry bytes: the write would
    // fail immediately and the dialog would sit on "已发送 1/N" forever.
    if (!sendReady()) {
        oneClickSteps_.clear();
        oneClickIndex_ = 0;
        oneClickStatus_ = (state_ == transport::ConnectionState::Connected)
            ? QStringLiteral("没有可发送的客户端：请先连接设备")
            : QStringLiteral("未连接：请先连接设备再一键发送");
        if (error != nullptr)
            *error = oneClickStatus_;
        emit oneClickStateChanged();
        pushRemoteOneClick(QStringLiteral("idle"));
        return false;
    }

    auto plan = commandStore_.buildOneClickPlan(oneClickIntervalMs_,
                                                group.toStdString());
    if (plan.steps.empty()) {
        oneClickStatus_ = plan.errors.empty()
            ? QStringLiteral("没有可发送的滑块")
            : QStringLiteral("部分命令无法生成，请检查命令模板");
        if (error != nullptr)
            *error = oneClickStatus_;
        emit oneClickStateChanged();
        pushRemoteOneClick(QStringLiteral("idle"));
        return false;
    }

    oneClickSteps_ = std::move(plan.steps);
    oneClickIndex_ = 0;
    oneClickSending_ = true;
    oneClickStatus_ = QStringLiteral("准备发送 %1 项").arg(oneClickSteps_.size());
    emit oneClickStateChanged();
    pushRemoteOneClick(QStringLiteral("start"));
    sendNextOneClickStep();
    return true;
}

void AppController::sendNextOneClickStep()
{
    if (!oneClickSending_)
        return;

    if (oneClickIndex_ >= static_cast<int>(oneClickSteps_.size())) {
        finishOneClickSend(false);
        return;
    }

    if (!sendReady()) {
        abortOneClickSend(QStringLiteral("已中断：连接已断开"));
        return;
    }

    const auto& step = oneClickSteps_[static_cast<std::size_t>(oneClickIndex_)];
    const QByteArray payload(
        reinterpret_cast<const char*>(step.payload.data()),
        static_cast<int>(step.payload.size()));

    const auto delay = step.delayAfterMs > 0 ? step.delayAfterMs : oneClickIntervalMs_;

    if (payload.isEmpty()) {
        // transport drops empty writes silently, so counting it as "sent"
        // would only mislead the progress dialog.
        ++oneClickIndex_;
        oneClickStatus_ = QStringLiteral("已跳过 %1/%2（空数据）: %3")
                              .arg(oneClickIndex_)
                              .arg(oneClickSteps_.size())
                              .arg(QString::fromStdString(step.label));
        emit oneClickStateChanged();
        pushRemoteOneClick(QStringLiteral("step"));
        oneClickTimer_.start(delay);
        return;
    }

    transport_.send(payload);

    // While no worker thread exists yet, sendFailed comes back synchronously
    // and has already stopped the run. Restarting the timer here would keep
    // reporting "已发送" for bytes that never left the process.
    if (!oneClickSending_)
        return;

    totalTxBytes_ += static_cast<qulonglong>(payload.size());
    emit statsChanged();

    ++oneClickIndex_;
    oneClickStatus_ = QStringLiteral("已发送 %1/%2: %3")
                          .arg(oneClickIndex_)
                          .arg(oneClickSteps_.size())
                          .arg(QString::fromStdString(step.label));
    emit oneClickStateChanged();
    pushRemoteOneClick(QStringLiteral("step"));

    oneClickTimer_.start(delay);
}

bool AppController::sendReady() const
{
    if (state_ != transport::ConnectionState::Connected)
        return false;
    // A listening TCP server reports "connected" but has no peer to write to
    // until a client joins.
    if (transportKind_ == 2 && activeClientId_ == 0)
        return false;
    return true;
}

void AppController::abortOneClickSend(const QString& status)
{
    oneClickTimer_.stop();
    oneClickSending_ = false;
    oneClickStatus_ = status;
    emit oneClickStateChanged();
    pushRemoteOneClick(QStringLiteral("cancel"));
}

void AppController::cancelOneClickSend()
{
    if (!oneClickSending_)
        return;
    finishOneClickSend(true);
}

void AppController::finishOneClickSend(bool cancelled)
{
    oneClickTimer_.stop();
    oneClickSending_ = false;
    oneClickStatus_ = cancelled ? QStringLiteral("已取消") : QStringLiteral("发送完成");
    emit oneClickStateChanged();
    pushRemoteOneClick(cancelled ? QStringLiteral("cancel") : QStringLiteral("done"));
}

void AppController::buildWaveform(int channel, int width, std::vector<float>& out) const
{
    out.clear();
    if (width <= 0 || channel < 0
        || static_cast<std::size_t>(channel) >= channelConfigs_.size()) {
        return;
    }

    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];
    if (!config.visible)
        return;

    width = std::min(width, 4096);
    const auto* history = store_.channel(static_cast<std::size_t>(channel));
    if (history == nullptr || history->size() == 0)
        return;

    auto viewEnd = history->absoluteEnd();
    if (waveformPanSamples_ > 0) {
        viewEnd -= std::min<std::size_t>(
            static_cast<std::size_t>(waveformPanSamples_), history->size());
    }

    std::size_t count = viewEnd - history->absoluteStart();
    const int windowSamples = effectiveWindowSamples();
    if (windowSamples > 0) {
        count = std::min(count, static_cast<std::size_t>(windowSamples));
    }
    const auto start = viewEnd - count;
    buildWaveformRange(channel, start, count, width, out);
}

void AppController::buildWaveformRange(int channel,
                                       std::uint64_t startAbsolute,
                                       std::size_t count,
                                       int width,
                                       std::vector<float>& out) const
{
    out.clear();
    if (width <= 0 || channel < 0
        || static_cast<std::size_t>(channel) >= channelConfigs_.size()) {
        return;
    }
    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];
    if (!config.visible)
        return;
    const auto* history = store_.channel(static_cast<std::size_t>(channel));
    if (history == nullptr || history->size() == 0)
        return;

    width = std::min(width, 8192);
    const auto historyStart = history->absoluteStart();
    const auto historyEnd = history->absoluteEnd();
    if (startAbsolute < historyStart) {
        const auto skip = historyStart - startAbsolute;
        if (skip >= count)
            return;
        count -= static_cast<std::size_t>(skip);
        startAbsolute = historyStart;
    }
    if (startAbsolute >= historyEnd)
        return;
    count = std::min<std::size_t>(
        count, static_cast<std::size_t>(historyEnd - startAbsolute));
    if (count == 0)
        return;

    out.reserve(static_cast<std::size_t>(width) * 2);
    for (int i = 0; i < width; ++i) {
        const auto begin = startAbsolute
            + (static_cast<std::uint64_t>(i) * count)
                / static_cast<std::uint64_t>(width);
        const auto end = startAbsolute
            + (static_cast<std::uint64_t>(i + 1) * count)
                / static_cast<std::uint64_t>(width);

        float minValue = 0.0f;
        float maxValue = 0.0f;
        if (end > begin) {
            (void)history->minMax(begin, static_cast<std::size_t>(end - begin),
                                  minValue, maxValue);
        } else if (begin < historyEnd) {
            (void)history->copyRange(begin, 1, &minValue);
            maxValue = minValue;
        }

        float outMin = static_cast<float>(minValue * config.scale + config.offset);
        float outMax = static_cast<float>(maxValue * config.scale + config.offset);
        if (outMin > outMax)
            std::swap(outMin, outMax);
        out.push_back(outMin);
        out.push_back(outMax);
    }
}

float AppController::sampleAt(int channel, std::uint64_t absoluteIndex) const
{
    if (channel < 0 || static_cast<std::size_t>(channel) >= channelConfigs_.size())
        return std::numeric_limits<float>::quiet_NaN();
    const auto* history = store_.channel(static_cast<std::size_t>(channel));
    if (history == nullptr || absoluteIndex < history->absoluteStart()
        || absoluteIndex >= history->absoluteEnd()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float value = 0.0f;
    if (!history->copyRange(absoluteIndex, 1, &value))
        return std::numeric_limits<float>::quiet_NaN();
    const auto& config = channelConfigs_[static_cast<std::size_t>(channel)];
    return static_cast<float>(value * config.scale + config.offset);
}

std::uint64_t AppController::dataAbsoluteStart() const
{
    const auto* history = store_.channel(0);
    return history != nullptr ? history->absoluteStart() : 0;
}

std::uint64_t AppController::dataAbsoluteEnd() const
{
    const auto* history = store_.channel(0);
    return history != nullptr ? history->absoluteEnd() : 0;
}

QVariantList AppController::waveformSnapshot(int channel, int width) const
{
    std::vector<float> values;
    buildWaveform(channel, width, values);

    QVariantList result;
    result.reserve(static_cast<int>(values.size()));
    for (const float value : values)
        result.push_back(static_cast<double>(value));
    return result;
}

int AppController::waveformChannelCount() const
{
    return static_cast<int>(std::min<std::size_t>(
        channelConfigs_.size(), store_.channelCount()));
}

bool AppController::channelVisible(int channel) const
{
    return channel >= 0
        && static_cast<std::size_t>(channel) < channelConfigs_.size()
        && channelConfigs_[static_cast<std::size_t>(channel)].visible;
}

QColor AppController::channelColor(int channel) const
{
    if (channel < 0 || static_cast<std::size_t>(channel) >= channelConfigs_.size())
        return {};
    return QColor(channelConfigs_[static_cast<std::size_t>(channel)].color);
}

QString AppController::exportCsv()
{
    const auto count = store_.channelCount();
    if (count == 0)
        return QStringLiteral("没有可导出的数据");

    std::uint64_t start = 0;
    std::uint64_t end = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t i = 0; i < count; ++i) {
        const auto* history = store_.channel(i);
        if (history == nullptr)
            return QStringLiteral("没有可导出的数据");
        start = std::max(start, history->absoluteStart());
        end = std::min(end, history->absoluteEnd());
    }
    if (end <= start)
        return QStringLiteral("没有可导出的数据");

    // Bounded export: newest rows first, so a long soak does not produce a
    // multi-gigabyte file by accident.
    constexpr std::uint64_t kMaxRows = 200000;
    std::uint64_t rows = end - start;
    if (rows > kMaxRows) {
        start = end - kMaxRows;
        rows = kMaxRows;
    }

    QDir dir(configDir());
    if (!dir.mkpath(QStringLiteral("exports")))
        return QStringLiteral("无法创建导出目录");

    const auto stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const auto path = dir.filePath(QStringLiteral("exports/vofa-%1.csv").arg(stamp));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入 %1").arg(path);

    QByteArray header("index");
    for (std::size_t c = 0; c < count; ++c)
        header += ",CH" + QByteArray::number(static_cast<int>(c));
    header += '\n';
    if (file.write(header) != header.size()) {
        file.cancelWriting();
        return QStringLiteral("写入失败：%1").arg(path);
    }

    constexpr std::size_t kChunkRows = 2048;
    std::vector<std::vector<float>> columns(count,
                                            std::vector<float>(kChunkRows));
    std::uint64_t offset = start;
    QByteArray block;

    while (offset < end) {
        const auto n = static_cast<std::size_t>(
            std::min<std::uint64_t>(kChunkRows, end - offset));
        for (std::size_t c = 0; c < count; ++c) {
            const auto* history = store_.channel(c);
            if (history == nullptr
                || !history->copyRange(offset, n, columns[c].data())) {
                file.cancelWriting();
                return QStringLiteral("读取波形历史失败");
            }
        }

        block.clear();
        block.reserve(static_cast<int>(n * count * 12 + 64));
        for (std::size_t r = 0; r < n; ++r) {
            block += QByteArray::number(static_cast<qulonglong>(offset + r));
            for (std::size_t c = 0; c < count; ++c) {
                block += ',';
                block += QByteArray::number(static_cast<double>(columns[c][r]), 'g', 9);
            }
            block += '\n';
        }

        if (file.write(block) != block.size()) {
            file.cancelWriting();
            return QStringLiteral("写入失败：%1").arg(path);
        }
        offset += n;
    }

    if (!file.commit())
        return QStringLiteral("写入失败：%1").arg(path);

    return QStringLiteral("已导出 %1 行 × %2 通道 → %3")
        .arg(rows)
        .arg(count)
        .arg(path);
}

QString AppController::chooseFileToSend()
{
#ifdef Q_OS_WIN
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    if (auto* window = QGuiApplication::focusWindow())
        dialog.hwndOwner = reinterpret_cast<HWND>(window->winId());
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"所有文件\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != 0)
        return QString::fromWCharArray(filename);
#endif
    return {};
}

QString AppController::sendFile(const QString& path)
{
    if (path.isEmpty())
        return QStringLiteral("未选择文件");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("无法读取文件：%1").arg(path);

    constexpr qint64 kMaxFileBytes = 16 * 1024 * 1024;
    if (file.size() > kMaxFileBytes)
        return QStringLiteral("文件超过 16 MB，已取消发送");

    const auto data = file.read(kMaxFileBytes + 1);
    if (data.size() > kMaxFileBytes)
        return QStringLiteral("文件超过 16 MB，已取消发送");

    totalTxBytes_ += static_cast<qulonglong>(data.size());
    transport_.send(data);
    appendTxEcho(QStringLiteral("文件 %1 (%2 字节)")
                     .arg(QFileInfo(path).fileName())
                     .arg(data.size()),
                 false);
    emit statsChanged();
    return QStringLiteral("已发送 %1 字节：%2").arg(data.size()).arg(path);
}

QString AppController::newScreenshotPath() const
{
    QDir dir(configDir());
    if (!dir.mkpath(QStringLiteral("screenshots")))
        return {};
    return dir.filePath(QStringLiteral("screenshots/vofa2-%1.png")
                            .arg(QDateTime::currentDateTime().toString(
                                QStringLiteral("yyyyMMdd-HHmmss"))));
}

QString AppController::importLegacyFromDefault()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/vofa+");
    QDir dir(base);
    if (!dir.exists())
        return QStringLiteral("未找到旧版配置目录：%1").arg(base);

    const auto versions = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                        QDir::Name | QDir::Reversed);

    // Old installations create one directory per data version. The newest one
    // is not necessarily the user's real workspace (it may contain only a
    // chart), so pick the workspace with the most sliders/toggles.
    QString bestContext;
    int bestScore = -1;
    for (const auto& version : versions) {
        const auto context = dir.filePath(version + QStringLiteral("/context"));
        if (!QFile::exists(context + QStringLiteral("/vofa+.cmds.json")))
            continue;

        int score = 0;
        QFile tabviews(context + QStringLiteral("/vofa+.tabviews.json"));
        if (tabviews.open(QIODevice::ReadOnly)) {
            const auto document = QJsonDocument::fromJson(tabviews.readAll());
            std::function<void(const QJsonValue&)> count = [&](const QJsonValue& value) {
                if (value.isObject()) {
                    const auto object = value.toObject();
                    const auto path = object.value(QStringLiteral("path")).toString();
                    if (path == QStringLiteral("slider")
                        || path == QStringLiteral("ExtraButtonToggle")) {
                        ++score;
                    }
                    for (auto it = object.begin(); it != object.end(); ++it)
                        count(it.value());
                } else if (value.isArray()) {
                    for (const auto& child : value.toArray())
                        count(child);
                }
            };
            count(document.object().value(QStringLiteral("ctx")));
        }

        if (score > bestScore) {
            bestScore = score;
            bestContext = context;
        }
    }

    if (bestContext.isEmpty())
        return QStringLiteral("未找到 vofa+.cmds.json");

    QString message;
    (void)importLegacyDirectory(bestContext, &message);
    return message + QStringLiteral("（来源：%1）").arg(bestContext);
}

QString AppController::importLegacyFromDirectory(const QString& contextDir)
{
    QString message;
    (void)importLegacyDirectory(contextDir, &message);
    return message;
}

bool AppController::importLegacyDirectory(const QString& contextDir, QString* message)
{
    QFile commandsFile(contextDir + QStringLiteral("/vofa+.cmds.json"));
    if (!commandsFile.open(QIODevice::ReadOnly)) {
        *message = QStringLiteral("无法读取旧命令文件");
        return false;
    }

    const auto commandsDocument = QJsonDocument::fromJson(commandsFile.readAll());
    if (!commandsDocument.isObject()) {
        *message = QStringLiteral("旧命令文件格式错误");
        return false;
    }

    // Copy the legacy files before importing. The old app writes without
    // atomic replacement, so a truncated/corrupt file should stay recoverable.
    const auto backupDir = configDir()
        + QStringLiteral("/import/legacy-backup/")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    bool backedUp = QDir().mkpath(backupDir);
    if (backedUp)
        backedUp = QFile::copy(contextDir + QStringLiteral("/vofa+.cmds.json"),
                               backupDir + QStringLiteral("/vofa+.cmds.json"));
    const auto legacyTabviewsPath =
        contextDir + QStringLiteral("/vofa+.tabviews.json");
    if (backedUp && QFile::exists(legacyTabviewsPath))
        (void)QFile::copy(legacyTabviewsPath,
                          backupDir + QStringLiteral("/vofa+.tabviews.json"));

    std::vector<QJsonObject> leaves;
    std::vector<QString> leafGroups;
    std::function<void(const QJsonObject&, const QString&)> walk =
        [&](const QJsonObject& group, const QString& parentGroup) {
            const auto children = group.value(QStringLiteral("subCmds")).toArray();
            for (const auto& child : children) {
                const auto object = child.toObject();
                if (object.value(QStringLiteral("is_group")).toBool()) {
                    const auto name = normalizeGroupPath(
                        object.value(QStringLiteral("name")).toString());
                    auto path = parentGroup;
                    if (!name.isEmpty()) {
                        path = path.isEmpty()
                            ? name
                            : path + QLatin1Char('/') + name;
                    }
                    walk(object, path);
                } else {
                    leaves.push_back(object);
                    leafGroups.push_back(parentGroup);
                }
            }
        };
    walk(commandsDocument.object().value(QStringLiteral("ctx")).toObject(), QString());

    // The migration report records everything that was skipped or could not be
    // mapped, so a failed import never leaves the user guessing.
    QJsonObject report;
    report.insert(QStringLiteral("generatedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    report.insert(QStringLiteral("source"), contextDir);
    report.insert(QStringLiteral("backup"), backedUp ? backupDir : QString());
    QJsonArray commandSkips;
    QJsonArray sliderSkips;
    QJsonArray toggleSkips;
    QJsonObject unknownWidgets;
    int sliderTotal = 0;
    int sliderImported = 0;
    int toggleTotal = 0;
    int toggleImported = 0;
    bool fallbackBindings = false;

    auto writeReport = [&](const QString& status, const QString& summary) {
        report.insert(QStringLiteral("status"), status);
        report.insert(QStringLiteral("message"), summary);

        QJsonObject sliders;
        sliders.insert(QStringLiteral("total"), sliderTotal);
        sliders.insert(QStringLiteral("imported"), sliderImported);
        sliders.insert(QStringLiteral("skipped"), sliderSkips);
        QJsonObject toggles;
        toggles.insert(QStringLiteral("total"), toggleTotal);
        toggles.insert(QStringLiteral("imported"), toggleImported);
        toggles.insert(QStringLiteral("skipped"), toggleSkips);
        QJsonObject widgets;
        widgets.insert(QStringLiteral("sliders"), sliders);
        widgets.insert(QStringLiteral("toggles"), toggles);
        report.insert(QStringLiteral("widgets"), widgets);
        report.insert(QStringLiteral("unknownWidgetPaths"), unknownWidgets);
        report.insert(QStringLiteral("fallbackBindings"), fallbackBindings);

        QSaveFile file(migrationReportPath());
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "cannot write migration report" << file.errorString();
            return;
        }
        file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        if (!file.commit())
            qWarning() << "cannot commit migration report" << file.errorString();
    };

    struct LegacyCommand {
        quint64 id = 0;
        QString name;
        QByteArray bytes;
        commands::TemplateFormatter formatter;
    };

    std::vector<LegacyCommand> commands;
    QHash<int, quint64> indexToCommand;

    struct LegacyRaw {
        QString name;
        QString text;
    };
    QHash<int, LegacyRaw> rawByIndex;
    QHash<int, QString> groupByIndex;

    for (std::size_t i = 0; i < leaves.size(); ++i) {
        const auto& leaf = leaves[i];
        const auto name = leaf.value(QStringLiteral("name")).toString();
        QString hex = leaf.value(QStringLiteral("cmd_hex")).toString();
        hex.remove(QRegularExpression(QStringLiteral("\\s+")));
        const auto bytes = QByteArray::fromHex(hex.toLatin1());
        if (bytes.isEmpty()) {
            QJsonObject skip;
            skip.insert(QStringLiteral("index"), static_cast<int>(i));
            skip.insert(QStringLiteral("name"), name);
            skip.insert(QStringLiteral("reason"), QStringLiteral("empty cmd_hex"));
            commandSkips.append(skip);
            continue;
        }

        rawByIndex.insert(static_cast<int>(i),
                          {name, QString::fromUtf8(bytes)});
        groupByIndex.insert(static_cast<int>(i),
                            leafGroups[i].isEmpty() ? QStringLiteral("默认")
                                                    : leafGroups[i]);

        auto formatter = commands::TemplateFormatter::parse(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.constData()),
                static_cast<std::size_t>(bytes.size())));
        if (!formatter || formatter->isRaw() || formatter->slotCount() == 0) {
            QJsonObject skip;
            skip.insert(QStringLiteral("index"), static_cast<int>(i));
            skip.insert(QStringLiteral("name"), name);
            skip.insert(QStringLiteral("reason"),
                        QStringLiteral("raw command without %f/%d/%s slot"));
            commandSkips.append(skip);
            continue;
        }

        const auto id = 1000 + static_cast<quint64>(i);
        commands.push_back({id, name, bytes, std::move(*formatter)});
        indexToCommand.insert(static_cast<int>(i), id);
    }

    QJsonObject commandStats;
    commandStats.insert(QStringLiteral("total"), static_cast<int>(leaves.size()));
    commandStats.insert(QStringLiteral("numeric"), static_cast<int>(commands.size()));
    commandStats.insert(QStringLiteral("raw"),
                        static_cast<int>(rawByIndex.size())
                            - static_cast<int>(commands.size()));
    commandStats.insert(QStringLiteral("skipped"), commandSkips);
    report.insert(QStringLiteral("commands"), commandStats);

    if (commands.empty()) {
        const auto text = QStringLiteral("旧命令中没有找到可导入的数值命令");
        *message = text;
        writeReport(QStringLiteral("failed"), text);
        return false;
    }

    commandStore_.clear();
    if (!commandGroups_.contains(QStringLiteral("默认")))
        commandGroups_.push_back(QStringLiteral("默认"));

    // Preserve the legacy command tree as slash-separated groups. The root
    // group itself is implicit, so a flat file still imports into 默认.
    QStringList importedGroups;
    for (const auto& group : leafGroups) {
        for (const auto& ancestor : groupAncestors(group)) {
            if (!importedGroups.contains(ancestor))
                importedGroups.push_back(ancestor);
        }
        if (!group.isEmpty() && !importedGroups.contains(group))
            importedGroups.push_back(group);
    }
    for (const auto& group : importedGroups) {
        if (!commandGroups_.contains(group))
            commandGroups_.push_back(group);
    }

    currentGroup_ = commandGroups_.indexOf(QStringLiteral("默认"));
    persistCommandGroups();
    for (auto& command : commands) {
        commandStore_.addCommand(
            command.id,
            command.name.toStdString(),
            std::string(command.bytes.constData(), static_cast<std::size_t>(command.bytes.size())),
            std::move(command.formatter));
    }

    int bindingCount = 0;
    int actionCount = 0;
    QSet<quint64> usedCommands;
    actions_.clear();

    QFile tabviewsFile(contextDir + QStringLiteral("/vofa+.tabviews.json"));
    if (tabviewsFile.open(QIODevice::ReadOnly)) {
        const auto tabviewsDocument = QJsonDocument::fromJson(tabviewsFile.readAll());
        std::vector<QJsonObject> sliders;
        std::vector<QJsonObject> toggles;
        std::function<void(const QJsonValue&)> collect = [&](const QJsonValue& value) {
            if (value.isObject()) {
                const auto object = value.toObject();
                const auto path = object.value(QStringLiteral("path")).toString();
                if (path == QStringLiteral("slider"))
                    sliders.push_back(object);
                else if (path == QStringLiteral("ExtraButtonToggle"))
                    toggles.push_back(object);
                else if (!path.isEmpty())
                    unknownWidgets.insert(
                        path, unknownWidgets.value(path).toInt() + 1);
                for (auto it = object.begin(); it != object.end(); ++it)
                    collect(it.value());
            } else if (value.isArray()) {
                for (const auto& child : value.toArray())
                    collect(child);
            }
        };
        collect(tabviewsDocument.object().value(QStringLiteral("ctx")));
        sliderTotal = static_cast<int>(sliders.size());
        toggleTotal = static_cast<int>(toggles.size());

        auto widgetLabel = [](const QJsonObject& ctx) -> QString {
            const auto label = ctx.value(QStringLiteral("name_menu"))
                                   .toObject()
                                   .value(QStringLiteral("ctx"))
                                   .toObject()
                                   .value(QStringLiteral("attr"))
                                   .toObject()
                                   .value(QStringLiteral("name_"))
                                   .toString();
            if (label.isEmpty() || label == QStringLiteral("name"))
                return {};
            return label;
        };
        auto skipEntry = [](int index, const QString& label, const QString& reason) {
            QJsonObject skip;
            if (index >= 0)
                skip.insert(QStringLiteral("index"), index);
            if (!label.isEmpty())
                skip.insert(QStringLiteral("label"), label);
            skip.insert(QStringLiteral("reason"), reason);
            return skip;
        };

        int order = 1;
        for (const auto& slider : sliders) {
            const auto ctx = slider.value(QStringLiteral("ctx")).toObject();
            const auto indices = ctx.value(QStringLiteral("cmd_menu"))
                                     .toObject()
                                     .value(QStringLiteral("ctx"))
                                     .toArray();
            if (indices.isEmpty()) {
                sliderSkips.append(
                    skipEntry(-1, widgetLabel(ctx),
                              QStringLiteral("widget has no cmd_menu index")));
                continue;
            }

            const auto oldIndex = indices.first().toInt(-1);
            const auto commandIt = indexToCommand.constFind(oldIndex);
            if (commandIt == indexToCommand.constEnd()) {
                sliderSkips.append(
                    skipEntry(oldIndex, widgetLabel(ctx),
                              QStringLiteral("command index is missing or not a "
                                             "numeric template")));
                continue;
            }
            if (usedCommands.contains(*commandIt)) {
                sliderSkips.append(
                    skipEntry(oldIndex, widgetLabel(ctx),
                              QStringLiteral("command already bound by another "
                                             "widget")));
                continue;
            }

            const auto dot = ctx.value(QStringLiteral(".")).toObject();
            const auto lower = dot.value(QStringLiteral("from")).toDouble(0.0);
            const auto upper = dot.value(QStringLiteral("to")).toDouble(10.0);
            const auto step = dot.value(QStringLiteral("step_size")).toDouble(0.01);
            const auto value = ctx.value(QStringLiteral("slider_inside"))
                                   .toObject()
                                   .value(QStringLiteral("target_value"))
                                   .toDouble(0.0);

            QString label = widgetLabel(ctx);
            if (label.isEmpty()) {
                for (const auto& command : commands) {
                    if (command.id == *commandIt) {
                        label = command.name;
                        break;
                    }
                }
            }

            commands::SendBinding binding;
            binding.bindingId = *commandIt;
            binding.commandId = *commandIt;
            binding.label = label.toStdString();
            binding.group = groupByIndex.value(oldIndex, QStringLiteral("默认"))
                                .toStdString();
            binding.kind = commands::ControlKind::Slider;
            binding.includeInOneClickSend = true;
            binding.order = order++;
            binding.minValue = std::min(lower, upper);
            binding.maxValue = std::max(lower, upper);
            binding.stepSize = std::max(0.0, step);
            binding.args = {commands::CommandArg::fromFloat(
                std::clamp(value, binding.minValue, binding.maxValue))};

            commandStore_.addBinding(std::move(binding));
            usedCommands.insert(*commandIt);
            ++bindingCount;
            ++sliderImported;
        }

        // Legacy ExtraButtonToggle widgets send one raw command per state.
        // Pair well-known complementary commands (start/stop, brushon/brushoff,
        // left/right, ...) into a real on/off toggle.
        struct NamePair {
            const char* first;
            const char* second;
        };
        static constexpr std::array<NamePair, 7> kNamePairs {{
            {"start", "stop"},
            {"brushon", "brushoff"},
            {"left", "right"},
            {"forward", "back"},
            {"on", "off"},
            {"enable", "disable"},
            {"open", "close"},
        }};

        auto complementFor = [](const QString& name) -> QString {
            for (const auto& pair : kNamePairs) {
                if (name.compare(QLatin1String(pair.first), Qt::CaseInsensitive) == 0)
                    return QString::fromLatin1(pair.second);
                if (name.compare(QLatin1String(pair.second), Qt::CaseInsensitive) == 0)
                    return QString::fromLatin1(pair.first);
            }
            return {};
        };

        QSet<QString> usedActionOn;
        for (const auto& toggle : toggles) {
            const auto ctx = toggle.value(QStringLiteral("ctx")).toObject();
            const auto indices = ctx.value(QStringLiteral("cmd_menu"))
                                     .toObject()
                                     .value(QStringLiteral("ctx"))
                                     .toArray();
            if (indices.isEmpty()) {
                toggleSkips.append(
                    skipEntry(-1, widgetLabel(ctx),
                              QStringLiteral("widget has no cmd_menu index")));
                continue;
            }
            const auto oldIndex = indices.first().toInt(-1);
            const auto rawIt = rawByIndex.constFind(oldIndex);
            if (rawIt == rawByIndex.constEnd() || rawIt->text.isEmpty()) {
                toggleSkips.append(
                    skipEntry(oldIndex, widgetLabel(ctx),
                              QStringLiteral("command index is missing or has "
                                             "empty cmd_hex")));
                continue;
            }
            if (usedActionOn.contains(rawIt->text)) {
                toggleSkips.append(
                    skipEntry(oldIndex, widgetLabel(ctx),
                              QStringLiteral("command already imported as a "
                                             "toggle state")));
                continue;
            }

            QString label = widgetLabel(ctx);
            if (label.isEmpty())
                label = rawIt->name;

            QString offText;
            const auto complement = complementFor(rawIt->name);
            if (!complement.isEmpty()) {
                for (auto it = rawByIndex.constBegin(); it != rawByIndex.constEnd(); ++it) {
                    if (it->name.compare(complement, Qt::CaseInsensitive) == 0) {
                        offText = it->text;
                        break;
                    }
                }
            }

            // The legacy workspace often binds both directions of a pair as
            // separate toggles (start + stop). Import them once as a single
            // two-state toggle instead of duplicating the pair.
            if (!offText.isEmpty() && usedActionOn.contains(offText)) {
                toggleSkips.append(
                    skipEntry(oldIndex, widgetLabel(ctx),
                              QStringLiteral("paired with the complementary "
                                             "toggle already imported")));
                continue;
            }

            ActionEntry entry;
            entry.id = 2000 + static_cast<quint64>(actions_.size());
            entry.label = label;
            entry.group = groupByIndex.value(oldIndex, QStringLiteral("默认"));
            entry.onTemplate = rawIt->text;
            entry.offTemplate = offText;
            entry.kind = 1;
            actions_.push_back(std::move(entry));
            usedActionOn.insert(rawIt->text);
            if (!offText.isEmpty())
                usedActionOn.insert(offText);
            ++actionCount;
            ++toggleImported;
        }
    }

    persistActionBindings();
    rebuildActionBindings();

    if (bindingCount == 0) {
        int order = 1;
        for (const auto& command : commands) {
            commands::SendBinding binding;
            binding.bindingId = command.id;
            binding.commandId = command.id;
            binding.label = command.name.toStdString();
            binding.group =
                groupByIndex.value(static_cast<int>(command.id - 1000),
                                   QStringLiteral("默认"))
                    .toStdString();
            binding.kind = commands::ControlKind::Slider;
            binding.includeInOneClickSend = false;
            binding.order = order++;
            binding.minValue = 0.0;
            binding.maxValue = 10.0;
            binding.stepSize = 0.01;
            binding.args = {commands::CommandArg::fromFloat(0.0)};
            commandStore_.addBinding(std::move(binding));
        }
        bindingCount = static_cast<int>(commands.size());
        fallbackBindings = true;
    }

    rebuildOneClickBindings();
    persistOneClickBindings();
    persistCommandGroups();
    emit commandGroupsChanged();
    emit currentGroupChanged();
    const auto summary = QStringLiteral("已导入 %1 条数值命令、%2 个滑块绑定、%3 个动作")
                             .arg(static_cast<int>(commands.size()))
                             .arg(bindingCount)
                             .arg(actionCount);
    *message = summary;
    writeReport(QStringLiteral("ok"), summary);
    // Import replaces the whole workspace; undoing into a half-merged legacy
    // state would be confusing, so start a fresh edit history.
    clearEditHistory();
    return true;
}

QString AppController::migrationReportPath() const
{
    return configDir() + QStringLiteral("/migration-report.json");
}

QString AppController::openMigrationReport() const
{
    const auto path = migrationReportPath();
    if (!QFile::exists(path))
        return QStringLiteral("尚无迁移报告");
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return QStringLiteral("无法打开迁移报告：%1").arg(path);
    return QStringLiteral("已打开迁移报告");
}

void AppController::requestDialog(const QString& name)
{
    emit dialogRequested(name);
}

void AppController::onStateChanged(transport::ConnectionState state, const QString& error)
{
    state_ = state;
    if (state != transport::ConnectionState::Disconnected && replayActive_)
        stopReplay();
    if (state != transport::ConnectionState::Connected) {
        listeningPort_ = 0;
        // The port is closed (or closing); a latched BREAK is no longer valid.
        if (serialBreak_) {
            serialBreak_ = false;
            emit serialBreakChanged();
        }
    }
    if (state == transport::ConnectionState::Disconnected
        && autoReconnect_ && lastConfigValid_ && !userClosed_
        && !reconnectTimer_.isActive()) {
        reconnectTimer_.start();
    }
    lastError_ = error;
    emit connectionStateChanged();
    emit lastErrorChanged();
    pushRemoteConn();
}

void AppController::onReceived(quint64 /*generation*/,
                               quint64 /*clientId*/,
                               const QByteArray& data)
{
    totalRxBytes_ += static_cast<qulonglong>(data.size());
    if (receivePaused_)
        return;
    // "send only" hides incoming traffic from the terminal log.
    if (receiveFilterDirection_ == 2)
        return;

    // HEX mode logs every byte, including binary sample protocols.
    if (receiveHex_) {
        appendHexChunk(data);
        return;
    }

    // Binary sample protocols are shown through the channel values/waveform.
    // Only text-oriented protocols go to the terminal log.
    if (protocolKind_ != protocol::ProtocolKind::RawData
        && protocolKind_ != protocol::ProtocolKind::FireWater) {
        return;
    }

    const QString decoded = textDecoder_.decode(data);

    if (!receiveFilter_.isEmpty()) {
        filterLineBuffer_ += decoded;
        int newline = -1;
        while ((newline = filterLineBuffer_.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = filterLineBuffer_.left(newline);
            filterLineBuffer_.remove(0, newline + 1);
            const QString html =
                receiveAnsi_ ? ansiToHtml(line + QLatin1Char('\n')) : QString();
            if (!lineMatchesReceiveFilter(line))
                continue;

            QString prefix;
            if (receiveTimestamps_) {
                prefix = QLatin1Char('[')
                    + QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                    + QStringLiteral("] ");
            }
            if (receiveAnsi_) {
                if (!prefix.isEmpty()) {
                    pendingAnsi_ += QStringLiteral("<span style=\"color:#7f8c8d\">")
                        + prefix.toHtmlEscaped() + QStringLiteral("</span>");
                }
                pendingAnsi_ += html;
            } else {
                pendingReceive_ += prefix + line + QLatin1Char('\n');
            }
        }

        // A very long line without a newline must not grow without bound.
        if (filterLineBuffer_.size() > 65536) {
            const QString line = filterLineBuffer_;
            filterLineBuffer_.clear();
            const QString html = receiveAnsi_ ? ansiToHtml(line) : QString();
            if (lineMatchesReceiveFilter(line)) {
                if (receiveAnsi_)
                    pendingAnsi_ += html;
                else
                    pendingReceive_ += line;
            }
        }
    } else {
        QString prefix;
        if (receiveTimestamps_) {
            prefix = QLatin1Char('[')
                + QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                + QStringLiteral("] ");
        }
        if (receiveAnsi_) {
            if (!prefix.isEmpty()) {
                pendingAnsi_ += QStringLiteral("<span style=\"color:#7f8c8d\">")
                    + prefix.toHtmlEscaped() + QStringLiteral("</span>");
            }
            pendingAnsi_ += ansiToHtml(decoded);
        } else {
            pendingReceive_ += prefix + decoded;
        }
    }

    if (pendingReceive_.size() > 262144)
        pendingReceive_ = pendingReceive_.right(262144);
    if (pendingAnsi_.size() > 1048576)
        pendingAnsi_ = pendingAnsi_.right(1048576);
}

void AppController::onClientsChanged(const QList<quint64>& ids,
                                     const QStringList& labels,
                                     quint64 activeId)
{
    QVariantList variantIds;
    variantIds.reserve(ids.size());
    for (const auto id : ids)
        variantIds.push_back(QVariant::fromValue<qulonglong>(id));

    clientIds_ = std::move(variantIds);
    clientLabels_ = labels;
    activeClientId_ = activeId;
    emit clientsChanged();
}

void AppController::onStreamReset(quint64 /*generation*/)
{
    stopReplay();
    hasReplayRecording_ = false;
    replaySamples_.clear();
    replayChannels_ = 0;
    replayRows_ = 0;
    replayPosition_ = 0;
    store_.clearHistories();
    latestValues_.clear();
    channelCount_ = 0;
    lastFramesStored_ = 0;
    clearTrigger();
    emit replayChanged();
    ++waveformRevision_;
    emit latestValuesChanged();
    emit channelSettingsChanged();
    emit waveformChanged();
}

void AppController::onSendFailed(const QString& message)
{
    setError(message);
    if (!oneClickSending_)
        return;
    abortOneClickSend(QStringLiteral("发送失败：%1").arg(message));
}

void AppController::onErrorOccurred(const QString& message)
{
    setError(message);
}

QString AppController::imageSource() const
{
    if (imageCount_ == 0 || latestImageId_ < 0)
        return {};
    return QStringLiteral("image://vofa/%1?rev=%2").arg(latestImageId_).arg(imageRevision_);
}

void AppController::onImageReceived(quint64 /*generation*/,
                                    quint32 imageId,
                                    int format,
                                    int width,
                                    int height,
                                    const QByteArray& data)
{
    if (data.isEmpty())
        return;

    const auto id = static_cast<int>(imageId & 0x7fffffffU);
    const auto sequence = imageProvider_->setImage(id, data);
    if (sequence == 0)
        return;

    // Decoding happens on the provider's worker thread. Only announce the
    // frame (and bump the QML source revision) once the image is ready.
    pendingImageInfo_.insert(
        sequence, QStringLiteral("img%1 · %2×%3 · %4 B · 格式 %5")
                      .arg(id)
                      .arg(width)
                      .arg(height)
                      .arg(data.size())
                      .arg(format));
    for (auto it = pendingImageInfo_.begin(); it != pendingImageInfo_.end();) {
        if (it.key() + 64 < sequence)
            it = pendingImageInfo_.erase(it);
        else
            ++it;
    }
}

void AppController::onUpdateTimer()
{
    // Drain the whole frame ring each tick. At 60 Hz this consumes up to
    // ~2M frames/s, enough for a 20 MB/s burst of small JustFloat frames.
    const auto drained = store_.drain(limits::kDefaultFrameRingCapacity);
    framesInStatsWindow_ += static_cast<qulonglong>(drained);
    checkTrigger();

    QVariantList values;
    const auto count = store_.channelCount();
    values.reserve(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        float value = 0.0f;
        const auto* history = store_.channel(i);
        if (history != nullptr && history->size() > 0)
            (void)history->copyRange(history->absoluteEnd() - 1, 1, &value);
        values.push_back(static_cast<double>(value));
    }

    if (values != latestValues_ || static_cast<int>(count) != channelCount_) {
        latestValues_ = std::move(values);
        channelCount_ = static_cast<int>(count);
        if (count > 0) {
            if (channelConfigs_.size() != count)
                ensureChannelConfigs(count);
            else
                emit channelSettingsChanged();
        } else {
            emit channelSettingsChanged();
        }
        emit latestValuesChanged();
    }

    const auto frames = store_.framesStored();
    if (frames != lastFramesStored_) {
        lastFramesStored_ = frames;
        if (!waveformPaused_) {
            ++waveformRevision_;
            emit waveformChanged();
        }
    }

    if (!pendingReceive_.isEmpty()) {
        receiveText_ += pendingReceive_;
        pendingReceive_.clear();
        if (receiveText_.size() > 262144)
            receiveText_ = receiveText_.right(262144);
        if (!receiveHex_ && !receiveAnsi_)
            emit receiveTextChanged();
    }

    if (!pendingAnsi_.isEmpty()) {
        receiveAnsiText_ += pendingAnsi_;
        pendingAnsi_.clear();
        if (receiveAnsiText_.size() > 524288) {
            int cut = receiveAnsiText_.size() - 480000;
            cut = receiveAnsiText_.indexOf(QStringLiteral("</span>"), cut);
            receiveAnsiText_ = cut >= 0 ? receiveAnsiText_.mid(cut + 7)
                                        : receiveAnsiText_.right(480000);
        }
        if (receiveAnsi_)
            emit receiveTextChanged();
    }

    if (!pendingHex_.isEmpty()) {
        receiveHexText_ += pendingHex_;
        pendingHex_.clear();
        if (receiveHexText_.size() > 524288)
            receiveHexText_ = receiveHexText_.right(524288);
        if (receiveHex_)
            emit receiveTextChanged();
    }

    // Throughput window: at most one notification per second regardless of
    // the incoming packet rate.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto elapsed = now - statsWindowStartMs_;
    if (elapsed >= 1000) {
        framesPerSecond_ =
            static_cast<double>(framesInStatsWindow_) * 1000.0 / static_cast<double>(elapsed);
        // Replay data is fed by the UI thread, so the drained-frame rate is
        // zero. Keep the user-configured replay rate for the time axis.
        if (framesPerSecond_ > 0.0 && !hasReplayRecording_)
            samplesPerSecond_ = framesPerSecond_;
        framesInStatsWindow_ = 0;
        statsWindowStartMs_ = now;
        emit statsChanged();
    }
}

void AppController::setError(const QString& message)
{
    if (lastError_ == message)
        return;
    lastError_ = message;
    emit lastErrorChanged();
}

// ---------------------------------------------------------------------------
// Remote tuning
//
// The PC stays authoritative: remote writes call the same methods the QML
// controls call, and every accepted change is echoed as a delta so all clients
// converge on the same state (last write wins).
// ---------------------------------------------------------------------------

void AppController::setRemoteEnabled(bool enabled)
{
    if (remoteEnabled_ == enabled)
        return;

    remoteEnabled_ = enabled;
    settings_.setValue(QStringLiteral("remote/enabled"), enabled);
    if (enabled)
        startRemoteServer();
    else
        stopRemoteServer();
    emit remoteChanged();
}

void AppController::setRemotePort(int port)
{
    const auto clamped = static_cast<quint16>(std::clamp(port, 1024, 65535));
    if (remotePort_ == clamped)
        return;

    remotePort_ = clamped;
    settings_.setValue(QStringLiteral("remote/port"), static_cast<int>(clamped));
    if (remoteEnabled_) {
        remote_.stop();
        startRemoteServer();
    }
    emit remoteChanged();
}

void AppController::setRemoteServerName(const QString& name)
{
    const auto cleaned = name.trimmed().left(64);
    if (cleaned.isEmpty() || cleaned == remoteServerName_)
        return;

    remoteServerName_ = cleaned;
    settings_.setValue(QStringLiteral("remote/name"), cleaned);
    remote_.setServerName(cleaned);
    emit remoteChanged();
}

void AppController::setRemotePin(const QString& pin)
{
    const auto cleaned = pin.trimmed().left(12);
    if (cleaned == remotePin_)
        return;

    remotePin_ = cleaned;
    settings_.setValue(QStringLiteral("remote/pin"), cleaned);
    remote_.setPin(cleaned);
    emit remoteChanged();
}

QStringList AppController::remoteAddresses() const
{
    QStringList result;
    if (!remote_.running())
        return result;

    const auto port = remote_.port();
    for (const auto& address : remote_.localAddresses())
        result.push_back(QStringLiteral("ws://%1:%2").arg(address).arg(port));
    result.push_back(QStringLiteral("ws://127.0.0.1:%1").arg(port));
    return result;
}

int AppController::remoteClientCount() const
{
    // Straight from the server so the count never lags a debounced refresh.
    return remote_.greetedCount();
}

void AppController::kickRemoteClient(qulonglong session)
{
    remote_.kick(session);
    refreshRemoteClients();
    updateRemoteStatus();
}

void AppController::refreshRemoteServer()
{
    if (!remoteEnabled_)
        return;
    remote_.stop();
    startRemoteServer();
}

void AppController::startRemoteServer()
{
    QString error;
    remote_.setPin(remotePin_);
    remote_.setServerName(remoteServerName_);
    if (!remote_.start(remotePort_, remoteServerName_, remotePin_, &error)) {
        remoteLastStartOk_ = false;
        remoteStatus_ = error;
        qWarning().noquote() << "[remote]" << error;
        emit remoteChanged();
        return;
    }

    remotePort_ = remote_.port();
    remoteLastStartOk_ = true;
    updateRemoteStatus();
    refreshRemoteClients();
}

void AppController::stopRemoteServer()
{
    remote_.stop();
    remoteClients_.clear();
    emit remoteClientsChanged();
    updateRemoteStatus();
}

void AppController::updateRemoteStatus()
{
    QString status;
    if (!remoteEnabled_) {
        status = QStringLiteral("未启用");
    } else if (!remote_.running()) {
        status = remoteLastStartOk_ ? QStringLiteral("已停止") : QStringLiteral("启动失败");
    } else {
        status = QStringLiteral("运行中 · 端口 %1").arg(remote_.port());
        if (remote_.localAddresses().isEmpty())
            status += QStringLiteral(" · 未找到局域网地址");
        if (remote_.pinRequired())
            status += QStringLiteral(" · 需访问码");
    }

    if (status == remoteStatus_)
        return;
    remoteStatus_ = status;
    emit remoteChanged();
}

remote::StateDoc AppController::buildRemoteState() const
{
    remote::StateDoc doc;
    doc.serverName = remoteServerName_;
    doc.version = remote::RemoteControlServer::versionString();
    doc.groups = commandGroups_;
    doc.pcGroup = currentGroupName();

    for (const auto& binding : commandStore_.bindings()) {
        if (!commands::isValueControl(binding.kind))
            continue;
        remote::ControlDoc control;
        control.id = binding.bindingId;
        control.label = QString::fromStdString(binding.label);
        control.group = QString::fromStdString(binding.group);
        control.value = binding.args.empty() ? 0.0 : binding.args[0].floatValue;
        control.minValue = binding.minValue;
        control.maxValue = binding.maxValue;
        control.step = binding.stepSize;
        control.included = binding.includeInOneClickSend;
        control.order = binding.order;
        doc.controls.push_back(control);
    }

    for (const auto& action : actions_) {
        remote::ActionDoc item;
        item.id = action.id;
        item.label = action.label;
        item.group = action.group;
        item.kind = action.kind;
        item.state = action.state;
        doc.actions.push_back(item);
    }

    doc.transportConnected = connected();
    doc.transportState = connectionState();
    doc.oneClick.sending = oneClickSending_;
    doc.oneClick.sent = oneClickIndex_;
    doc.oneClick.total = static_cast<int>(oneClickSteps_.size());
    doc.oneClick.status = oneClickStatus_;
    doc.oneClick.intervalMs = oneClickIntervalMs_;
    return doc;
}

void AppController::sendRemoteStateTo(quint64 session)
{
    if (!remote_.running())
        return;
    remote_.sendState(session, remote::stateToJson(buildRemoteState(), 0));
}

void AppController::scheduleRemoteState()
{
    if (!remoteEnabled_ || !remote_.running())
        return;
    remoteStateTimer_.start();
}

void AppController::broadcastRemoteState()
{
    if (!remote_.running())
        return;
    remote_.broadcastState(remote::stateToJson(buildRemoteState(), 0));
}

void AppController::queueRemoteValue(quint64 bindingId, double value, const QString& from)
{
    if (!remote_.running())
        return;
    pendingRemoteValues_.insert(bindingId, qMakePair(value, from));
    if (!remoteValueTimer_.isActive())
        remoteValueTimer_.start();
}

void AppController::flushRemoteValues()
{
    if (pendingRemoteValues_.isEmpty())
        return;

    const auto pending = pendingRemoteValues_;
    pendingRemoteValues_.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        remote_.broadcastValue(it.key(), it.value().first, it.value().second);
}

void AppController::pushRemoteOneClick(const QString& phase)
{
    if (!remote_.running())
        return;
    remote_.broadcastOneClick(remote::oneClickMessage(
        phase, oneClickIndex_, static_cast<int>(oneClickSteps_.size()), oneClickStatus_));
}

void AppController::pushRemoteConn()
{
    if (!remote_.running())
        return;
    remote_.broadcastConn(remote::connMessage(connected(), connectionState()));
}

void AppController::refreshRemoteClients()
{
    QVariantList list;
    const auto clients = remote_.clients();
    for (const auto& client : clients) {
        if (!client.greeted)
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(client.session));
        item.insert(QStringLiteral("name"),
                    client.name.isEmpty()
                        ? QStringLiteral("手机 %1").arg(client.session)
                        : client.name);
        item.insert(QStringLiteral("identity"), client.identity);
        item.insert(QStringLiteral("address"), client.address);
        item.insert(QStringLiteral("since"),
                    QDateTime::fromMSecsSinceEpoch(client.connectedAtMs)
                        .toString(QStringLiteral("HH:mm:ss")));
        list.push_back(item);
    }

    remoteClients_ = std::move(list);
    emit remoteClientsChanged();
}

QString AppController::remoteActorTag(quint64 session) const
{
    return QStringLiteral("c%1").arg(session);
}

void AppController::onRemoteValue(quint64 session,
                                  quint64 bindingId,
                                  double value,
                                  bool commit,
                                  bool exact)
{
    if (!std::isfinite(value)) {
        remote_.sendError(session, QStringLiteral("bad_value"),
                          QStringLiteral("数值无效"));
        return;
    }
    if (commandStore_.findBinding(bindingId) == nullptr) {
        remote_.sendError(session, QStringLiteral("unknown_control"),
                          QStringLiteral("控件不存在或已被删除"));
        return;
    }

    const auto actor = remoteActorTag(session);

    if (commit && exact) {
        // Typed value: same escape hatch the PC value field offers.
        if (!setBindingValueExact(bindingId, value)) {
            remote_.sendError(session, QStringLiteral("bad_value"),
                              QStringLiteral("数值无效"));
            return;
        }
        emit remoteValueApplied(bindingId, value);
        queueRemoteValue(bindingId, value, actor);
        return;
    }

    setBindingValue(bindingId, value);
    if (commit)
        commitBindingValue(bindingId);

    const auto* updated = commandStore_.findBinding(bindingId);
    const auto actual = (updated != nullptr && !updated->args.empty())
        ? static_cast<double>(updated->args[0].floatValue)
        : value;
    // setBindingValue() 只就地改 oneClickBindings_，不触发
    // oneClickBindingsChanged，PC 界面因此不会跟着动。
    emit remoteValueApplied(bindingId, actual);
    queueRemoteValue(bindingId, actual, actor);
}

void AppController::onRemoteAction(quint64 session,
                                   quint64 actionId,
                                   bool on,
                                   bool trigger)
{
    const auto it = std::find_if(
        actions_.begin(), actions_.end(),
        [actionId](const ActionEntry& entry) { return entry.id == actionId; });
    if (it == actions_.end()) {
        remote_.sendError(session, QStringLiteral("unknown_control"),
                          QStringLiteral("按键/开关不存在"));
        return;
    }

    const auto actor = remoteActorTag(session);
    if (it->kind == 1) {
        if (trigger)
            triggerActionFrom(actionId, actor);
        else
            applyActionState(actionId, on, actor);
    } else {
        triggerActionFrom(actionId, actor);
    }
}

void AppController::onRemoteInclusion(quint64 session, quint64 bindingId, bool included)
{
    if (commandStore_.findBinding(bindingId) == nullptr) {
        remote_.sendError(session, QStringLiteral("unknown_control"),
                          QStringLiteral("控件不存在或已被删除"));
        return;
    }

    // Same code path the PC checkbox uses, so the saved settings and the
    // one-click plan stay consistent no matter which side ticked it.
    // rebuildOneClickBindings() also emits oneClickBindingsChanged (PC list
    // refreshes) and schedules a full state push (phones refresh).
    commandStore_.setBindingIncluded(bindingId, included);
    rebuildOneClickBindings();
    persistOneClickBindings();
}

void AppController::onRemoteOneClick(quint64 session, const QString& group, int intervalMs)
{
    const auto target = group.trimmed().isEmpty() ? currentGroupName() : group.trimmed();
    if (!commandGroups_.contains(target)) {
        remote_.sendError(session, QStringLiteral("unknown_group"),
                          QStringLiteral("命令组不存在"));
        return;
    }
    if (oneClickSending_) {
        remote_.sendError(session, QStringLiteral("busy"),
                          QStringLiteral("一键发送正在进行中"));
        return;
    }
    if (intervalMs > 0)
        setOneClickIntervalMs(intervalMs);

    // The PC has nowhere to write yet. startOneClickSendForGroup refuses the
    // run and broadcasts phase=idle, which unlocks the phone's progress UI.
    if (!sendReady()) {
        QString reason;
        (void)startOneClickSendForGroup(target, &reason);
        remote_.sendError(session, QStringLiteral("not_connected"), reason);
        return;
    }

    QString error;
    if (!startOneClickSendForGroup(target, &error) && !error.isEmpty())
        remote_.sendError(session, QStringLiteral("empty_plan"), error);
}

} // namespace vofa::app
