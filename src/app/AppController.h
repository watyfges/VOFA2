#pragma once

#include "ImageProvider.h"
#include "SettingsStore.h"
#include "commands/CommandStore.h"
#include "data/ChannelStore.h"
#include "protocol/ParserFactory.h"
#include "remote/RemoteControlServer.h"
#include "transport/TransportController.h"

#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <cstddef>
#include <memory>
#include <vector>

namespace vofa::app {

// QML-facing application controller. Owns the data store and the transport
// session, and exposes a small, stable API to the UI.
class AppController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect
                   NOTIFY autoReconnectChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString receiveText READ receiveText NOTIFY receiveTextChanged)
    Q_PROPERTY(QVariantList latestValues READ latestValues NOTIFY latestValuesChanged)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY latestValuesChanged)
    Q_PROPERTY(QVariantList clientIds READ clientIds NOTIFY clientsChanged)
    Q_PROPERTY(QStringList clientLabels READ clientLabels NOTIFY clientsChanged)
    Q_PROPERTY(qulonglong activeClientId READ activeClientId NOTIFY clientsChanged)
    Q_PROPERTY(QString protocol READ protocol WRITE setProtocol NOTIFY protocolChanged)
    Q_PROPERTY(int oneClickIntervalMs READ oneClickIntervalMs WRITE setOneClickIntervalMs
                   NOTIFY oneClickIntervalMsChanged)
    Q_PROPERTY(bool sendOnMove READ sendOnMove WRITE setSendOnMove
                   NOTIFY sendOnMoveChanged)
    Q_PROPERTY(bool serialBreak READ serialBreak WRITE setSerialBreak
                   NOTIFY serialBreakChanged)
    Q_PROPERTY(QStringList serialPorts READ serialPorts NOTIFY serialPortsChanged)
    Q_PROPERTY(QString configDir READ configDir CONSTANT)
    Q_PROPERTY(bool recoveredFromAutosave READ recoveredFromAutosave CONSTANT)
    Q_PROPERTY(QVariantList oneClickBindings READ oneClickBindings NOTIFY oneClickBindingsChanged)
    Q_PROPERTY(bool oneClickSending READ oneClickSending NOTIFY oneClickStateChanged)
    Q_PROPERTY(int oneClickSent READ oneClickSent NOTIFY oneClickStateChanged)
    Q_PROPERTY(int oneClickTotal READ oneClickTotal NOTIFY oneClickStateChanged)
    Q_PROPERTY(QString oneClickStatus READ oneClickStatus NOTIFY oneClickStateChanged)
    Q_PROPERTY(int waveformRevision READ waveformRevision NOTIFY waveformChanged)
    Q_PROPERTY(int waveformWindowSamples READ waveformWindowSamples WRITE setWaveformWindowSamples
                   NOTIFY waveformWindowChanged)
    Q_PROPERTY(double waveformWindowSeconds READ waveformWindowSeconds
                   WRITE setWaveformWindowSeconds NOTIFY waveformWindowChanged)
    Q_PROPERTY(double samplesPerSecond READ samplesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(int waveformMode READ waveformMode WRITE setWaveformMode
                   NOTIFY waveformModeChanged)
    Q_PROPERTY(bool triggerArmed READ triggerArmed NOTIFY triggerChanged)
    Q_PROPERTY(int triggerChannel READ triggerChannel WRITE setTriggerChannel
                   NOTIFY triggerChanged)
    Q_PROPERTY(double triggerLevel READ triggerLevel WRITE setTriggerLevel
                   NOTIFY triggerChanged)
    Q_PROPERTY(int triggerEdge READ triggerEdge WRITE setTriggerEdge
                   NOTIFY triggerChanged)
    Q_PROPERTY(qulonglong triggerSample READ triggerSample NOTIFY triggerChanged)
    Q_PROPERTY(int triggerCount READ triggerCount NOTIFY triggerChanged)
    Q_PROPERTY(bool triggerReady READ triggerReady NOTIFY triggerChanged)
    Q_PROPERTY(int triggerRevision READ triggerRevision NOTIFY triggerChanged)
    Q_PROPERTY(QString triggerStatus READ triggerStatus NOTIFY triggerChanged)
    Q_PROPERTY(bool hasReplayRecording READ hasReplayRecording NOTIFY replayChanged)
    Q_PROPERTY(bool replayActive READ replayActive NOTIFY replayChanged)
    Q_PROPERTY(int replayRows READ replayRows NOTIFY replayChanged)
    Q_PROPERTY(int replayPosition READ replayPosition NOTIFY replayChanged)
    Q_PROPERTY(double replayRate READ replayRate WRITE setReplayRate
                   NOTIFY replayChanged)
    Q_PROPERTY(int waveformPanSamples READ waveformPanSamples NOTIFY waveformPanChanged)
    Q_PROPERTY(double uiDensity READ uiDensity WRITE setUiDensity NOTIFY uiDensityChanged)
    Q_PROPERTY(double uiFontScale READ uiFontScale WRITE setUiFontScale NOTIFY uiFontScaleChanged)
    Q_PROPERTY(int uiPreset READ uiPreset WRITE setUiPreset NOTIFY uiPresetChanged)
    Q_PROPERTY(int uiTheme READ uiTheme WRITE setUiTheme NOTIFY uiThemeChanged)
    Q_PROPERTY(bool showConnectionPanel READ showConnectionPanel WRITE setShowConnectionPanel
                   NOTIFY panelsChanged)
    Q_PROPERTY(bool showSliderPanel READ showSliderPanel WRITE setShowSliderPanel
                   NOTIFY panelsChanged)
    Q_PROPERTY(QVariantList recentSends READ recentSends NOTIFY recentSendsChanged)
    Q_PROPERTY(QVariantList filteredRecentSends READ filteredRecentSends
                   NOTIFY recentSendsChanged)
    Q_PROPERTY(QString sendFilter READ sendFilter WRITE setSendFilter
                   NOTIFY recentSendsChanged)
    Q_PROPERTY(QVariantList actionBindings READ actionBindings NOTIFY actionBindingsChanged)
    Q_PROPERTY(QStringList commandGroups READ commandGroups NOTIFY commandGroupsChanged)
    Q_PROPERTY(int currentGroup READ currentGroup WRITE setCurrentGroup
                   NOTIFY currentGroupChanged)
    Q_PROPERTY(QString currentGroupName READ currentGroupName NOTIFY currentGroupChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY editHistoryChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY editHistoryChanged)
    Q_PROPERTY(int transportKind READ transportKind NOTIFY transportKindChanged)
    Q_PROPERTY(QString tcpClientHost READ tcpClientHost NOTIFY connectionParamsChanged)
    Q_PROPERTY(int tcpClientPort READ tcpClientPort NOTIFY connectionParamsChanged)
    Q_PROPERTY(QString tcpClientBanner READ tcpClientBanner NOTIFY connectionParamsChanged)
    Q_PROPERTY(int tcpServerPort READ tcpServerPort NOTIFY connectionParamsChanged)
    Q_PROPERTY(QVariantList channelSettings READ channelSettings NOTIFY channelSettingsChanged)
    Q_PROPERTY(bool waveformPaused READ waveformPaused WRITE setWaveformPaused
                   NOTIFY waveformPausedChanged)
    Q_PROPERTY(bool receivePaused READ receivePaused WRITE setReceivePaused
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(bool receiveTimestamps READ receiveTimestamps WRITE setReceiveTimestamps
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(bool receiveHex READ receiveHex WRITE setReceiveHex
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(int receiveEncoding READ receiveEncoding WRITE setReceiveEncoding
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(bool receiveAnsi READ receiveAnsi WRITE setReceiveAnsi
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(QString receiveFilter READ receiveFilter WRITE setReceiveFilter
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(bool receiveFilterRegex READ receiveFilterRegex WRITE setReceiveFilterRegex
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(int receiveFilterDirection READ receiveFilterDirection
                   WRITE setReceiveFilterDirection NOTIFY receiveOptionsChanged)
    Q_PROPERTY(bool receiveTxEcho READ receiveTxEcho WRITE setReceiveTxEcho
                   NOTIFY receiveOptionsChanged)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY imageUpdated)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY imageUpdated)
    Q_PROPERTY(QString imageInfo READ imageInfo NOTIFY imageUpdated)
    Q_PROPERTY(qulonglong totalRxBytes READ totalRxBytes NOTIFY statsChanged)
    Q_PROPERTY(qulonglong totalTxBytes READ totalTxBytes NOTIFY statsChanged)
    Q_PROPERTY(double framesPerSecond READ framesPerSecond NOTIFY statsChanged)
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY statsChanged)
    // Remote tuning (mobile clients on the LAN).
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled WRITE setRemoteEnabled
                   NOTIFY remoteChanged)
    Q_PROPERTY(int remotePort READ remotePort WRITE setRemotePort NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteServerName READ remoteServerName WRITE setRemoteServerName
                   NOTIFY remoteChanged)
    Q_PROPERTY(QString remotePin READ remotePin WRITE setRemotePin NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteStatus READ remoteStatus NOTIFY remoteChanged)
    Q_PROPERTY(QStringList remoteAddresses READ remoteAddresses NOTIFY remoteChanged)
    Q_PROPERTY(int remoteClientCount READ remoteClientCount NOTIFY remoteClientsChanged)
    Q_PROPERTY(QVariantList remoteClients READ remoteClients NOTIFY remoteClientsChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QString connectionState() const;
    [[nodiscard]] QString appVersion() const
    {
        return QStringLiteral(VOFA_VERSION_STRING);
    }
    [[nodiscard]] bool connected() const
    {
        return state_ == transport::ConnectionState::Opening
            || state_ == transport::ConnectionState::Connected
            || state_ == transport::ConnectionState::Closing;
    }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString receiveText() const
    {
        if (receiveHex_)
            return receiveHexText_;
        if (receiveAnsi_)
            return receiveAnsiText_;
        return receiveText_;
    }
    [[nodiscard]] QVariantList latestValues() const { return latestValues_; }
    [[nodiscard]] int channelCount() const { return channelCount_; }
    [[nodiscard]] QVariantList clientIds() const { return clientIds_; }
    [[nodiscard]] QStringList clientLabels() const { return clientLabels_; }
    [[nodiscard]] qulonglong activeClientId() const { return activeClientId_; }
    [[nodiscard]] QString protocol() const;
    Q_INVOKABLE void setProtocol(const QString& protocol);
    [[nodiscard]] int oneClickIntervalMs() const { return oneClickIntervalMs_; }
    Q_INVOKABLE void setOneClickIntervalMs(int value);
    [[nodiscard]] bool sendOnMove() const { return sendOnMove_; }
    Q_INVOKABLE void setSendOnMove(bool enabled);
    [[nodiscard]] bool serialBreak() const { return serialBreak_; }
    Q_INVOKABLE void setSerialBreak(bool enabled);
    [[nodiscard]] QStringList serialPorts() const { return serialPorts_; }
    [[nodiscard]] QString configDir() const;
    [[nodiscard]] bool recoveredFromAutosave() const { return settings_.recovered(); }
    [[nodiscard]] QVariantList oneClickBindings() const { return oneClickBindings_; }
    [[nodiscard]] bool oneClickSending() const { return oneClickSending_; }
    [[nodiscard]] int oneClickSent() const { return oneClickIndex_; }
    [[nodiscard]] int oneClickTotal() const
    {
        return static_cast<int>(oneClickSteps_.size());
    }
    [[nodiscard]] QString oneClickStatus() const { return oneClickStatus_; }
    [[nodiscard]] int waveformRevision() const { return waveformRevision_; }
    [[nodiscard]] int waveformWindowSamples() const { return waveformWindowSamples_; }
    Q_INVOKABLE void setWaveformWindowSamples(int samples);
    [[nodiscard]] double waveformWindowSeconds() const { return waveformWindowSeconds_; }
    Q_INVOKABLE void setWaveformWindowSeconds(double seconds);
    [[nodiscard]] double samplesPerSecond() const { return samplesPerSecond_; }
    [[nodiscard]] int waveformMode() const { return waveformMode_; }
    Q_INVOKABLE void setWaveformMode(int mode);
    [[nodiscard]] bool triggerArmed() const { return triggerArmed_; }
    [[nodiscard]] int triggerChannel() const { return triggerChannel_; }
    Q_INVOKABLE void setTriggerChannel(int channel);
    [[nodiscard]] double triggerLevel() const { return triggerLevel_; }
    Q_INVOKABLE void setTriggerLevel(double level);
    [[nodiscard]] int triggerEdge() const { return triggerEdge_; }
    Q_INVOKABLE void setTriggerEdge(int edge);
    [[nodiscard]] qulonglong triggerSample() const { return triggerSample_; }
    [[nodiscard]] int triggerCount() const { return triggerCount_; }
    [[nodiscard]] bool triggerReady() const { return triggerReady_; }
    [[nodiscard]] int triggerRevision() const { return triggerRevision_; }
    [[nodiscard]] QString triggerStatus() const;
    Q_INVOKABLE void armTrigger();
    Q_INVOKABLE void clearTrigger();
    [[nodiscard]] bool hasReplayRecording() const { return hasReplayRecording_; }
    [[nodiscard]] bool replayActive() const { return replayActive_; }
    [[nodiscard]] int replayRows() const { return static_cast<int>(replayRows_); }
    [[nodiscard]] int replayPosition() const
    {
        return static_cast<int>(replayPosition_);
    }
    [[nodiscard]] double replayRate() const { return replayRate_; }
    Q_INVOKABLE void setReplayRate(double rowsPerSecond);
    Q_INVOKABLE QString chooseCsvToImport();
    Q_INVOKABLE QString loadCsvRecording(const QString& path);
    Q_INVOKABLE void startReplay();
    Q_INVOKABLE void stopReplay();
    Q_INVOKABLE void resetReplay();
    // Analysis data for the non-time waveform modes.
    void buildSpectrum(int channel, int bins, std::vector<float>& out) const;
    void buildHistogram(int channel,
                        int bins,
                        float& minValue,
                        float& maxValue,
                        std::vector<float>& out) const;
    void buildXY(int xChannel,
                 int yChannel,
                 int maxPoints,
                 std::vector<float>& out) const;
    [[nodiscard]] int waveformPanSamples() const { return waveformPanSamples_; }
    [[nodiscard]] int waveformVisibleSampleCount() const;
    Q_INVOKABLE void panWaveform(int deltaSamples);
    Q_INVOKABLE void resetWaveformPan();
    Q_INVOKABLE void zoomWaveformAt(double factor, double anchorFraction);
    Q_INVOKABLE void zoomWaveformToFractions(double startFraction,
                                             double endFraction);
    [[nodiscard]] double uiDensity() const { return uiDensity_; }
    Q_INVOKABLE void setUiDensity(double value);
    [[nodiscard]] double uiFontScale() const { return uiFontScale_; }
    Q_INVOKABLE void setUiFontScale(double value);
    [[nodiscard]] int uiPreset() const { return uiPreset_; }
    Q_INVOKABLE void setUiPreset(int preset);
    [[nodiscard]] int uiTheme() const { return uiTheme_; }
    Q_INVOKABLE void setUiTheme(int theme);
    [[nodiscard]] bool autoReconnect() const { return autoReconnect_; }
    Q_INVOKABLE void setAutoReconnect(bool enabled);
    [[nodiscard]] bool showConnectionPanel() const { return showConnectionPanel_; }
    Q_INVOKABLE void setShowConnectionPanel(bool visible);
    [[nodiscard]] bool showSliderPanel() const { return showSliderPanel_; }
    Q_INVOKABLE void setShowSliderPanel(bool visible);
    [[nodiscard]] QVariantList recentSends() const { return recentSends_; }
    [[nodiscard]] QVariantList filteredRecentSends() const;
    [[nodiscard]] QString sendFilter() const { return sendFilter_; }
    Q_INVOKABLE void setSendFilter(const QString& filter);
    [[nodiscard]] QVariantList actionBindings() const { return actionBindings_; }
    [[nodiscard]] QStringList commandGroups() const { return commandGroups_; }
    [[nodiscard]] int currentGroup() const { return currentGroup_; }
    [[nodiscard]] QString currentGroupName() const
    {
        return commandGroups_.value(currentGroup_);
    }
    Q_INVOKABLE void setCurrentGroup(int index);
    [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool addCommandGroup(const QString& name);
    Q_INVOKABLE bool renameCommandGroup(int index, const QString& name);
    Q_INVOKABLE bool removeCommandGroup(int index);
    Q_INVOKABLE bool moveCurrentGroupContents(int targetIndex);
    [[nodiscard]] int transportKind() const { return transportKind_; }
    [[nodiscard]] QString tcpClientHost() const { return tcpClientHost_; }
    [[nodiscard]] int tcpClientPort() const { return tcpClientPort_; }
    [[nodiscard]] QString tcpClientBanner() const { return tcpClientBanner_; }
    [[nodiscard]] int tcpServerPort() const { return tcpServerPort_; }
    [[nodiscard]] QVariantList channelSettings() const;
    [[nodiscard]] bool waveformPaused() const { return waveformPaused_; }
    Q_INVOKABLE void setWaveformPaused(bool paused);
    [[nodiscard]] bool receivePaused() const { return receivePaused_; }
    Q_INVOKABLE void setReceivePaused(bool paused);
    [[nodiscard]] bool receiveTimestamps() const { return receiveTimestamps_; }
    Q_INVOKABLE void setReceiveTimestamps(bool enabled);
    [[nodiscard]] bool receiveHex() const { return receiveHex_; }
    Q_INVOKABLE void setReceiveHex(bool enabled);
    [[nodiscard]] int receiveEncoding() const { return receiveEncoding_; }
    Q_INVOKABLE void setReceiveEncoding(int encoding);
    [[nodiscard]] bool receiveAnsi() const { return receiveAnsi_; }
    Q_INVOKABLE void setReceiveAnsi(bool enabled);
    [[nodiscard]] QString receiveFilter() const { return receiveFilter_; }
    Q_INVOKABLE void setReceiveFilter(const QString& filter);
    [[nodiscard]] bool receiveFilterRegex() const { return receiveFilterRegex_; }
    Q_INVOKABLE void setReceiveFilterRegex(bool enabled);
    [[nodiscard]] int receiveFilterDirection() const { return receiveFilterDirection_; }
    Q_INVOKABLE void setReceiveFilterDirection(int direction);
    [[nodiscard]] bool receiveTxEcho() const { return receiveTxEcho_; }
    Q_INVOKABLE void setReceiveTxEcho(bool enabled);
    [[nodiscard]] QString imageSource() const;
    [[nodiscard]] int imageCount() const { return imageCount_; }
    [[nodiscard]] QString imageInfo() const { return imageInfo_; }
    [[nodiscard]] qulonglong totalRxBytes() const { return totalRxBytes_; }
    [[nodiscard]] qulonglong totalTxBytes() const { return totalTxBytes_; }
    [[nodiscard]] double framesPerSecond() const { return framesPerSecond_; }
    [[nodiscard]] qulonglong droppedFrames() const { return store_.droppedFrames(); }
    // QQmlEngine takes ownership of providers passed to addImageProvider().
    // Call this exactly once and hand the result to the engine.
    [[nodiscard]] ImageProvider* releaseImageProvider()
    {
        ownsImageProvider_ = false;
        return imageProvider_;
    }

    Q_INVOKABLE void refreshSerialPorts();
    Q_INVOKABLE void openSerial(const QString& port, int baudRate);
    Q_INVOKABLE void openSerialAdvanced(const QString& port,
                                        int baudRate,
                                        int dataBits,
                                        int parity,
                                        int stopBits,
                                        int flowControl,
                                        bool dtr,
                                        bool rts);
    Q_INVOKABLE void openTcpClient(const QString& host, int port, const QString& banner);
    Q_INVOKABLE void openTcpServer(int port);
    Q_INVOKABLE void openUdp(const QString& remoteHost, int remotePort, int localPort);
    Q_INVOKABLE void openDemo();
    Q_INVOKABLE void closeTransport();

    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void sendHex(const QString& hex);
    Q_INVOKABLE void sendTextTo(qulonglong clientId, const QString& text);
    Q_INVOKABLE void setActiveClient(qulonglong clientId);

    Q_INVOKABLE void clearReceive();
    Q_INVOKABLE void saveWindow(int x, int y, int width, int height);
    Q_INVOKABLE QVariantMap savedWindowGeometry() const;
    Q_INVOKABLE QVariantMap savedPanelWidths() const;
    Q_INVOKABLE void savePanelWidths(int left, int right);

    Q_INVOKABLE void setBindingValue(qulonglong bindingId, double value);
    // Typed value entry for debugging: accepts values outside the slider's
    // min/max instead of clamping to the slider range.
    Q_INVOKABLE bool setBindingValueExact(qulonglong bindingId, double value);
    Q_INVOKABLE void commitBindingValue(qulonglong bindingId);
    Q_INVOKABLE void setBindingIncluded(qulonglong bindingId, bool included);
    Q_INVOKABLE void startOneClickSend();
    Q_INVOKABLE void cancelOneClickSend();
    Q_INVOKABLE QVariantList waveformSnapshot(int channel, int width) const;

    // Fast path used by WaveformItem: fills `out` with [min,max] pairs for
    // each physical pixel column, applying the channel scale/offset. No
    // QVariant allocation, safe to call every rendered frame.
    void buildWaveform(int channel, int width, std::vector<float>& out) const;
    void buildWaveformRange(int channel,
                            std::uint64_t startAbsolute,
                            std::size_t count,
                            int width,
                            std::vector<float>& out) const;
    [[nodiscard]] float sampleAt(int channel, std::uint64_t absoluteIndex) const;
    [[nodiscard]] std::uint64_t dataAbsoluteStart() const;
    [[nodiscard]] std::uint64_t dataAbsoluteEnd() const;
    [[nodiscard]] int waveformChannelCount() const;
    [[nodiscard]] bool channelVisible(int channel) const;
    [[nodiscard]] QColor channelColor(int channel) const;
    Q_INVOKABLE QString importLegacyFromDefault();
    Q_INVOKABLE QString importLegacyFromDirectory(const QString& contextDir);
    Q_INVOKABLE QString migrationReportPath() const;
    Q_INVOKABLE QString openMigrationReport() const;
    Q_INVOKABLE QString exportCsv();
    Q_INVOKABLE QString chooseFileToSend();
    Q_INVOKABLE QString sendFile(const QString& path);
    Q_INVOKABLE QString newScreenshotPath() const;
    Q_INVOKABLE void clearWaveform();
    Q_INVOKABLE void setChannelVisible(int index, bool visible);
    Q_INVOKABLE void setChannelColor(int index, const QString& color);
    Q_INVOKABLE void setChannelScale(int index, double scale);
    Q_INVOKABLE void setChannelOffset(int index, double offset);
    Q_INVOKABLE void setChannelDecimals(int index, int decimals);
    Q_INVOKABLE void setChannelAutoScale(int index, bool autoScale);
    Q_INVOKABLE void setChannelRange(int index, double minValue, double maxValue);
    [[nodiscard]] bool channelFixedRange(int channel,
                                         float& minValue,
                                         float& maxValue) const;
    Q_INVOKABLE bool addOneClickBinding(const QString& label,
                                        const QString& templateText,
                                        double minValue,
                                        double maxValue,
                                        double stepSize,
                                        double value,
                                        bool included,
                                        const QString& group);
    Q_INVOKABLE bool updateOneClickBinding(qulonglong bindingId,
                                           const QString& label,
                                           const QString& templateText,
                                           double minValue,
                                           double maxValue,
                                           double stepSize,
                                           double value,
                                           bool included,
                                           const QString& group);
    Q_INVOKABLE bool removeOneClickBinding(qulonglong bindingId);
    // 步进值：数值框上下箭头和滑杆共用；输入框不受它限制。
    Q_INVOKABLE bool setBindingStep(qulonglong bindingId, double stepSize);
    Q_INVOKABLE void repeatRecentSend(int index);
    Q_INVOKABLE void repeatRecentSendEntry(const QString& text, bool hex);
    Q_INVOKABLE void clearRecentSends();

    // Action widgets: buttons send one raw command; toggles send the on/off
    // command for their new state. They are deliberately excluded from
    // one-click sending.
    Q_INVOKABLE bool addActionBinding(const QString& label,
                                      const QString& onTemplate,
                                      const QString& offTemplate,
                                      int kind,
                                      const QString& group);
    Q_INVOKABLE bool updateActionBinding(qulonglong bindingId,
                                         const QString& label,
                                         const QString& onTemplate,
                                         const QString& offTemplate,
                                         int kind,
                                         const QString& group);
    Q_INVOKABLE bool removeActionBinding(qulonglong bindingId);
    Q_INVOKABLE void triggerAction(qulonglong bindingId);
    Q_INVOKABLE void setActionState(qulonglong bindingId, bool on);
    Q_INVOKABLE void requestDialog(const QString& name);

    // Remote tuning. Enabling starts a LAN server that mirrors the sliders,
    // toggles, action buttons and the one-click send button to phones.
    [[nodiscard]] bool remoteEnabled() const { return remoteEnabled_; }
    Q_INVOKABLE void setRemoteEnabled(bool enabled);
    [[nodiscard]] int remotePort() const { return remotePort_; }
    Q_INVOKABLE void setRemotePort(int port);
    [[nodiscard]] QString remoteServerName() const { return remoteServerName_; }
    Q_INVOKABLE void setRemoteServerName(const QString& name);
    [[nodiscard]] QString remotePin() const { return remotePin_; }
    Q_INVOKABLE void setRemotePin(const QString& pin);
    [[nodiscard]] QString remoteStatus() const { return remoteStatus_; }
    [[nodiscard]] QStringList remoteAddresses() const;
    [[nodiscard]] int remoteClientCount() const;
    [[nodiscard]] QVariantList remoteClients() const { return remoteClients_; }
    Q_INVOKABLE void kickRemoteClient(qulonglong session);
    Q_INVOKABLE void refreshRemoteServer();

signals:
    void connectionStateChanged();
    void lastErrorChanged();
    void receiveTextChanged();
    void latestValuesChanged();
    void clientsChanged();
    void protocolChanged();
    void oneClickIntervalMsChanged();
    void serialPortsChanged();
    void oneClickBindingsChanged();
    // 远程端（手机）写入滑块值后单独广播，界面只需 patch 对应那一行，
    // 不必整表重建——整表重建会打断 PC 正在进行的拖动。
    void remoteValueApplied(qulonglong bindingId, double value);
    void oneClickStateChanged();
    void waveformChanged();
    void waveformWindowChanged();
    void waveformPanChanged();
    void waveformModeChanged();
    void triggerChanged();
    void replayChanged();
    void uiDensityChanged();
    void uiFontScaleChanged();
    void uiPresetChanged();
    void uiThemeChanged();
    void transportKindChanged();
    void connectionParamsChanged();
    void channelSettingsChanged();
    void waveformPausedChanged();
    void receiveOptionsChanged();
    void dialogRequested(const QString& name);
    void imageUpdated();
    void statsChanged();
    void panelsChanged();
    void recentSendsChanged();
    void autoReconnectChanged();
    void sendOnMoveChanged();
    void serialBreakChanged();
    void commandGroupsChanged();
    void currentGroupChanged();
    void editHistoryChanged();
    void actionBindingsChanged();
    void remoteChanged();
    void remoteClientsChanged();

private:
    void onStateChanged(transport::ConnectionState state, const QString& error);
    void onReceived(quint64 generation, quint64 clientId, const QByteArray& data);
    void onClientsChanged(const QList<quint64>& ids,
                          const QStringList& labels,
                          quint64 activeId);
    void onStreamReset(quint64 generation);
    void onImageReceived(quint64 generation,
                         quint32 imageId,
                         int format,
                         int width,
                         int height,
                         const QByteArray& data);
    void onSendFailed(const QString& message);
    void onErrorOccurred(const QString& message);
    void onUpdateTimer();
    void checkTrigger();
    void feedReplayFrames(std::size_t maxRows);
    void sendNextOneClickStep();
    void sendBindingCommand(quint64 bindingId);
    void finishOneClickSend(bool cancelled);
    // True only when a write would actually reach a peer. Queuing a send on an
    // unopened/idle transport just bounces back as sendFailed, which used to
    // leave one-click sending stranded halfway.
    [[nodiscard]] bool sendReady() const;
    // Stops a one-click run that cannot continue (disconnect or send failure)
    // and tells remote clients so their progress UI does not stay busy.
    void abortOneClickSend(const QString& status);
    void loadOneClickBindings(bool seedDefaults = true);
    void persistOneClickBindings();
    void loadCommandGroups();
    void persistCommandGroups();
    [[nodiscard]] bool hasCommandGroup(const QString& name) const;
    void loadRecentSends();
    void persistRecentSends();
    void addRecentSend(const QString& text, bool hex);
    void loadActionBindings();
    void persistActionBindings();
    void rebuildActionBindings();
    void sendRawTemplate(const QString& text);
    void appendHexChunk(const QByteArray& data);
    [[nodiscard]] bool lineMatchesReceiveFilter(const QString& line) const;
    void appendTxEcho(const QString& text, bool hex);
    void rebuildReceiveFilterExpression();
    QString ansiToHtml(const QString& text);
    void resetAnsiState();
    void appendTextChunk(const QString& text);
    [[nodiscard]] int effectiveWindowSamples() const;
    [[nodiscard]] quint64 nextActionId() const;
    void rebuildOneClickBindings();
    [[nodiscard]] quint64 nextBindingId() const;
    bool importLegacyDirectory(const QString& contextDir, QString* message);
    void ensureChannelConfigs(std::size_t count);
    void persistChannelConfigs();
    [[nodiscard]] std::size_t channelConfigCount() const noexcept
    {
        return channelConfigs_.size();
    }

    // Snapshot of the editable command/action/group state. Captured before
    // every structural edit and restored by undo/redo.
    struct EditSnapshot {
        QJsonArray oneClick;
        QJsonArray actions;
        QJsonArray groups;
        int currentGroup = 0;
    };
    void captureEdit();
    [[nodiscard]] EditSnapshot currentEditSnapshot() const;
    void restoreEditSnapshot(const EditSnapshot& snapshot);
    void clearEditHistory();

    // --- Remote tuning -------------------------------------------------
    // The server never owns tuning state: every remote write is routed through
    // the same methods the QML buttons call, then broadcast back as a delta.
    void startRemoteServer();
    void stopRemoteServer();
    void updateRemoteStatus();
    [[nodiscard]] remote::StateDoc buildRemoteState() const;
    void sendRemoteStateTo(quint64 session);
    void scheduleRemoteState();
    void broadcastRemoteState();
    void queueRemoteValue(quint64 bindingId,
                          double value,
                          const QString& from = QStringLiteral("pc"));
    void flushRemoteValues();
    void pushRemoteOneClick(const QString& phase);
    void pushRemoteConn();
    void pushRemoteLink();
    [[nodiscard]] remote::LinkDoc buildRemoteLink() const;
    void refreshRemoteClients();
    [[nodiscard]] QString remoteActorTag(quint64 session) const;
    bool startOneClickSendForGroup(const QString& group, QString* error);
    void applyActionState(quint64 bindingId, bool on, const QString& actor);
    void triggerActionFrom(quint64 bindingId, const QString& actor);
    void onRemoteValue(quint64 session,
                       quint64 bindingId,
                       double value,
                       bool commit,
                       bool exact);
    void onRemoteAction(quint64 session, quint64 actionId, bool on, bool trigger);
    void onRemoteInclusion(quint64 session, quint64 bindingId, bool included);
    void onRemoteLink(quint64 session, const remote::LinkCommand& command);
    void onRemoteOneClick(quint64 session, const QString& group, int intervalMs);

    struct ChannelConfig {
        QString name;
        QString color;
        bool visible = true;
        double scale = 1.0;
        double offset = 0.0;
        int decimals = 3;
        bool autoScale = true;
        double rangeMin = -5.0;
        double rangeMax = 5.0;
    };

    void setError(const QString& message);
    void applyUiFont();

    data::ChannelStore store_;
    transport::TransportController transport_;
    SettingsStore settings_;
    commands::CommandStore commandStore_;
    remote::RemoteControlServer remote_;
    QTimer remoteStateTimer_;
    QTimer remoteValueTimer_;
    QTimer remoteClientsTimer_;
    QHash<quint64, QPair<double, QString>> pendingRemoteValues_;
    bool remoteEnabled_ = false;
    quint16 remotePort_ = remote::kDefaultControlPort;
    QString remoteServerName_;
    QString remotePin_;
    QString remoteStatus_;
    QVariantList remoteClients_;
    bool remoteLastStartOk_ = true;
    QTimer oneClickTimer_;
    QTimer reconnectTimer_;
    std::vector<commands::SendStep> oneClickSteps_;
    QVariantList oneClickBindings_;
    int oneClickIndex_ = 0;
    bool oneClickSending_ = false;
    QString oneClickStatus_;
    bool serialBreak_ = false;
    QString sendFilter_;
    int waveformRevision_ = 0;
    int waveformWindowSamples_ = 20000;
    double waveformWindowSeconds_ = 10.0;
    double samplesPerSecond_ = 0.0;
    int waveformPanSamples_ = 0;
    int waveformMode_ = 0;
    bool triggerArmed_ = false;
    bool triggerReady_ = false;
    int triggerChannel_ = 0;
    int triggerEdge_ = 0; // 0 = rising, 1 = falling
    int triggerCount_ = 0;
    int triggerRevision_ = 0;
    double triggerLevel_ = 0.0;
    qulonglong triggerSample_ = 0;
    qulonglong triggerScanAbsolute_ = 0;
    QTimer triggerCaptureTimer_;
    // CSV recording replay: row-major interleaved channel samples.
    bool hasReplayRecording_ = false;
    bool replayActive_ = false;
    std::size_t replayChannels_ = 0;
    std::size_t replayRows_ = 0;
    std::size_t replayPosition_ = 0;
    std::vector<float> replaySamples_;
    double replayRate_ = 1000.0;
    QTimer replayTimer_;
    quint64 lastFramesStored_ = 0;
    double uiDensity_ = 1.0;
    double uiFontScale_ = 1.0;
    int uiPreset_ = 1;
    int uiTheme_ = 0;
    bool showConnectionPanel_ = true;
    bool showSliderPanel_ = true;
    bool autoReconnect_ = false;
    bool lastConfigValid_ = false;
    bool userClosed_ = true;
    transport::TransportConfig lastConfig_;
    QVariantList recentSends_;
    QStringList commandGroups_ {QStringLiteral("默认")};
    int currentGroup_ = 0;
    std::vector<EditSnapshot> undoStack_;
    std::vector<EditSnapshot> redoStack_;

    struct ActionEntry {
        quint64 id = 0;
        QString label;
        QString group;
        QString onTemplate;
        QString offTemplate;
        int kind = 0; // 0 = button, 1 = toggle
        bool state = false;
    };
    std::vector<ActionEntry> actions_;
    QVariantList actionBindings_;
    int transportKind_ = 0;
    QString tcpClientHost_ = QStringLiteral("127.0.0.1");
    int tcpClientPort_ = 1346;
    QString tcpClientBanner_ = QStringLiteral("plot0");
    int tcpServerPort_ = 1347;
    quint16 listeningPort_ = 0;
    std::vector<ChannelConfig> channelConfigs_;
    bool waveformPaused_ = false;
    bool receivePaused_ = false;
    bool receiveTimestamps_ = false;
    bool receiveHex_ = false;
    bool receiveAnsi_ = false;
    QString receiveFilter_;
    bool receiveFilterRegex_ = false;
    int receiveFilterDirection_ = 0; // 0 = both, 1 = receive only, 2 = send only
    bool receiveTxEcho_ = false;
    QRegularExpression receiveFilterExpression_;
    int receiveEncoding_ = 0; // 0 = UTF-8, 1 = system ANSI (GBK on zh-CN)
    QString receiveAnsiText_;
    QString pendingAnsi_;
    QString ansiColor_;
    bool ansiBold_ = false;
    QString filterLineBuffer_;
    QString receiveHexText_;
    QString pendingHex_;
    int hexColumn_ = 0;
    quint64 hexOffset_ = 0;
    QStringDecoder textDecoder_ {QStringDecoder::Utf8};
    ImageProvider* imageProvider_ = nullptr;
    bool ownsImageProvider_ = true;
    int latestImageId_ = 0;
    int imageCount_ = 0;
    quint64 imageRevision_ = 0;
    QString imageInfo_;
    QHash<quint64, QString> pendingImageInfo_;
    qulonglong totalRxBytes_ = 0;
    qulonglong totalTxBytes_ = 0;
    double framesPerSecond_ = 0.0;
    qulonglong framesInStatsWindow_ = 0;
    qint64 statsWindowStartMs_ = 0;

    QTimer updateTimer_;
    QString pendingReceive_;
    QString receiveText_;
    QVariantList latestValues_;
    QVariantList clientIds_;
    QStringList clientLabels_;
    int channelCount_ = 0;
    QString lastError_;
    QString protocol_ = QStringLiteral("JustFloat");
    protocol::ProtocolKind protocolKind_ = protocol::ProtocolKind::JustFloat;
    qulonglong activeClientId_ = 0;
    int oneClickIntervalMs_ = 100;
    bool sendOnMove_ = true;
    qint64 lastSliderSendMs_ = 0;
    quint64 lastSliderBinding_ = 0;
    QStringList serialPorts_;
    transport::ConnectionState state_ = transport::ConnectionState::Disconnected;
};

} // namespace vofa::app
