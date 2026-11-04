import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vofa2.Native 1.0

ApplicationWindow {
    id: window
    width: Math.min(1600, Screen.width - 24)
    height: Math.min(900, Screen.height - 48)
    minimumWidth: Math.min(1040, Screen.width - 24)
    minimumHeight: Math.min(640, Screen.height - 48)
    visible: true
    title: qsTr("VOFA2 %1").arg(app.appVersion)

    // Font is the primary metric driver: all Controls and Labels inherit it,
    // so a density-preset switch visibly changes text and control heights.
    font.pixelSize: window.fontPx(13)

    property int transportIndex: 0
    property int checkedCount: 0
    property bool showImagePanel: true
    property int editBindingId: 0
    property int actionEditId: 0

    readonly property var lightTheme: ({
        window: "#f4f5f7",
        panel: "#ffffff",
        base: "#ffffff",
        text: "#1f2d3d",
        button: "#e9ebee",
        buttonText: "#1f2d3d",
        tip: "#7f8c8d",
        error: "#c0392b",
        separator: "#d8dde3",
        waveBg: "#ffffff",
        waveBorder: "#c9d2da",
        grid: "#c2ccd6",
        axisText: "#4a5563",
        accent: "#1abc9c",
        checked: "#4a5563",
        light: "#dfe7ea",
        highlighted: "#1f2d3d"
    })
    readonly property var darkTheme: ({
        window: "#1b1f24",
        panel: "#232830",
        base: "#171b20",
        text: "#e6e9ed",
        button: "#2e3540",
        buttonText: "#e6e9ed",
        tip: "#9aa7b4",
        error: "#ff6b6b",
        separator: "#3a424d",
        waveBg: "#0d1116",
        waveBorder: "#39424e",
        grid: "#26313c",
        axisText: "#a8b8c8",
        accent: "#2ecc71",
        checked: "#3c4652",
        light: "#3a4652",
        highlighted: "#ffffff"
    })
    readonly property var eyeTheme: ({
        window: "#e8f0e8",
        panel: "#f3f8f1",
        base: "#fbfdf9",
        text: "#243024",
        button: "#dfe9dc",
        buttonText: "#243024",
        tip: "#6b7a6b",
        error: "#b33a3a",
        separator: "#c9d6c7",
        waveBg: "#f6fbf2",
        waveBorder: "#c9d6c7",
        grid: "#b8cbb3",
        axisText: "#4c5a4a",
        accent: "#3a8f5a",
        checked: "#5a6d58",
        light: "#d3e2cf",
        highlighted: "#243024"
    })
    property var theme: app.uiTheme === 1 ? window.darkTheme
                      : (app.uiTheme === 2 ? window.eyeTheme : window.lightTheme)

    color: theme.window
    palette.window: theme.window
    palette.windowText: theme.text
    palette.base: theme.base
    palette.alternateBase: theme.panel
    palette.text: theme.text
    palette.button: theme.button
    palette.buttonText: theme.buttonText
    palette.highlight: theme.accent
    palette.highlightedText: theme.highlighted
    palette.brightText: "#ffffff"
    palette.link: theme.accent
    palette.mid: theme.separator
    palette.midlight: theme.separator
    palette.light: theme.light
    palette.dark: theme.checked
    palette.shadow: "#000000"

    function dp(value) {
        return Math.round(value * app.uiDensity);
    }

    function fontPx(value) {
        return Math.round(value * app.uiFontScale);
    }

    // Called from the debug/test harness. It uses exactly the same QML ->
    // C++ call path as the preset buttons.
    function applyUiPreset(index) {
        app.setUiPreset(index);
    }

    function applyLogFilter() {
        app.receiveFilter = filterField.text;
        app.receiveFilterRegex = filterRegexCheck.checked;
        app.receiveFilterDirection = filterDirectionCombo.currentIndex;
        app.receiveTxEcho = filterTxEcho.checked;
        filterDialog.close();
    }

    function debugOpenProtocolCombo() {
        protocolCombo.popup.open();
    }

    function debugEnableCursors() {
        timePlot.setCursorsEnabled(true);
    }

    function resetWindowSize() {
        window.width = Math.min(1600, Screen.width - 24);
        window.height = Math.min(900, Screen.height - 48);
        window.x = Math.max(0, Math.round((Screen.width - window.width) / 2));
        window.y = Math.max(0, Math.round((Screen.height - window.height) / 2));
    }

    function formatReadout(value) {
        const number = Number(value);
        if (!isFinite(number))
            return "--";
        return number.toFixed(3);
    }

    // Compact but precise display for the editable parameter value fields.
    function formatNumber(value) {
        const number = Number(value);
        if (!isFinite(number))
            return "0";
        if (number === 0)
            return "0";
        return Number(number.toPrecision(8)).toString();
    }

    function formatSpan(seconds) {
        if (seconds <= 0)
            return qsTr("全部");
        if (seconds < 60) {
            const text = seconds < 10 ? seconds.toFixed(1) : seconds.toFixed(0);
            return qsTr("%1 秒").arg(text.replace(/\.0$/, ""));
        }
        return qsTr("%1 分钟").arg((seconds / 60).toFixed(1).replace(/\.0$/, ""));
    }

    function formatAxisTime(seconds, index, divisions) {
        if (index === divisions)
            return "0s";
        const value = -seconds * (divisions - index) / divisions;
        if (seconds < 10)
            return value.toFixed(2) + "s";
        if (seconds < 120)
            return value.toFixed(1) + "s";
        return (value / 60).toFixed(1) + "m";
    }

    // Moving X reference value under the pointer (same units as the axis).
    function formatHoverTime(seconds) {
        const value = Number(seconds);
        if (!isFinite(value))
            return "--";
        const span = timePlot.visibleSpanSeconds;
        if (span < 10)
            return value.toFixed(2) + "s";
        if (span < 120)
            return value.toFixed(1) + "s";
        return (value / 60).toFixed(1) + "m";
    }

    function debugDump(name, column) {
        console.log(name, "height", column.height,
                    "implicit", column.implicitHeight,
                    "spacing", column.spacing);
        for (let i = 0; i < column.children.length; ++i) {
            const c = column.children[i];
            console.log(name, "child", i, c, "visible", c.visible,
                        "y", c.y, "h", c.height, "implicit", c.implicitHeight);
        }
    }

    function debugLayout() {
        debugDump("left", leftColumn);
        debugDump("center", centerColumn);
    }

    function debugOpenPalette() {
        if (!channelDialog.opened)
            channelDialog.open();
        Qt.callLater(function() {
            paletteOverlay.channelIndex = 0;
            paletteOverlay.currentColor =
                channelModel.count > 0 ? "" + channelModel.get(0).color : "";
            paletteOverlay.x = window.dp(40);
            paletteOverlay.y = window.dp(40);
            paletteOverlay.visible = true;
        });
    }

    function openWaveMenu() {
        timeMenu.x = Math.max(0, timePlot.width - window.dp(220));
        timeMenu.y = window.dp(4);
        timeMenu.open();
    }

    function saveWindowScreenshot() {
        const path = app.newScreenshotPath();
        if (path.length === 0) {
            importStatus.text = qsTr("无法创建截图目录");
            return;
        }
        window.contentItem.grabToImage(function(result) {
            importStatus.text = result.saveToFile(path)
                ? qsTr("截图已保存：%1").arg(path)
                : qsTr("截图保存失败：%1").arg(path);
        });
    }

    function formatBytes(value) {
        if (value >= 1048576)
            return (value / 1048576).toFixed(1) + " MB";
        if (value >= 1024)
            return (value / 1024).toFixed(1) + " KB";
        return value + " B";
    }

    function syncChannelSettings() {
        const items = app.channelSettings;
        if (channelModel.count !== items.length) {
            channelModel.clear();
            for (let i = 0; i < items.length; ++i)
                channelModel.append(items[i]);
            return;
        }
        for (let i = 0; i < items.length; ++i) {
            const item = items[i];
            for (const key of ["name", "color", "visible", "scale", "offset", "decimals",
                               "autoScale", "rangeMin", "rangeMax"]) {
                if (channelModel.get(i)[key] !== item[key])
                    channelModel.setProperty(i, key, item[key]);
            }
        }
    }

    function loadEditBinding(index) {
        if (index < 0 || index >= sliderModel.count) {
            editBindingId = 0;
            editLabel.text = "";
            editTemplate.text = "";
            editMin.text = "0";
            editMax.text = "10";
            editStep.text = "0.01";
            editValue.text = "0";
            editInclude.checked = false;
            editGroupCombo.currentIndex = Math.max(0, app.currentGroup);
            return;
        }

        const item = sliderModel.get(index);
        editBindingId = item.id;
        editLabel.text = item.label;
        editTemplate.text = item.template;
        editMin.text = item.min;
        editMax.text = item.max;
        editStep.text = item.step;
        editValue.text = item.value;
        editInclude.checked = item.checked;
        editGroupCombo.currentIndex =
            Math.max(0, app.commandGroups.indexOf(item.group));
    }

    function syncBindings() {
        sliderModel.clear();
        const items = app.oneClickBindings;
        for (let i = 0; i < items.length; ++i)
            sliderModel.append(items[i]);
        updateCheckedCount();
    }

    function syncActions() {
        actionModel.clear();
        const items = app.actionBindings;
        for (let i = 0; i < items.length; ++i)
            actionModel.append(items[i]);
    }

    function loadActionEdit(index) {
        if (index < 0 || index >= actionModel.count) {
            actionEditId = 0;
            actionLabel.text = "";
            actionOn.text = "";
            actionOff.text = "";
            actionKindCombo.currentIndex = 0;
            actionGroupCombo.currentIndex = Math.max(0, app.currentGroup);
            return;
        }
        const item = actionModel.get(index);
        actionEditId = item.id;
        actionLabel.text = item.label;
        actionOn.text = item.on;
        actionOff.text = item.off;
        actionKindCombo.currentIndex = item.kind;
        actionGroupCombo.currentIndex =
            Math.max(0, app.commandGroups.indexOf(item.group));
    }

    function updateCheckedCount() {
        let count = 0;
        for (let i = 0; i < sliderModel.count; ++i) {
            if (sliderModel.get(i).checked)
                ++count;
        }
        checkedCount = count;
    }

    function setAllChecked(value) {
        for (let i = 0; i < sliderModel.count; ++i) {
            sliderModel.setProperty(i, "checked", value);
            app.setBindingIncluded(sliderModel.get(i).id, value);
        }
        updateCheckedCount();
    }

    function isConnected() {
        return app.connected;
    }

    function toggleConnection() {
        if (isConnected()) {
            app.closeTransport();
            return;
        }

        switch (transportIndex) {
        case 0:
            app.openSerialAdvanced(portCombo.currentText,
                                   parseInt(baudCombo.currentText),
                                   5 + dataBitsCombo.currentIndex,
                                   parityCombo.currentIndex === 0 ? 0
                                       : (parityCombo.currentIndex === 1 ? 2 : 3),
                                   stopBitsCombo.currentIndex === 0 ? 1
                                       : (stopBitsCombo.currentIndex === 1 ? 3 : 2),
                                   flowCombo.currentIndex,
                                   dtrCheck.checked,
                                   rtsCheck.checked);
            break;
        case 1:
            app.openTcpClient(hostField.text,
                              parseInt(clientPortField.text),
                              bannerField.text);
            break;
        case 2:
            app.openTcpServer(parseInt(serverPortField.text));
            break;
        case 3:
            app.openUdp(udpHostField.text,
                        parseInt(udpRemotePortField.text),
                        parseInt(udpLocalPortField.text));
            break;
        case 4:
            app.openDemo();
            break;
        }
    }

    function doSend() {
        if (sendModeCombo.currentIndex === 1) {
            app.sendHex(quickSendField.text);
            return;
        }
        let text = quickSendField.text;
        if (lineEndingCombo.currentIndex === 1)
            text += "\n";
        else if (lineEndingCombo.currentIndex === 2)
            text += "\r\n";
        app.sendText(text);
    }

    onClosing: app.saveWindow(x, y, width, height)

    Shortcut {
        sequence: "Ctrl+Return"
        onActivated: window.doSend()
    }

    // Ctrl+Alt avoids stealing text-field undo inside editors.
    Shortcut {
        sequence: "Ctrl+Alt+Z"
        onActivated: app.undo()
    }
    Shortcut {
        sequence: "Ctrl+Alt+Y"
        onActivated: app.redo()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.dp(12)
        spacing: window.dp(10)

        RowLayout {
            Layout.fillWidth: true
            spacing: window.dp(8)

            Label {
                text: qsTr("VOFA2")
                font.pixelSize: window.fontPx(22)
                font.bold: true
            }

            ComboBox {
                id: protocolCombo
                model: ["JustFloat", "FireWater", "RawData"]
                Layout.preferredWidth: window.dp(130)
                Layout.minimumWidth: 104
                Component.onCompleted: {
                    const index = model.indexOf(app.protocol);
                    if (index >= 0)
                        currentIndex = index;
                }
                onActivated: app.protocol = currentText
            }

            ComboBox {
                id: densityCombo
                model: [qsTr("紧凑"), qsTr("标准"), qsTr("舒适"), qsTr("自定义")]
                Layout.preferredWidth: window.dp(110)
                Layout.minimumWidth: window.dp(76)
                onActivated: {
                    if (currentIndex === 3)
                        uiSettingsDialog.open();
                    else
                        app.setUiPreset(currentIndex);
                }
            }

            Label {
                text: app.connectionState
                color: app.connected ? "#1abc9c" : window.theme.error
                font.bold: true
            }

            Label {
                visible: window.width >= 1200
                text: "RX " + window.formatBytes(app.totalRxBytes)
                      + " · TX " + window.formatBytes(app.totalTxBytes)
                      + " · " + app.framesPerSecond.toFixed(0) + " 帧/秒"
                      + (app.droppedFrames > 0
                         ? " · 丢 " + app.droppedFrames : "")
                font.pixelSize: window.fontPx(10)
                color: window.theme.tip
            }

            Button {
                text: qsTr("连接栏")
                checkable: true
                checked: app.showConnectionPanel
                onClicked: app.showConnectionPanel = checked
            }

            Button {
                text: qsTr("参数/命令栏")
                checkable: true
                checked: app.showSliderPanel
                onClicked: app.showSliderPanel = checked
            }

            Button {
                id: remoteButton
                text: qsTr("远程调参")
                onClicked: remoteDialog.open()
            }

            Label {
                visible: app.remoteEnabled
                text: app.remoteClientCount > 0
                      ? qsTr("%1 台在线").arg(app.remoteClientCount)
                      : qsTr("等待手机")
                color: app.remoteClientCount > 0 ? window.theme.accent : window.theme.tip
                font.bold: app.remoteClientCount > 0
            }

            Button {
                id: moreButton
                text: qsTr("更多")
                onClicked: moreMenu.open()

                Menu {
                    id: moreMenu
                    y: moreButton.height
                    MenuItem {
                        text: qsTr("界面设置")
                        onTriggered: uiSettingsDialog.open()
                    }
                    MenuItem {
                        text: qsTr("显示参数/命令栏")
                        onTriggered: app.showSliderPanel = true
                    }
                    MenuItem {
                        text: qsTr("远程调参（手机）…")
                        onTriggered: remoteDialog.open()
                    }
                    MenuItem {
                        text: qsTr("导入旧命令")
                        onTriggered: importStatus.text = app.importLegacyFromDefault()
                    }
                    MenuItem {
                        text: qsTr("打开迁移报告")
                        onTriggered: importStatus.text = app.openMigrationReport()
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("撤销命令编辑 (Ctrl+Alt+Z)")
                        enabled: app.canUndo
                        onTriggered: app.undo()
                    }
                    MenuItem {
                        text: qsTr("重做命令编辑 (Ctrl+Alt+Y)")
                        enabled: app.canRedo
                        onTriggered: app.redo()
                    }
                    MenuItem {
                        text: qsTr("导出CSV")
                        onTriggered: importStatus.text = app.exportCsv()
                    }
                    MenuItem {
                        text: qsTr("导入CSV录波…")
                        onTriggered: {
                            const path = app.chooseCsvToImport();
                            if (path.length > 0)
                                importStatus.text = app.loadCsvRecording(path);
                        }
                    }
                    MenuItem {
                        text: qsTr("发送文件…")
                        onTriggered: {
                            const path = app.chooseFileToSend();
                            if (path.length > 0)
                                importStatus.text = app.sendFile(path);
                        }
                    }
                    MenuItem {
                        text: qsTr("保存窗口截图")
                        onTriggered: window.saveWindowScreenshot()
                    }
                    MenuItem {
                        text: qsTr("打开配置目录")
                        onTriggered: Qt.openUrlExternally("file:///" + app.configDir)
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("图传")
                visible: app.imageCount > 0
                onClicked: window.showImagePanel = !window.showImagePanel
            }

            Button {
                text: qsTr("一键发送数据")
                onClicked: sendDialog.open()
            }
        }

        Label {
            id: importStatus
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: window.theme.text
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: window.dp(10)

            Frame {
                id: leftPanel
                clip: true
                visible: app.showConnectionPanel
                Layout.preferredWidth: Math.max(window.dp(200),
                                                Math.min(window.dp(300), window.width * 0.22))
                Layout.minimumWidth: 200
                Layout.fillHeight: true

                ColumnLayout {
                    id: leftColumn
                    anchors.fill: parent
                    spacing: window.dp(4)

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("连接")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            id: transportCombo
                            model: [qsTr("串口"), qsTr("TCP 客户端"), qsTr("TCP 服务端"),
                                    qsTr("UDP"), qsTr("演示")]
                            Layout.preferredWidth: window.dp(140)
                            Layout.minimumWidth: 108
                            currentIndex: window.transportIndex
                            onActivated: window.transportIndex = currentIndex
                        }
                    }

                    // Serial
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: window.transportIndex === 0
                        spacing: window.dp(4)

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("端口") }
                            ComboBox {
                                id: portCombo
                                Layout.fillWidth: true
                                editable: true
                                model: app.serialPorts
                                Component.onCompleted: {
                                    if (count > 0)
                                        currentIndex = 0;
                                }
                            }
                            Button {
                                text: qsTr("刷新")
                                onClicked: app.refreshSerialPorts()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("波特率") }
                            ComboBox {
                                id: baudCombo
                                Layout.fillWidth: true
                                editable: true
                                model: ["9600", "115200", "921600", "1000000", "2000000"]
                                currentIndex: 2
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: window.dp(6)
                            Label { text: qsTr("数据位") }
                            ComboBox {
                                id: dataBitsCombo
                                Layout.fillWidth: true
                                model: ["5", "6", "7", "8"]
                                currentIndex: 3
                            }
                            Label { text: qsTr("校验") }
                            ComboBox {
                                id: parityCombo
                                Layout.fillWidth: true
                                model: [qsTr("无"), qsTr("偶"), qsTr("奇")]
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: window.dp(6)
                            Label { text: qsTr("停止位") }
                            ComboBox {
                                id: stopBitsCombo
                                Layout.fillWidth: true
                                model: ["1", "1.5", "2"]
                            }
                            Label { text: qsTr("流控") }
                            ComboBox {
                                id: flowCombo
                                Layout.fillWidth: true
                                model: [qsTr("无"), qsTr("硬件"), qsTr("软件")]
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox { id: dtrCheck; text: "DTR" }
                            CheckBox { id: rtsCheck; text: "RTS" }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                id: breakCheck
                                text: qsTr("BREAK")
                                enabled: app.connected && window.transportIndex === 0
                                checked: app.serialBreak
                                onToggled: app.setSerialBreak(checked)
                            }
                            Label {
                                text: qsTr("保持串口 TX 为 BREAK 状态")
                                color: window.theme.tip
                                font.pixelSize: window.fontPx(10)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // TCP client
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: window.transportIndex === 1
                        spacing: window.dp(4)

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("主机") }
                            TextField {
                                id: hostField
                                Layout.fillWidth: true
                                text: "127.0.0.1"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("端口") }
                            TextField {
                                id: clientPortField
                                Layout.fillWidth: true
                                text: "1346"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("握手") }
                            TextField {
                                id: bannerField
                                Layout.fillWidth: true
                                text: "plot0"
                            }
                        }
                    }

                    // TCP server
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: window.transportIndex === 2
                        spacing: window.dp(4)

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("监听端口") }
                            TextField {
                                id: serverPortField
                                Layout.fillWidth: true
                                text: app.tcpServerPort
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("当前客户端") }
                            ComboBox {
                                id: clientCombo
                                Layout.fillWidth: true
                                model: app.clientLabels
                                onActivated: {
                                    if (currentIndex >= 0 && currentIndex < app.clientIds.length)
                                        app.setActiveClient(app.clientIds[currentIndex]);
                                }
                            }
                        }
                    }

                    // UDP
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: window.transportIndex === 3
                        spacing: window.dp(4)

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("远端主机") }
                            TextField {
                                id: udpHostField
                                Layout.fillWidth: true
                                text: "127.0.0.1"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("远端端口") }
                            TextField {
                                id: udpRemotePortField
                                Layout.fillWidth: true
                                text: "1346"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("本地端口") }
                            TextField {
                                id: udpLocalPortField
                                Layout.fillWidth: true
                                text: "1347"
                            }
                        }
                    }

                    CheckBox {
                        id: autoReconnectCheck
                        text: qsTr("断线后自动重连")
                        checked: app.autoReconnect
                        onToggled: app.autoReconnect = checked
                    }

                    Button {
                        Layout.fillWidth: true
                        text: window.isConnected() ? qsTr("断开") : qsTr("连接")
                        onClicked: window.toggleConnection()
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: app.lastError.length > 0
                        text: app.lastError
                        color: window.theme.error
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: window.theme.separator
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("发送")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            id: sendModeCombo
                            model: [qsTr("文本"), qsTr("HEX")]
                            Layout.preferredWidth: window.dp(100)
                            Layout.minimumWidth: 64
                        }
                        ComboBox {
                            id: lineEndingCombo
                            model: [qsTr("无换行"), "\\n", "\\r\\n"]
                            Layout.preferredWidth: window.dp(96)
                            Layout.minimumWidth: 82
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: quickSendField
                            Layout.fillWidth: true
                            Layout.minimumWidth: window.dp(80)
                            placeholderText: qsTr("输入要发送的数据")
                            onAccepted: window.doSend()
                        }
                        Button {
                            text: qsTr("发送")
                            Layout.preferredWidth: window.dp(56)
                            onClicked: window.doSend()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: window.dp(6)

                        Label {
                            text: qsTr("发送历史")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Button {
                            text: qsTr("清空")
                            Layout.preferredWidth: window.dp(48)
                            Layout.minimumWidth: 48
                            enabled: app.recentSends.length > 0
                            onClicked: app.clearRecentSends()
                        }
                    }

                    TextField {
                        id: sendFilterField
                        Layout.fillWidth: true
                        placeholderText: qsTr("过滤发送历史")
                        text: app.sendFilter
                        onTextEdited: app.sendFilter = text
                    }

                    Frame {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: window.dp(80)
                        padding: window.dp(2)

                        ListView {
                        id: historyList
                        anchors.fill: parent
                        clip: true
                        spacing: window.dp(2)
                        model: app.filteredRecentSends
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            width: historyList.width
                            height: window.dp(26)

                            Rectangle {
                                anchors.fill: parent
                                radius: window.dp(3)
                                color: historyMouse.containsMouse
                                       ? window.theme.separator : "transparent"
                            }

                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: window.dp(6)
                                anchors.rightMargin: window.dp(6)
                                text: (modelData.hex ? "HEX  " : "TEXT  ")
                                      + modelData.text
                                elide: Text.ElideRight
                                color: window.theme.text
                                font.pixelSize: window.fontPx(11)
                            }

                            MouseArea {
                                id: historyMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    quickSendField.text = modelData.text;
                                    sendModeCombo.currentIndex =
                                        modelData.hex ? 1 : 0;
                                    app.repeatRecentSendEntry(modelData.text,
                                                              modelData.hex);
                                }
                            }
                        }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: app.filteredRecentSends.length === 0
                            text: app.sendFilter.length > 0
                                  ? qsTr("无匹配的发送记录")
                                  : qsTr("暂无发送历史")
                            color: window.theme.tip
                            font.pixelSize: window.fontPx(11)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: window.dp(6)

                        CheckBox {
                            id: loopCheck
                            text: qsTr("循环")
                        }
                        TextField {
                            id: loopIntervalField
                            Layout.preferredWidth: window.dp(70)
                            Layout.minimumWidth: window.dp(48)
                            text: "500"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 10; top: 5000 }
                        }
                        Label { text: "ms" }
                        Item { Layout.fillWidth: true }
                    }

                    Timer {
                        id: loopTimer
                        interval: Math.max(10, parseInt(loopIntervalField.text) || 500)
                        repeat: true
                        running: loopCheck.checked
                        onTriggered: window.doSend()
                    }
                }
            }

            Frame {
                id: centerPanel
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: window.dp(320)

                ColumnLayout {
                    id: centerColumn
                    anchors.fill: parent
                    spacing: window.dp(2)

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("接收 / 最新通道值")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("通道数: %1").arg(app.channelCount)
                        }
                        Label {
                            visible: app.receiveFilter.length > 0
                            text: qsTr("过滤: %1").arg(app.receiveFilter)
                            color: window.theme.tip
                            elide: Text.ElideRight
                            Layout.maximumWidth: window.dp(180)
                        }
                        CheckBox {
                            id: autoScrollBox
                            text: qsTr("自动滚动")
                            checked: true
                        }
                        Button {
                            id: logButton
                            text: qsTr("日志")
                            onClicked: logMenu.open()

                            Menu {
                                id: logMenu
                                y: logButton.height

                                MenuItem {
                                    id: pauseLogItem
                                    text: qsTr("暂停日志")
                                    checkable: true
                                    checked: app.receivePaused
                                    onTriggered: app.receivePaused = checked
                                }
                                MenuItem {
                                    id: timestampItem
                                    text: qsTr("时间戳")
                                    checkable: true
                                    checked: app.receiveTimestamps
                                    onTriggered: app.receiveTimestamps = checked
                                }
                                MenuItem {
                                    id: hexItem
                                    text: qsTr("HEX 显示")
                                    checkable: true
                                    checked: app.receiveHex
                                    onTriggered: app.receiveHex = checked
                                }
                                MenuItem {
                                    id: ansiItem
                                    text: qsTr("ANSI 颜色")
                                    checkable: true
                                    checked: app.receiveAnsi
                                    onTriggered: app.receiveAnsi = checked
                                }
                                MenuItem {
                                    text: qsTr("日志显示发送")
                                    checkable: true
                                    checked: app.receiveTxEcho
                                    onTriggered: app.receiveTxEcho = checked
                                }
                                MenuItem {
                                    text: qsTr("过滤…")
                                    onTriggered: filterDialog.open()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    id: utf8Item
                                    text: qsTr("编码：UTF-8")
                                    checkable: true
                                    checked: app.receiveEncoding === 0
                                    onTriggered: app.receiveEncoding = 0
                                }
                                MenuItem {
                                    id: systemEncodingItem
                                    text: qsTr("编码：系统 (GBK)")
                                    checkable: true
                                    checked: app.receiveEncoding === 1
                                    onTriggered: app.receiveEncoding = 1
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("清空日志")
                                    onTriggered: app.clearReceive()
                                }
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.minimumWidth: 0
                        spacing: window.dp(6)

                        Button {
                            text: qsTr("跟随")
                            enabled: app.waveformMode === 0
                            onClicked: timePlot.showFollow()
                        }
                        Button {
                            text: qsTr("全览")
                            enabled: app.waveformMode === 0
                            onClicked: timePlot.showAll()
                        }
                        Button {
                            text: qsTr("Y轴")
                            enabled: app.waveformMode === 0
                            onClicked: yRangeDialog.open()
                        }
                        Button {
                            text: qsTr("触发")
                            enabled: app.waveformMode === 0
                            onClicked: triggerDialog.open()
                        }
                        Button {
                            text: qsTr("游标")
                            checkable: true
                            checked: timePlot.cursorsEnabled
                            enabled: app.waveformMode === 0
                            onToggled: timePlot.setCursorsEnabled(checked)
                        }
                        Button {
                            text: app.replayActive ? qsTr("回放中") : qsTr("回放")
                            onClicked: replayDialog.open()
                        }
                        Button {
                            text: qsTr("清空")
                            onClicked: app.clearWaveform()
                        }
                        ComboBox {
                            id: waveformModeCombo
                            width: Math.max(window.dp(96), 92)
                            model: [qsTr("时域"), "XY", qsTr("频谱"), qsTr("直方图")]
                            currentIndex: app.waveformMode
                            onActivated: app.waveformMode = currentIndex
                        }
                        Button {
                            text: "?"
                            width: window.dp(34)
                            onClicked: helpDialog.open()
                        }
                        Button {
                            text: qsTr("波形菜单")
                            enabled: app.waveformMode === 0
                            onClicked: window.openWaveMenu()
                        }
                        Label {
                            text: app.waveformMode === 0
                                  ? timePlot.viewMode + " · "
                                    + window.formatSpan(timePlot.visibleSpanSeconds)
                                  : ""
                            color: window.theme.tip
                            font.pixelSize: window.fontPx(10)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: window.dp(150)
                        Layout.preferredHeight: window.dp(320)
                        color: window.theme.waveBg
                        border.color: window.theme.waveBorder
                        clip: true

                        TimePlotItem {
                            id: timePlot
                            anchors.fill: parent
                            anchors.margins: window.dp(6)
                            visible: app.waveformMode === 0
                            controller: app
                            gridColor: window.theme.grid
                            onMenuRequested: function(x, y) {
                                timeMenu.x = x;
                                timeMenu.y = y;
                                timeMenu.open();
                            }

                            Menu {
                                id: timeMenu
                                MenuItem {
                                    text: qsTr("显示全部")
                                    onTriggered: timePlot.showAll()
                                }
                                MenuItem {
                                    text: qsTr("自动跟随")
                                    onTriggered: timePlot.showFollow()
                                }
                                MenuItem {
                                    text: qsTr("自适应纵轴")
                                    checkable: true
                                    checked: timePlot.autoY
                                    onTriggered: timePlot.autoY = checked
                                }
                                MenuItem {
                                    text: qsTr("Y 轴范围…")
                                    onTriggered: yRangeDialog.open()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("清空波形")
                                    onTriggered: app.clearWaveform()
                                }
                                MenuItem {
                                    text: qsTr("通道设置")
                                    onTriggered: channelDialog.open()
                                }
                                MenuItem {
                                    text: qsTr("保存窗口截图")
                                    onTriggered: window.saveWindowScreenshot()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("操作说明…")
                                    onTriggered: helpDialog.open()
                                }
                            }
                        }

                        WaveformItem {
                            id: waveItem
                            anchors.fill: parent
                            anchors.margins: window.dp(6)
                            visible: app.waveformMode !== 0
                            controller: app
                            mode: app.waveformMode
                            axisFontPx: window.fontPx(12)
                            gridColor: window.theme.grid
                            onWidthChanged: update()
                            onHeightChanged: update()
                        }

                        Item {
                            id: axisOverlay
                            anchors.fill: parent
                            anchors.margins: window.dp(6)
                            visible: app.waveformMode === 0
                            enabled: false // purely visual; never steal wheel/hover events
                            property int divisions: width >= 720 ? 10 : 5

                            Rectangle {
                                visible: timePlot.cursorsEnabled
                                         && timePlot.cursorAVisible
                                x: Math.round(timePlot.cursorAX)
                                y: 0
                                width: Math.max(1, window.dp(1))
                                height: Math.max(0, parent.height - window.dp(18))
                                color: "#00bcd4"
                                opacity: 0.9
                            }
                            Label {
                                visible: timePlot.cursorsEnabled
                                         && timePlot.cursorAVisible
                                x: Math.round(timePlot.cursorAX) + window.dp(3)
                                y: 0
                                text: "A"
                                color: "#00bcd4"
                                font.bold: true
                                font.pixelSize: window.fontPx(11)
                            }
                            Rectangle {
                                visible: timePlot.cursorsEnabled
                                         && timePlot.cursorBVisible
                                x: Math.round(timePlot.cursorBX)
                                y: 0
                                width: Math.max(1, window.dp(1))
                                height: Math.max(0, parent.height - window.dp(18))
                                color: "#ff9800"
                                opacity: 0.9
                            }
                            Label {
                                visible: timePlot.cursorsEnabled
                                         && timePlot.cursorBVisible
                                x: Math.round(timePlot.cursorBX) - window.dp(14)
                                y: 0
                                text: "B"
                                color: "#ff9800"
                                font.bold: true
                                font.pixelSize: window.fontPx(11)
                            }

                            Rectangle {
                                id: cursorReadout
                                visible: timePlot.cursorsEnabled
                                x: window.dp(44)
                                // Bottom-left, above the time labels, so the
                                // A/B markers at the top stay visible.
                                y: parent.height - height - window.dp(20)
                                width: cursorColumn.implicitWidth + window.dp(14)
                                height: cursorColumn.implicitHeight + window.dp(10)
                                radius: window.dp(3)
                                color: window.theme.panel
                                opacity: 0.94
                                border.color: window.theme.separator

                                Column {
                                    id: cursorColumn
                                    anchors.centerIn: parent
                                    spacing: window.dp(1)

                                    Label {
                                        text: "A " + window.formatReadout(timePlot.cursorTimeA)
                                              + " s   B "
                                              + window.formatReadout(timePlot.cursorTimeB)
                                              + " s   Δ "
                                              + window.formatReadout(timePlot.cursorDeltaSeconds)
                                              + " s"
                                        color: window.theme.text
                                        font.pixelSize: window.fontPx(10)
                                    }

                                    Repeater {
                                        model: Math.min(8, timePlot.cursorValuesA.length)
                                        Row {
                                            spacing: window.dp(6)
                                            Rectangle {
                                                width: window.dp(6)
                                                height: window.dp(6)
                                                radius: 1
                                                color: timePlot.cursorColors[index]
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                width: window.dp(34)
                                                text: "CH" + index
                                                color: window.theme.text
                                                font.pixelSize: window.fontPx(10)
                                            }
                                            Label {
                                                width: window.dp(62)
                                                text: "A " + window.formatReadout(
                                                              timePlot.cursorValuesA[index])
                                                color: "#00bcd4"
                                                font.family: "monospace"
                                                font.pixelSize: window.fontPx(10)
                                            }
                                            Label {
                                                width: window.dp(62)
                                                text: "B " + window.formatReadout(
                                                              timePlot.cursorValuesB[index])
                                                color: "#ff9800"
                                                font.family: "monospace"
                                                font.pixelSize: window.fontPx(10)
                                            }
                                            Label {
                                                width: window.dp(62)
                                                text: "Δ " + window.formatReadout(
                                                              timePlot.cursorDeltas[index])
                                                color: window.theme.accent
                                                font.family: "monospace"
                                                font.pixelSize: window.fontPx(10)
                                            }
                                        }
                                    }

                                    Label {
                                        visible: timePlot.cursorValuesA.length > 8
                                        text: qsTr("…")
                                        color: window.theme.tip
                                        font.pixelSize: window.fontPx(10)
                                    }
                                }
                            }

                            Label {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.leftMargin: window.dp(8)
                                anchors.topMargin: window.dp(2)
                                text: timePlot.yMaxVisible.toFixed(2)
                                color: window.theme.axisText
                                font.pixelSize: window.fontPx(11)
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: window.dp(8)
                                y: Math.round(parent.height * 0.25 - height / 2)
                                text: (timePlot.yMinVisible
                                       + (timePlot.yMaxVisible - timePlot.yMinVisible)
                                         * 0.75).toFixed(2)
                                color: window.theme.axisText
                                font.pixelSize: window.fontPx(11)
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: window.dp(8)
                                text: timePlot.yMidVisible.toFixed(2)
                                color: window.theme.axisText
                                font.pixelSize: window.fontPx(11)
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: window.dp(8)
                                y: Math.round(parent.height * 0.75 - height / 2)
                                text: (timePlot.yMinVisible
                                       + (timePlot.yMaxVisible - timePlot.yMinVisible)
                                         * 0.25).toFixed(2)
                                color: window.theme.axisText
                                font.pixelSize: window.fontPx(11)
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: window.dp(8)
                                anchors.bottomMargin: window.dp(18)
                                text: timePlot.yMinVisible.toFixed(2)
                                color: window.theme.axisText
                                font.pixelSize: window.fontPx(11)
                            }

                            Repeater {
                                model: axisOverlay.divisions + 1
                                Label {
                                    required property int index
                                    text: window.formatAxisTime(timePlot.visibleSpanSeconds,
                                                                index,
                                                                axisOverlay.divisions)
                                    color: window.theme.axisText
                                    font.pixelSize: window.fontPx(11)
                                    x: Math.max(window.dp(2),
                                                Math.min(parent.width - width - window.dp(2),
                                                         (parent.width - width) * index
                                                         / axisOverlay.divisions))
                                    y: parent.height - height - window.dp(2)
                                }
                            }

                            Rectangle {
                                visible: timePlot.hoverVisible
                                x: Math.max(
                                       window.dp(2),
                                       Math.min(parent.width - width - window.dp(2),
                                                timePlot.hoverX - width / 2))
                                y: parent.height - height - window.dp(2)
                                width: hoverXLabel.implicitWidth + window.dp(8)
                                height: hoverXLabel.implicitHeight + window.dp(2)
                                radius: window.dp(2)
                                color: window.theme.panel
                                opacity: 0.95
                                border.color: window.theme.separator

                                Label {
                                    id: hoverXLabel
                                    anchors.centerIn: parent
                                    text: window.formatHoverTime(
                                              timePlot.hoverTimeSeconds)
                                    color: window.theme.axisText
                                    font.pixelSize: window.fontPx(11)
                                }
                            }

                            Rectangle {
                                visible: timePlot.hoverVisible
                                // Right of the static Y labels so the moving
                                // reference value is not hidden behind them.
                                x: window.dp(44)
                                y: Math.max(window.dp(2),
                                            Math.min(parent.height - height - window.dp(2),
                                                     timePlot.hoverY - height / 2))
                                width: hoverYLabel.implicitWidth + window.dp(8)
                                height: hoverYLabel.implicitHeight + window.dp(2)
                                radius: window.dp(2)
                                color: window.theme.panel
                                opacity: 0.95
                                border.color: window.theme.separator

                                Label {
                                    id: hoverYLabel
                                    anchors.centerIn: parent
                                    text: timePlot.hoverYValue.toFixed(2)
                                    color: window.theme.axisText
                                    font.pixelSize: window.fontPx(11)
                                }
                            }

                            Rectangle {
                                visible: timePlot.hoverVisible
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: window.dp(8)
                                anchors.topMargin: window.dp(8)
                                radius: window.dp(3)
                                color: window.theme.panel
                                opacity: 0.93
                                border.color: window.theme.separator
                                width: hoverColumn.implicitWidth + window.dp(16)
                                height: hoverColumn.implicitHeight + window.dp(12)

                                Column {
                                    id: hoverColumn
                                    anchors.centerIn: parent
                                    spacing: window.dp(1)
                                    Repeater {
                                        model: timePlot.hoverValues
                                        Label {
                                            required property var modelData
                                            required property int index
                                            text: "CH" + index + "  "
                                                  + Number(modelData).toFixed(3)
                                            color: timePlot.hoverColors[index]
                                            font.family: "monospace"
                                            font.pixelSize: window.fontPx(11)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Connections {
                        target: app
                        function onWaveformChanged() {
                            if (app.waveformMode !== 0)
                                waveItem.update();
                        }
                    }
                    Frame {
                        id: imagePanel
                        visible: app.imageCount > 0 && window.showImagePanel
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(window.dp(150),
                                                         Math.min(window.dp(280),
                                                                  window.height * 0.32))
                        padding: window.dp(6)

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: window.dp(4)

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: qsTr("图传")
                                    font.bold: true
                                }
                                Label {
                                    text: app.imageInfo
                                    color: window.theme.tip
                                    font.pixelSize: window.fontPx(10)
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("共 %1 帧").arg(app.imageCount)
                                    color: window.theme.tip
                                    font.pixelSize: window.fontPx(10)
                                }
                                Button {
                                    text: qsTr("隐藏")
                                    onClicked: window.showImagePanel = false
                                }
                            }

                            Image {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                source: app.imageSource
                                cache: false
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                onSourceChanged: imageError.text = ""
                                onStatusChanged: {
                                    if (status === Image.Error)
                                        imageError.text = qsTr("图像解码失败");
                                }
                            }

                            Label {
                                id: imageError
                                visible: text.length > 0
                                color: window.theme.error
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: window.dp(66)
                        Layout.minimumHeight: window.dp(66)
                        Layout.maximumHeight: window.dp(66)

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: window.dp(66)
                            Layout.maximumHeight: window.dp(66)
                            orientation: ListView.Horizontal
                            model: channelModel
                            spacing: window.dp(8)
                            clip: true

                            delegate: Frame {
                                id: valueCard
                                width: window.dp(120)
                                height: window.dp(66)
                                padding: window.dp(4)
                                opacity: model.visible ? 1.0 : 0.5

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: app.setChannelVisible(index, !model.visible)
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: window.dp(2)

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: window.dp(4)

                                        Rectangle {
                                            width: window.dp(8)
                                            height: window.dp(8)
                                            radius: window.dp(2)
                                            color: model.color
                                        }
                                        Label {
                                            text: model.name
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Label {
                                        text: {
                                            const list = timePlot.hoverVisible
                                                ? timePlot.hoverValues
                                                : app.latestValues;
                                            if (index >= list.length)
                                                return "--";
                                            return Number(list[index]).toFixed(model.decimals);
                                        }
                                        font.family: "monospace"
                                    }
                                }
                            }
                        }
                    }

                    Frame {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: window.dp(110)
                        Layout.preferredHeight: window.dp(240)
                        padding: window.dp(3)

                        ScrollView {
                            id: logScroll
                            anchors.fill: parent

                            TextArea {
                                id: logText
                                text: app.receiveText
                                textFormat: app.receiveAnsi ? TextEdit.RichText
                                                            : TextEdit.PlainText
                                readOnly: true
                                wrapMode: Text.WrapAnywhere
                                font.family: "monospace"
                            }
                        }

                        Label {
                            anchors.fill: parent
                            anchors.margins: window.dp(10)
                            visible: app.receiveText.length === 0
                            wrapMode: Text.WordWrap
                            color: window.theme.tip
                            text: qsTr("接收日志：文本协议或 HEX 模式的数据会显示在这里。")
                        }
                    }
                }
            }

            Frame {
                id: rightPanel
                clip: true
                visible: app.showSliderPanel
                Layout.preferredWidth: Math.max(window.dp(220),
                                                Math.min(window.dp(320), window.width * 0.26))
                Layout.fillHeight: true
                Layout.minimumWidth: 210

                ColumnLayout {
                    anchors.fill: parent
                    spacing: window.dp(8)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: window.dp(6)

                        Label { text: qsTr("命令组") }
                        ComboBox {
                            id: groupCombo
                            Layout.fillWidth: true
                            Layout.minimumWidth: 90
                            model: app.commandGroups
                            currentIndex: app.currentGroup
                            onActivated: app.setCurrentGroup(index)
                        }
                        Button {
                            text: qsTr("管理")
                            Layout.preferredWidth: window.dp(56)
                            Layout.minimumWidth: 56
                            onClicked: groupDialog.open()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("命令 / 开关")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("%1 项").arg(actionModel.count)
                            color: window.theme.tip
                        }
                        Button {
                            text: qsTr("编辑")
                            Layout.preferredWidth: window.dp(56)
                            Layout.minimumWidth: 56
                            onClicked: {
                                actionEditCombo.currentIndex =
                                    actionModel.count > 0 ? 0 : -1;
                                window.loadActionEdit(0);
                                actionDialog.open();
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: actionModel.count === 0
                        wrapMode: Text.WordWrap
                        color: window.theme.tip
                        text: qsTr("当前命令组没有命令/开关。点击上面的“编辑”添加启动、停止、自检等动作。")
                    }

                    ListView {
                        id: actionView
                        Layout.fillWidth: true
                        Layout.minimumHeight: 0
                        Layout.preferredHeight: actionModel.count > 0
                            ? Math.min(actionModel.count * (window.dp(34) + window.dp(2))
                                       + window.dp(8), window.dp(260))
                            : 0
                        visible: actionModel.count > 0
                        model: actionModel
                        clip: true
                        spacing: window.dp(2)

                        delegate: Item {
                            width: actionView.width
                            height: window.dp(34)

                            CheckBox {
                                anchors.fill: parent
                                visible: model.kind === 1
                                text: model.label
                                checked: model.state
                                onToggled: app.setActionState(model.id, checked)
                            }

                            Button {
                                anchors.fill: parent
                                visible: model.kind === 0
                                text: model.label
                                onClicked: app.triggerAction(model.id)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("参数滑块")
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Button {
                            text: qsTr("编辑")
                            Layout.preferredWidth: window.dp(56)
                            Layout.minimumWidth: 56
                            onClicked: {
                                editCombo.currentIndex = sliderModel.count > 0 ? 0 : -1;
                                window.loadEditBinding(0);
                                editDialog.open();
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            id: sendOnMoveBox
                            text: qsTr("拖动时实时发送")
                            checked: app.sendOnMove
                            onToggled: app.sendOnMove = checked
                        }
                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        id: sliderView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: window.dp(80)
                        Layout.preferredHeight: window.dp(320)
                        model: sliderModel
                        clip: true

                        delegate: ColumnLayout {
                            width: sliderView.width
                            spacing: window.dp(2)

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: model.label
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                TextField {
                                    id: valueField
                                    Layout.preferredWidth: window.dp(86)
                                    Layout.minimumWidth: window.dp(66)
                                    text: window.formatNumber(model.value)
                                    horizontalAlignment: TextInput.AlignRight
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    selectByMouse: true
                                    // Accept any number for debugging, including
                                    // values outside the slider's min/max.
                                    onEditingFinished: {
                                        const typed = Number(text.trim());
                                        if (!isFinite(typed) || text.trim() === "") {
                                            text = window.formatNumber(model.value);
                                            return;
                                        }
                                        text = window.formatNumber(typed);
                                        sliderModel.setProperty(index, "value", typed);
                                        const bindingId = model.id;
                                        Qt.callLater(function() {
                                            app.setBindingValueExact(bindingId, typed);
                                        });
                                    }
                                }
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: model.min
                                to: model.max
                                value: model.value
                                stepSize: model.step
                                onMoved: {
                                    // 只改模型；输入框的 text 绑在 model.value 上，
                                    // 手动赋值会切断绑定，导致后续远程改动不再刷新。
                                    sliderModel.setProperty(index, "value", value);
                                    app.setBindingValue(model.id, value);
                                }
                                onPressedChanged: {
                                    if (!pressed)
                                        app.commitBindingValue(model.id);
                                }
                            }
                        }
                    }

                }
            }
        }
    }

    ListModel {
        id: sliderModel
    }

    ListModel {
        id: actionModel
    }

    ListModel {
        id: channelModel
    }

    Connections {
        target: app
        function onOneClickBindingsChanged() {
            window.syncBindings();
        }
        // 手机端拖滑块只会走这里：只改对应那一行，不整表重建。
        function onRemoteValueApplied(bindingId, value) {
            for (let i = 0; i < sliderModel.count; ++i) {
                if (sliderModel.get(i).id == bindingId) {
                    sliderModel.setProperty(i, "value", value);
                    break;
                }
            }
        }
        function onActionBindingsChanged() {
            window.syncActions();
        }
        function onCommandGroupsChanged() {
            groupCombo.currentIndex = app.currentGroup;
            groupManageCombo.currentIndex = app.currentGroup;
            moveTargetCombo.currentIndex = app.currentGroup;
        }
        function onCurrentGroupChanged() {
            groupCombo.currentIndex = app.currentGroup;
            groupManageCombo.currentIndex = app.currentGroup;
            moveTargetCombo.currentIndex = app.currentGroup;
        }
        function onSendOnMoveChanged() {
            sendOnMoveBox.checked = app.sendOnMove;
        }
        function onRemoteChanged() {
            remoteEnableBox.checked = app.remoteEnabled;
        }
        function onTransportKindChanged() {
            window.transportIndex = app.transportKind;
            transportCombo.currentIndex = app.transportKind;
        }
        function onConnectionParamsChanged() {
            hostField.text = app.tcpClientHost;
            clientPortField.text = app.tcpClientPort;
            bannerField.text = app.tcpClientBanner;
            serverPortField.text = app.tcpServerPort;
        }
        function onChannelSettingsChanged() {
            window.syncChannelSettings();
        }
        function onReceiveOptionsChanged() {
            pauseLogItem.checked = app.receivePaused;
            timestampItem.checked = app.receiveTimestamps;
            hexItem.checked = app.receiveHex;
            ansiItem.checked = app.receiveAnsi;
            utf8Item.checked = app.receiveEncoding === 0;
            systemEncodingItem.checked = app.receiveEncoding === 1;
        }
        function onProtocolChanged() {
            const index = protocolCombo.model.indexOf(app.protocol);
            if (index >= 0)
                protocolCombo.currentIndex = index;
        }
        function onWaveformModeChanged() {
            waveformModeCombo.currentIndex = app.waveformMode;
        }
        function onUiPresetChanged() {
            densityCombo.currentIndex = app.uiPreset >= 0 ? app.uiPreset : 3;
        }
        function onUiDensityChanged() {
            densitySlider.value = app.uiDensity;
        }
        function onUiFontScaleChanged() {
            fontSlider.value = app.uiFontScale;
        }
        function onUiThemeChanged() {
            themeCombo.currentIndex = app.uiTheme;
        }
        function onReceiveTextChanged() {
            if (!autoScrollBox.checked)
                return;
            Qt.callLater(function() {
                if (logScroll.contentItem)
                    logScroll.contentItem.contentY =
                        Math.max(0, logScroll.contentItem.contentHeight
                                    - logScroll.contentItem.height);
            });
        }
        function onDialogRequested(name) {
            if (name === "oneclick")
                sendDialog.open();
            else if (name === "channels")
                channelDialog.open();
            else if (name === "edit")
                editDialog.open();
            else if (name === "ui")
                uiSettingsDialog.open();
            else if (name === "actions") {
                actionEditCombo.currentIndex = actionModel.count > 0 ? 0 : -1;
                window.loadActionEdit(0);
                actionDialog.open();
            }
            else if (name === "groups") {
                groupDialog.open();
            }
            else if (name === "help") {
                helpDialog.open();
            }
            else if (name === "trigger") {
                triggerDialog.open();
            }
            else if (name === "replay") {
                replayDialog.open();
            }
            else if (name === "filter") {
                filterDialog.open();
            }
            else if (name === "remote") {
                remoteDialog.open();
            }
        }
    }

    Connections {
        target: timePlot
        function onSpanChanged() {
            if (timePlot.spanSeconds > 0)
                app.waveformWindowSeconds = timePlot.spanSeconds;
        }
    }

    Component.onCompleted: {
        window.transportIndex = app.transportKind;
        transportCombo.currentIndex = app.transportKind;
        hostField.text = app.tcpClientHost;
        clientPortField.text = app.tcpClientPort;
        bannerField.text = app.tcpClientBanner;
        serverPortField.text = app.tcpServerPort;
        densityCombo.currentIndex = app.uiPreset >= 0 ? app.uiPreset : 3;
        groupCombo.currentIndex = app.currentGroup;
        if (app.waveformWindowSeconds > 0)
            timePlot.spanSeconds = app.waveformWindowSeconds;
        else
            timePlot.showAll();
        window.syncBindings();
        window.syncActions();
        window.syncChannelSettings();

        // Restore the previous window geometry, but clamp it to the current
        // screen (a monitor or DPI change can make old geometry invalid).
        const saved = app.savedWindowGeometry();
        if (saved.valid) {
            width = Math.max(minimumWidth, Math.min(saved.width, Screen.width - 24));
            height = Math.max(minimumHeight, Math.min(saved.height, Screen.height - 48));
            x = Math.max(0, Math.min(saved.x, Screen.width - width));
            y = Math.max(0, Math.min(saved.y, Screen.height - height));
        } else {
            x = Math.max(0, Math.round((Screen.width - width) / 2));
            y = Math.max(0, Math.round((Screen.height - height) / 2));
        }
    }

    Dialog {
        id: sendDialog
        title: qsTr("一键发送数据")
        modal: true
        width: Math.min(window.dp(600), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: window.dp(10)

            Label {
                text: qsTr("当前命令组：%1。选择需要发送的滑块。按键/开关不会参与，避免误触发启动、停止等动作。")
                          .arg(app.currentGroupName)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                visible: !app.connected
                text: qsTr("当前未连接，请先在左侧连接设备后再一键发送。")
                color: window.theme.error
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("全选滑块")
                    onClicked: window.setAllChecked(true)
                }
                Button {
                    text: qsTr("全不选")
                    onClicked: window.setAllChecked(false)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("已选 %1 项").arg(window.checkedCount)
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.preferredHeight: window.dp(240)

                ListView {
                    id: sendListView
                    anchors.fill: parent
                    model: sliderModel
                    clip: true

                    delegate: CheckBox {
                        width: sendListView.width
                        text: model.label + "  (" + model.value.toFixed(3) + ")"
                        checked: model.checked
                        onToggled: {
                            sliderModel.setProperty(index, "checked", checked);
                            app.setBindingIncluded(model.id, checked);
                            window.updateCheckedCount();
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("发送间隔") }
                SpinBox {
                    id: intervalSpin
                    from: 10
                    to: 5000
                    stepSize: 10
                    value: app.oneClickIntervalMs
                    editable: true
                    textFromValue: function(value) { return value + " ms"; }
                    valueFromText: function(text) { return parseInt(text) || 100; }
                    onValueChanged: app.oneClickIntervalMs = value
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: app.oneClickSending ? qsTr("发送中…") : qsTr("就绪")
                    color: app.oneClickSending ? "#1abc9c" : "#666666"
                }
            }

            ProgressBar {
                id: sendProgress
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, app.oneClickTotal)
                value: app.oneClickSent
            }

            Label {
                id: sendStatus
                text: app.oneClickStatus.length > 0
                      ? app.oneClickStatus
                      : qsTr("按键/开关不会被发送；间隔可调。")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("发送")
                    enabled: window.checkedCount > 0 && !app.oneClickSending && app.connected
                    onClicked: {
                        app.setOneClickIntervalMs(intervalSpin.value);
                        app.startOneClickSend();
                    }
                }
                Button {
                    text: qsTr("停止")
                    enabled: app.oneClickSending
                    onClicked: app.cancelOneClickSend()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: sendDialog.close()
                }
            }
        }
    }

    Dialog {
        id: remoteDialog
        title: qsTr("远程调参（手机局域网）")
        modal: true
        width: Math.min(window.dp(580), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        // Only refresh the text fields when the dialog opens, so a status
        // broadcast cannot overwrite what the user is typing.
        onOpened: {
            remoteNameField.text = app.remoteServerName;
            remotePortSpin.value = app.remotePort;
            remotePinField.text = app.remotePin;
        }

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            RowLayout {
                Layout.fillWidth: true

                CheckBox {
                    id: remoteEnableBox
                    text: qsTr("启用远程调参服务")
                    checked: app.remoteEnabled
                    onToggled: app.setRemoteEnabled(checked)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("在线 %1 台").arg(app.remoteClientCount)
                    color: app.remoteClientCount > 0 ? window.theme.accent
                                                     : window.theme.tip
                    font.bold: app.remoteClientCount > 0
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: window.dp(8)
                rowSpacing: window.dp(6)
                enabled: app.remoteEnabled

                Label { text: qsTr("本机名称") }
                TextField {
                    id: remoteNameField
                    Layout.fillWidth: true
                    maximumLength: 64
                    placeholderText: qsTr("手机会显示这个名称")
                    onEditingFinished: app.setRemoteServerName(text)
                }

                Label { text: qsTr("端口") }
                SpinBox {
                    id: remotePortSpin
                    from: 1024
                    to: 65535
                    editable: true
                    // Ports must not be rendered with thousands separators.
                    textFromValue: function(value) { return value.toString(); }
                    valueFromText: function(text) {
                        return parseInt(text.replace(/,/g, "")) || 47900;
                    }
                    onValueModified: app.setRemotePort(value)
                }

                Label { text: qsTr("访问码") }
                TextField {
                    id: remotePinField
                    Layout.fillWidth: true
                    maximumLength: 12
                    placeholderText: qsTr("留空表示手机可直接连接")
                    onEditingFinished: app.setRemotePin(text)
                }
            }

            Label {
                Layout.fillWidth: true
                text: app.remoteStatus
                wrapMode: Text.WordWrap
                color: app.remoteEnabled ? window.theme.text : window.theme.tip
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: window.dp(2)
                visible: app.remoteEnabled && app.remoteAddresses.length > 0

                Label {
                    text: qsTr("手机连接地址")
                    color: window.theme.tip
                }
                Label {
                    Layout.fillWidth: true
                    text: app.remoteAddresses.join("\n")
                    font.family: "monospace"
                    wrapMode: Text.WrapAnywhere
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.preferredHeight: window.dp(120)
                visible: app.remoteEnabled

                Label {
                    anchors.centerIn: parent
                    visible: app.remoteClients.length === 0
                    text: qsTr("等待手机连接…")
                    color: window.theme.tip
                }

                ListView {
                    id: remoteClientView
                    anchors.fill: parent
                    clip: true
                    spacing: window.dp(2)
                    // Index the array directly: named roles are not exposed for
                    // a QVariantList of maps, so `model.name` would be empty.
                    model: app.remoteClients.length

                    delegate: RowLayout {
                        id: remoteClientRow
                        width: remoteClientView.width
                        spacing: window.dp(6)
                        readonly property var entry: app.remoteClients[index] || ({})

                        Label {
                            text: remoteClientRow.entry.name || ""
                            font.bold: true
                        }
                        Label {
                            text: remoteClientRow.entry.address || ""
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: window.theme.tip
                        }
                        Label {
                            text: remoteClientRow.entry.since || ""
                            color: window.theme.tip
                        }
                        Button {
                            text: qsTr("断开")
                            Layout.preferredWidth: window.dp(56)
                            onClicked: app.kickRemoteClient(remoteClientRow.entry.id)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: qsTr("手机只能操作滑块数值、开关、命令按钮和一键发送，"
                               + "不能新增或修改命令。")
                    wrapMode: Text.WordWrap
                    color: window.theme.tip
                }
                Button {
                    text: qsTr("关闭")
                    onClicked: remoteDialog.close()
                }
            }
        }
    }

    Dialog {
        id: editDialog
        title: qsTr("编辑参数")
        modal: true
        width: Math.min(window.dp(660), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("选择参数") }
                ComboBox {
                    id: editCombo
                    Layout.fillWidth: true
                    model: sliderModel
                    textRole: "label"
                    onActivated: window.loadEditBinding(index)
                }
                Button {
                    text: qsTr("新增")
                    onClicked: {
                        if (app.addOneClickBinding("new", "cmd%.3f", 0, 10, 0.01, 0,
                                                   false, app.currentGroupName)) {
                            editCombo.currentIndex = sliderModel.count - 1;
                            window.loadEditBinding(editCombo.currentIndex);
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: window.dp(8)
                rowSpacing: window.dp(6)

                Label { text: qsTr("名称") }
                TextField { id: editLabel; Layout.fillWidth: true }

                Label { text: qsTr("命令模板") }
                TextField {
                    id: editTemplate
                    Layout.fillWidth: true
                    placeholderText: "kp1%.3f"
                }

                Label { text: qsTr("最小值") }
                TextField { id: editMin; Layout.fillWidth: true }

                Label { text: qsTr("最大值") }
                TextField { id: editMax; Layout.fillWidth: true }

                Label { text: qsTr("步长") }
                TextField { id: editStep; Layout.fillWidth: true }

                Label { text: qsTr("当前值") }
                TextField { id: editValue; Layout.fillWidth: true }

                Label { text: qsTr("命令组") }
                ComboBox {
                    id: editGroupCombo
                    Layout.fillWidth: true
                    model: app.commandGroups
                }
            }

            CheckBox {
                id: editInclude
                text: qsTr("参与一键发送数据")
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("保存")
                    enabled: window.editBindingId !== 0
                    onClicked: {
                        if (app.updateOneClickBinding(
                                window.editBindingId,
                                editLabel.text,
                                editTemplate.text,
                                parseFloat(editMin.text) || 0,
                                parseFloat(editMax.text) || 1,
                                parseFloat(editStep.text) || 0.01,
                                parseFloat(editValue.text) || 0,
                                editInclude.checked,
                                editGroupCombo.currentText)) {
                            editDialog.close();
                        }
                    }
                }
                Button {
                    text: qsTr("删除")
                    enabled: window.editBindingId !== 0
                    onClicked: {
                        app.removeOneClickBinding(window.editBindingId);
                        editDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: editDialog.close()
                }
            }
        }
    }

    Dialog {
        id: channelDialog
        title: qsTr("通道设置")
        modal: true
        width: Math.min(window.dp(820), Screen.width - 40)
        height: Math.min(window.dp(640), Screen.height - 60)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: Item {
            ColumnLayout {
                id: channelDialogLayout
                anchors.fill: parent
                spacing: window.dp(8)

            Label {
                text: qsTr("通道数: %1。颜色支持 #RRGGBB；比例为乘数，偏移为加数。")
                    .arg(app.channelCount)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: channelListView
                    anchors.fill: parent
                    model: channelModel
                    clip: true
                    spacing: window.dp(4)

                    delegate: ColumnLayout {
                        width: channelListView.width
                        spacing: window.dp(3)
                        // Capture channel identity/colour: the palette Repeater
                        // below shadows `index` and `model` with its own.
                        property int channelIndex: index
                        property string channelColor: "" + model.color

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: window.dp(6)

                            CheckBox {
                                text: "CH" + index
                                checked: model.visible
                                onToggled: app.setChannelVisible(index, checked)
                            }

                            Rectangle {
                                id: colorSwatch
                                width: window.dp(22)
                                height: window.dp(22)
                                radius: window.dp(3)
                                color: model.color
                                border.color: window.theme.separator
                                border.width: 1

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        paletteOverlay.channelIndex = channelIndex;
                                        paletteOverlay.currentColor = channelColor;
                                        const p = colorSwatch.mapToItem(
                                            channelDialog.contentItem,
                                            0, colorSwatch.height + 2);
                                        paletteOverlay.x = Math.max(4, Math.min(
                                            p.x,
                                            channelDialog.contentItem.width
                                                - paletteOverlay.width - 4));
                                        paletteOverlay.y = Math.max(4, Math.min(
                                            p.y,
                                            channelDialog.contentItem.height
                                                - paletteOverlay.height - 4));
                                        paletteOverlay.visible = true;
                                    }
                                }

                            }

                            TextField {
                                Layout.preferredWidth: window.dp(96)
                                text: model.color
                                onEditingFinished: app.setChannelColor(index, text)
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: model.name
                                color: window.theme.tip
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: window.dp(6)

                            Label { text: qsTr("比例") }
                            TextField {
                                Layout.preferredWidth: window.dp(66)
                                text: model.scale
                                onEditingFinished: {
                                    const value = parseFloat(text);
                                    app.setChannelScale(index, isNaN(value) ? 1.0 : value);
                                }
                            }

                            Label { text: qsTr("偏移") }
                            TextField {
                                Layout.preferredWidth: window.dp(66)
                                text: model.offset
                                onEditingFinished: {
                                    const value = parseFloat(text);
                                    app.setChannelOffset(index, isNaN(value) ? 0.0 : value);
                                }
                            }

                            Label { text: qsTr("小数") }
                            SpinBox {
                                from: 0
                                to: 9
                                value: model.decimals
                                onValueChanged: app.setChannelDecimals(index, value)
                            }

                            CheckBox {
                                text: qsTr("自动量程")
                                checked: model.autoScale
                                onToggled: app.setChannelAutoScale(index, checked)
                            }

                            Label { text: qsTr("最小") }
                            TextField {
                                Layout.preferredWidth: window.dp(62)
                                text: model.rangeMin
                                enabled: !model.autoScale
                                onEditingFinished: {
                                    const v = parseFloat(text);
                                    app.setChannelRange(index,
                                                        isNaN(v) ? model.rangeMin : v,
                                                        model.rangeMax);
                                }
                            }
                            Label { text: qsTr("最大") }
                            TextField {
                                Layout.preferredWidth: window.dp(62)
                                text: model.rangeMax
                                enabled: !model.autoScale
                                onEditingFinished: {
                                    const v = parseFloat(text);
                                    app.setChannelRange(index,
                                                        model.rangeMin,
                                                        isNaN(v) ? model.rangeMax : v);
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: window.theme.separator
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: channelDialog.close()
                }
            }
            }

            // In-dialog floating palette. A Popup would be blocked by the
            // modal channel dialog, so this is a plain overlay item.
            Rectangle {
                id: paletteOverlay
                visible: false
                z: 100
                width: paletteGrid.implicitWidth + window.dp(16)
                height: paletteGrid.implicitHeight + window.dp(16)
                radius: window.dp(4)
                color: window.theme.panel
                border.color: window.theme.separator
                property int channelIndex: -1
                property string currentColor: ""

                Grid {
                    id: paletteGrid
                    anchors.centerIn: parent
                    columns: 6
                    spacing: window.dp(6)

                    Repeater {
                        model: ["#2089DE", "#F44336", "#8BC34A", "#05BDD5",
                                "#4A5BB9", "#FFD968", "#64BFB6", "#EA2F6F",
                                "#FF9800", "#9C27B0", "#607D8B", "#FFFFFF"]
                        Rectangle {
                            required property var modelData
                            width: window.dp(22)
                            height: window.dp(22)
                            radius: window.dp(3)
                            color: modelData
                            border.width:
                                paletteOverlay.currentColor.toUpperCase() === modelData
                                ? 2 : 1
                            border.color:
                                paletteOverlay.currentColor.toUpperCase() === modelData
                                ? window.theme.text
                                : window.theme.separator
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    app.setChannelColor(paletteOverlay.channelIndex,
                                                        modelData);
                                    paletteOverlay.visible = false;
                                }
                            }
                        }
                    }
                }
            }

            MouseArea {
                z: 99
                anchors.fill: parent
                visible: paletteOverlay.visible
                onClicked: paletteOverlay.visible = false
            }
        }
    }

    Dialog {
        id: yRangeDialog
        title: qsTr("Y 轴范围")
        modal: true
        width: Math.min(window.dp(500), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        onOpened: {
            yStartField.text = timePlot.yMinVisible.toFixed(3);
            ySpanField.text = (timePlot.yMaxVisible - timePlot.yMinVisible).toFixed(3);
        }

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("起点") }
                TextField {
                    id: yStartField
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("长度") }
                TextField {
                    id: ySpanField
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: window.theme.tip
                text: qsTr("应用后切换为手动 Y 轴；滚轮或拖动也会自动切换为手动范围。")
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("应用")
                    onClicked: {
                        const start = parseFloat(yStartField.text);
                        const span = parseFloat(ySpanField.text);
                        if (!isNaN(start) && !isNaN(span) && span > 0) {
                            timePlot.setYRange(start, span);
                            yRangeDialog.close();
                        }
                    }
                }
                Button {
                    text: qsTr("自动")
                    onClicked: {
                        timePlot.resetY();
                        yRangeDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: yRangeDialog.close()
                }
            }
        }
    }

    Dialog {
        id: helpDialog
        title: qsTr("波形操作说明")
        modal: true
        width: Math.min(window.dp(700), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            Label {
                font.bold: true
                text: qsTr("鼠标操作")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("左键拖动：同时平移 X 轴和 Y 轴（查看历史/上下移动）。\n左键单击：不改变视图。\n双击：重置为自动跟随 + 自适应纵轴。")
            }

            Label {
                font.bold: true
                text: qsTr("中键、右键与滚轮")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("中键拖动：框选一块区域放大；过小的框会被忽略，避免一次缩放过头；中键单击恢复自动 Y。\n右键拖动：左右拖动缩放 X 轴，上下拖动缩放 Y 轴。\n右键单击：打开快捷菜单。\n滚轮：以鼠标位置为中心同时缩放 X/Y，连续无级。\n鼠标悬停：显示竖向参考线和各通道数值。\n游标：点击工具栏“游标”后，Shift+左键放置/拖动 A，Ctrl+左键放置/拖动 B，也可直接拖动 A/B 竖线；左上角显示 A/B 时间与各通道差值 Δ。游标模式会暂停实时滚动，点击“跟随”或双击恢复。")
            }

            Label {
                font.bold: true
                text: qsTr("视图状态")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("滚轮或拖动后视图会冻结在当前位置（实时刷新停止），数据继续在后台接收。\n点击「跟随」回到最新数据；「全览」显示全部数据；「自适应Y」按数据自动调整纵轴；「Y轴范围」可手动输入起点和长度。")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: window.theme.tip
                text: qsTr("所有缩放都是连续的，没有固定档位。绘图使用场景图批量绘制，避免整窗纹理上传。")
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: helpDialog.close()
                }
            }
        }
    }

    Dialog {
        id: triggerDialog
        title: qsTr("波形触发")
        modal: true
        width: Math.min(window.dp(480), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        onOpened: {
            triggerChannelCombo.currentIndex =
                Math.max(0, Math.min(app.triggerChannel,
                                     Math.max(0, app.channelCount - 1)));
            triggerLevelField.text = app.triggerLevel.toString();
            triggerEdgeCombo.currentIndex = app.triggerEdge;
        }

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("在选定通道上等待电平穿越。触发后继续采集约 0.3 秒，然后冻结视图并在触发位置画一条橙色竖线。点击“跟随”或双击波形可恢复实时滚动。")
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("通道") }
                ComboBox {
                    id: triggerChannelCombo
                    Layout.fillWidth: true
                    model: {
                        const n = Math.max(1, app.channelCount);
                        const names = [];
                        for (let i = 0; i < n; ++i)
                            names.push("CH" + i);
                        return names;
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("电平") }
                TextField {
                    id: triggerLevelField
                    Layout.fillWidth: true
                    validator: DoubleValidator {}
                    placeholderText: "0.0"
                    onEditingFinished: {
                        const value = parseFloat(text);
                        if (!isNaN(value))
                            app.triggerLevel = value;
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("边沿") }
                ComboBox {
                    id: triggerEdgeCombo
                    Layout.fillWidth: true
                    model: [qsTr("上升沿"), qsTr("下降沿")]
                }
            }

            Label {
                Layout.fillWidth: true
                text: app.triggerStatus
                color: app.triggerReady ? window.theme.accent : window.theme.text
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("开始触发")
                    onClicked: {
                        app.triggerChannel = triggerChannelCombo.currentIndex;
                        app.triggerEdge = triggerEdgeCombo.currentIndex;
                        const value = parseFloat(triggerLevelField.text);
                        if (!isNaN(value))
                            app.triggerLevel = value;
                        app.armTrigger();
                    }
                }
                Button {
                    text: qsTr("清除")
                    onClicked: app.clearTrigger()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: triggerDialog.close()
                }
            }
        }
    }

    Dialog {
        id: replayDialog
        title: qsTr("CSV 录波回放")
        modal: true
        width: Math.min(window.dp(480), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        onOpened: replayRateField.text = app.replayRate.toString()

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("用“更多 → 导入CSV录波…”载入导出格式（index,CH0,CH1,…）。载入后先显示全部数据；“播放”从头按设定速率逐帧回放。回放前请断开连接。")
            }

            Label {
                Layout.fillWidth: true
                text: app.hasReplayRecording
                      ? qsTr("录波：%1 行 · 已播放 %2 行")
                            .arg(app.replayRows).arg(app.replayPosition)
                      : qsTr("尚未导入录波")
                color: window.theme.tip
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("速率") }
                TextField {
                    id: replayRateField
                    Layout.fillWidth: true
                    validator: DoubleValidator { bottom: 1; top: 1000000 }
                    onEditingFinished: {
                        const value = parseFloat(text);
                        if (!isNaN(value))
                            app.replayRate = value;
                    }
                }
                Label { text: qsTr("行/秒") }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("播放")
                    enabled: app.hasReplayRecording && !app.replayActive
                    onClicked: app.startReplay()
                }
                Button {
                    text: qsTr("暂停")
                    enabled: app.replayActive
                    onClicked: app.stopReplay()
                }
                Button {
                    text: qsTr("回到开头")
                    enabled: app.hasReplayRecording
                    onClicked: app.resetReplay()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: replayDialog.close()
                }
            }
        }
    }

    Dialog {
        id: filterDialog
        title: qsTr("日志过滤")
        modal: true
        width: Math.min(window.dp(560), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        onOpened: {
            filterField.text = app.receiveFilter;
            filterRegexCheck.checked = app.receiveFilterRegex;
            filterDirectionCombo.currentIndex = app.receiveFilterDirection;
            filterTxEcho.checked = app.receiveTxEcho;
        }

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("只显示匹配以下文字的行（不区分大小写）。可切换为正则表达式；文本与 ANSI 日志生效，HEX 摘要模式不受文本过滤影响。")
            }

            TextField {
                id: filterField
                Layout.fillWidth: true
                placeholderText: qsTr("例如 error、CAN、motor 或 ERR|FAIL")
                onAccepted: window.applyLogFilter()
            }

            CheckBox {
                id: filterRegexCheck
                text: qsTr("正则表达式")
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("方向") }
                ComboBox {
                    id: filterDirectionCombo
                    Layout.fillWidth: true
                    model: [qsTr("接收 + 发送"), qsTr("仅接收"), qsTr("仅发送")]
                }
                CheckBox {
                    id: filterTxEcho
                    text: qsTr("日志显示发送")
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: window.theme.tip
                font.pixelSize: window.fontPx(10)
                text: qsTr("日志显示发送会把文本/HEX/动作/文件发送记录为 TX 行；滑块与一键发送不会刷屏。选择“仅发送”时会自动显示。")
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("应用")
                    onClicked: window.applyLogFilter()
                }
                Button {
                    text: qsTr("清除过滤")
                    onClicked: {
                        app.receiveFilter = "";
                        app.receiveFilterRegex = false;
                        app.receiveFilterDirection = 0;
                        app.receiveTxEcho = false;
                        filterField.text = "";
                        filterRegexCheck.checked = false;
                        filterDirectionCombo.currentIndex = 0;
                        filterTxEcho.checked = false;
                        filterDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: filterDialog.close()
                }
            }
        }
    }

    Dialog {
        id: groupDialog
        title: qsTr("命令组")
        modal: true
        width: Math.min(window.dp(600), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        onOpened: {
            groupManageCombo.currentIndex = app.currentGroup;
            groupNameField.text = app.currentGroupName;
        }

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("每个命令组可以对应一个项目。组名支持用 / 分层，例如“底盘/PID/速度环”；父组会自动创建。切换组后，右侧参数滑块和命令/开关只显示该组，一键发送数据也只发送当前组。")
            }

            ComboBox {
                id: groupManageCombo
                Layout.fillWidth: true
                model: app.commandGroups
                currentIndex: app.currentGroup
                onActivated: {
                    groupNameField.text = currentText;
                    app.setCurrentGroup(index);
                }
            }

            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: groupNameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("输入组名，如 底盘/PID")
                }
                Button {
                    text: qsTr("新建")
                    onClicked: {
                        if (app.addCommandGroup(groupNameField.text)) {
                            groupManageCombo.currentIndex =
                                app.commandGroups.length - 1;
                            app.setCurrentGroup(groupManageCombo.currentIndex);
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("重命名为上面的名称")
                    onClicked: app.renameCommandGroup(
                                   groupManageCombo.currentIndex,
                                   groupNameField.text)
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("删除当前组")
                    enabled: app.commandGroups.length > 1
                    onClicked: app.removeCommandGroup(groupManageCombo.currentIndex)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("批量移动") }
                ComboBox {
                    id: moveTargetCombo
                    Layout.fillWidth: true
                    model: app.commandGroups
                    currentIndex: app.currentGroup
                }
                Button {
                    text: qsTr("本组全部移入")
                    enabled: moveTargetCombo.currentIndex !== app.currentGroup
                    onClicked: app.moveCurrentGroupContents(
                                   moveTargetCombo.currentIndex)
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: window.theme.tip
                text: qsTr("删除父组会同时删除其子组；组内控件会移动到第一个组，不会被删除。组名最多 128 个字符。")
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: groupDialog.close()
                }
            }
        }
    }

    Dialog {
        id: actionDialog
        title: qsTr("编辑命令 / 开关")
        modal: true
        width: Math.min(window.dp(660), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: window.dp(8)

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("选择动作") }
                ComboBox {
                    id: actionEditCombo
                    Layout.fillWidth: true
                    model: actionModel
                    textRole: "label"
                    onActivated: window.loadActionEdit(index)
                }
                Button {
                    text: qsTr("新增")
                    onClicked: {
                        if (app.addActionBinding(qsTr("新动作"), "start", "stop", 1,
                                                 app.currentGroupName)) {
                            actionEditCombo.currentIndex = actionModel.count - 1;
                            window.loadActionEdit(actionEditCombo.currentIndex);
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: window.dp(8)
                rowSpacing: window.dp(6)

                Label { text: qsTr("名称") }
                TextField { id: actionLabel; Layout.fillWidth: true }

                Label { text: qsTr("类型") }
                ComboBox {
                    id: actionKindCombo
                    Layout.fillWidth: true
                    model: [qsTr("按钮（发送一次）"), qsTr("开关（开/关各一条命令）")]
                }

                Label { text: qsTr("开启命令") }
                TextField {
                    id: actionOn
                    Layout.fillWidth: true
                    placeholderText: qsTr("无参数命令，例如 start")
                }

                Label { text: qsTr("关闭命令") }
                TextField {
                    id: actionOff
                    Layout.fillWidth: true
                    placeholderText: qsTr("开关专用，可留空；例如 stop")
                }

                Label { text: qsTr("命令组") }
                ComboBox {
                    id: actionGroupCombo
                    Layout.fillWidth: true
                    model: app.commandGroups
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: window.theme.tip
                text: qsTr("动作命令不支持 %f/%d 参数；带参数的滑块请使用上面的参数编辑器。动作不会参与一键发送数据。")
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("保存")
                    enabled: window.actionEditId !== 0
                    onClicked: {
                        if (app.updateActionBinding(window.actionEditId,
                                                    actionLabel.text,
                                                    actionOn.text,
                                                    actionOff.text,
                                                    actionKindCombo.currentIndex,
                                                    actionGroupCombo.currentText))
                            actionDialog.close();
                    }
                }
                Button {
                    text: qsTr("删除")
                    enabled: window.actionEditId !== 0
                    onClicked: {
                        app.removeActionBinding(window.actionEditId);
                        actionDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: actionDialog.close()
                }
            }
        }
    }

    Dialog {
        id: uiSettingsDialog
        title: qsTr("界面设置")
        modal: true
        width: Math.min(window.dp(720), Screen.width - 40)
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: window.dp(10)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("预设会同时调整控件密度与字体；下面的滑杆可独立微调。文字始终按系统 DPI 清晰渲染，修改立即生效。")
            }

            Label {
                Layout.fillWidth: true
                color: window.theme.tip
                text: qsTr("屏幕可用逻辑尺寸 %1×%2 · 系统缩放 %3×")
                          .arg(Screen.width).arg(Screen.height)
                          .arg(Screen.devicePixelRatio.toFixed(2))
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("主题") }
                ComboBox {
                    id: themeCombo
                    Layout.fillWidth: true
                    model: [qsTr("浅色"), qsTr("深色"), qsTr("护眼")]
                    currentIndex: app.uiTheme
                    onActivated: app.uiTheme = currentIndex
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("紧凑")
                    onClicked: app.setUiPreset(0)
                }
                Button {
                    text: qsTr("标准")
                    onClicked: app.setUiPreset(1)
                }
                Button {
                    text: qsTr("舒适")
                    onClicked: app.setUiPreset(2)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: app.uiPreset >= 0
                          ? [qsTr("紧凑"), qsTr("标准"), qsTr("舒适")][app.uiPreset]
                          : qsTr("自定义")
                    font.bold: true
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: window.dp(8)

                Label { text: qsTr("UI 密度") }
                Slider {
                    id: densitySlider
                    Layout.fillWidth: true
                    from: 0.60
                    to: 1.40
                    stepSize: 0.01
                    value: app.uiDensity
                    onMoved: app.uiDensity = value
                }
                Label {
                    Layout.preferredWidth: window.dp(44)
                    text: app.uiDensity.toFixed(2)
                }

                Label { text: qsTr("字体缩放") }
                Slider {
                    id: fontSlider
                    Layout.fillWidth: true
                    from: 0.60
                    to: 1.40
                    stepSize: 0.01
                    value: app.uiFontScale
                    onMoved: app.uiFontScale = value
                }
                Label {
                    Layout.preferredWidth: window.dp(44)
                    text: app.uiFontScale.toFixed(2)
                }
            }

            Frame {
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: window.dp(6)

                    Label {
                        text: qsTr("预览")
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button { text: qsTr("按钮") }
                        CheckBox { text: qsTr("开关") }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: 0.6
                        }
                    }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("输入框示例")
                    }
                    Label {
                        text: qsTr("数值示例 12.345 · 当前字体 %1 px")
                                  .arg(window.fontPx(13))
                        color: window.theme.text
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("恢复标准")
                    onClicked: app.setUiPreset(1)
                }
                Button {
                    text: qsTr("窗口恢复默认大小")
                    onClicked: window.resetWindowSize()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("关闭")
                    onClicked: uiSettingsDialog.close()
                }
            }
        }
    }

}
