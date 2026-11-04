#include "app/AppController.h"
#include "app/WindowGeometry.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>

#include <algorithm>
#include <limits>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using vofa::app::AppController;

class TestAppController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void uiPresetsAreCallableByName();
    void oneClickSendsRealTemplatesOverTcp();
    void oneClickWhileDisconnectedFailsFast();
    void oneClickWithoutPeerIsRefused();
    void sliderValueSendsLiveCommand();
    void sendFileTransmitsFileContents();
    void receiveHexAndTimestampModes();
    void receiveAnsiAndLineFilter();
    void waveformSnapshotReturnsMinMax();
    void waveformWindowLimitsSnapshot();
    void demoTransportProducesFrames();
    void channelFixedRangeControlsScale();
    void waveformAnalysisModes();
    void waveformContinuousZoom();
    void autoReconnectAfterPeerClose();
    void commandGroupsPartitionBindings();
    void legacyImportCreatesBindings();
    void windowClampFitsAvailableGeometry();
    void serialBreakResetsOnClose();
    void sendFilterNarrowsRecentSends();
    void undoRedoRestoresCommandEdits();
    void nestedCommandGroups();
    void legacyImportPreservesGroupHierarchy();
    void receiveFilterRegexAndDirection();
    void waveformTriggerCapturesCrossing();
    void csvImportAndReplay();
    void typedBindingValueCanExceedSliderRange();
    void tcp20mbBurstNoDrop();
    void imageDecodeIsAsynchronous();
    void image1080pDecodeBenchmark();
};

void TestAppController::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("VOFA2Test"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();
}

void TestAppController::cleanupTestCase()
{
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();
}

void TestAppController::uiPresetsAreCallableByName()
{
    // QML calls app.setUiPreset(...) as a method. Property WRITE accessors are
    // not callable by name unless Q_INVOKABLE, which was a real bug: the
    // preset buttons silently did nothing while the density sliders worked.
    AppController controller;
    QVERIFY(controller.metaObject()->indexOfMethod("setUiPreset(int)") >= 0);
    QVERIFY(controller.metaObject()->indexOfMethod("setUiDensity(double)") >= 0);
    QVERIFY(controller.metaObject()->indexOfMethod("setUiFontScale(double)") >= 0);

    QVERIFY(QMetaObject::invokeMethod(&controller, "setUiPreset", Q_ARG(int, 0)));
    QCOMPARE(controller.uiPreset(), 0);
    QVERIFY(qFuzzyCompare(controller.uiDensity(), 0.70));
    QVERIFY(qFuzzyCompare(controller.uiFontScale(), 0.75));

    QVERIFY(QMetaObject::invokeMethod(&controller, "setUiPreset", Q_ARG(int, 2)));
    QCOMPARE(controller.uiPreset(), 2);
    QVERIFY(qFuzzyCompare(controller.uiDensity(), 1.25));
    QVERIFY(qFuzzyCompare(controller.uiFontScale(), 1.18));

    QVERIFY(QMetaObject::invokeMethod(&controller, "setUiDensity",
                                      Q_ARG(double, 0.93)));
    QCOMPARE(controller.uiPreset(), -1); // custom
    QVERIFY(qFuzzyCompare(controller.uiDensity(), 0.93));
}

void TestAppController::oneClickSendsRealTemplatesOverTcp()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray received;
    connect(socket.data(), &QTcpSocket::readyRead, this, [&]() {
        received += socket->readAll();
    });

    controller.setOneClickIntervalMs(50);
    controller.startOneClickSend();

    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kp10.750"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kp20.010"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kd0.100"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.oneClickSending(), 3000);

    // The default "speed" slider is excluded from one-click sending.
    QVERIFY(!received.contains("speed"));

    controller.closeTransport();
}

void TestAppController::oneClickWhileDisconnectedFailsFast()
{
    // Regression: with no transport open, startOneClickSend() used to arm the
    // run anyway. sendFailed came back synchronously, the step was still
    // counted, and the dialog stayed on "已发送 1/3" with no way forward.
    AppController controller;
    QCOMPARE(controller.connectionState(), QStringLiteral("未连接"));

    controller.setOneClickIntervalMs(10);
    controller.startOneClickSend();

    QVERIFY(!controller.oneClickSending());
    QCOMPARE(controller.oneClickSent(), 0);
    QCOMPARE(controller.oneClickTotal(), 0);
    QVERIFY(controller.oneClickStatus().contains(QStringLiteral("未连接")));

    // Nothing may resume the run behind our back.
    QTest::qWait(150);
    QVERIFY(!controller.oneClickSending());
    QCOMPARE(controller.oneClickSent(), 0);
}

void TestAppController::oneClickWithoutPeerIsRefused()
{
    // A listening TCP server is "connected" but has no peer to write to.
    AppController controller;
    controller.openTcpServer(0);
    QTRY_VERIFY_WITH_TIMEOUT(controller.connectionState().startsWith(QStringLiteral("监听中")),
                             3000);

    controller.setOneClickIntervalMs(10);
    controller.startOneClickSend();

    QVERIFY(!controller.oneClickSending());
    QCOMPARE(controller.oneClickSent(), 0);
    QCOMPARE(controller.oneClickTotal(), 0);
    QVERIFY(controller.oneClickStatus().contains(QStringLiteral("客户端")));

    controller.closeTransport();
}

void TestAppController::sliderValueSendsLiveCommand()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray received;
    connect(socket.data(), &QTcpSocket::readyRead, this, [&]() {
        received += socket->readAll();
    });

    // Live tuning: moving a slider sends the bound command.
    controller.setBindingValue(100, 2.5);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kp12.500"), 3000);

    // With send-on-move disabled, only the release (commit) sends.
    controller.setSendOnMove(false);
    received.clear();
    controller.setBindingValue(100, 3.25);
    QTest::qWait(150);
    QVERIFY(!received.contains("kp13.250"));

    controller.commitBindingValue(100);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kp13.250"), 3000);

    controller.closeTransport();
}

void TestAppController::sendFileTransmitsFileContents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("payload.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("FILE-PAYLOAD-123");
    file.close();

    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray received;
    connect(socket.data(), &QTcpSocket::readyRead, this, [&]() {
        received += socket->readAll();
    });

    const auto message = controller.sendFile(path);
    QVERIFY(message.contains(QStringLiteral("已发送")));
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("FILE-PAYLOAD-123"), 3000);

    QVERIFY(controller.sendFile(dir.filePath(QStringLiteral("missing.bin")))
                .contains(QStringLiteral("无法读取")));

    controller.closeTransport();
}

void TestAppController::receiveAnsiAndLineFilter()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("RawData"));
    controller.setReceiveAnsi(true);
    controller.setReceiveFilter(QStringLiteral("ERR"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    socket->write("\x1b[31mERR red line\x1b[0m\nOK skipped\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("color:#e06c75")), 3000);
    QVERIFY(controller.receiveText().contains(QStringLiteral("ERR")));
    QVERIFY(!controller.receiveText().contains(QStringLiteral("OK")));

    controller.setReceiveAnsi(false);
    controller.setReceiveFilter(QString());
    socket->write("plain after\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("plain after")), 3000);

    controller.closeTransport();
}

void TestAppController::receiveHexAndTimestampModes()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("RawData"));
    controller.setReceiveHex(true);
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    socket->write("AB");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(controller.receiveText().contains(QStringLiteral("41 42")),
                             3000);

    // Switching back shows the decoded text buffer, not the hex dump.
    controller.setReceiveHex(false);
    socket->write("hello");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(controller.receiveText().contains(QStringLiteral("hello")),
                             3000);

    controller.clearReceive();
    controller.setReceiveTimestamps(true);
    socket->write("x");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(controller.receiveText().contains(QLatin1Char('[')), 3000);

    // Encoding switching is persisted and does not crash on arbitrary bytes.
    controller.setReceiveEncoding(1);
    QCOMPARE(controller.receiveEncoding(), 1);
    socket->write(QByteArray("\xC4\xE3\xBA\xC3", 4)); // GBK "你好"
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.receiveText().isEmpty(), 3000);

    controller.closeTransport();
}

void TestAppController::waveformSnapshotReturnsMinMax()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("JustFloat"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    auto makeFrame = [](float a, float b, float c) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << a << b << c;
        bytes.append("\x00\x00\x80\x7f", 4);
        return bytes;
    };

    socket->write(makeFrame(1.0f, 2.0f, 3.0f));
    socket->write(makeFrame(5.0f, 6.0f, 7.0f));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 3, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.waveformSnapshot(0, 4).size(), 8, 3000);

    const auto snapshot = controller.waveformSnapshot(0, 4);
    bool sawExpectedValue = false;
    for (const auto& value : snapshot) {
        const auto number = value.toDouble();
        QVERIFY(number >= 1.0);
        QVERIFY(number <= 7.0);
        if (qFuzzyCompare(number, 1.0) || qFuzzyCompare(number, 7.0))
            sawExpectedValue = true;
    }
    QVERIFY(sawExpectedValue);

    controller.closeTransport();
}

void TestAppController::waveformWindowLimitsSnapshot()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("JustFloat"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    auto makeFrame = [](float value) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << value;
        bytes.append("\x00\x00\x80\x7f", 4);
        return bytes;
    };

    for (int i = 0; i < 104; ++i)
        socket->write(makeFrame(static_cast<float>(i)));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 1, 3000);
    controller.setWaveformWindowSamples(10);
    QCOMPARE(controller.waveformWindowSamples(), 10);

    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            const auto snapshot = controller.waveformSnapshot(0, 2);
            if (snapshot.size() != 4)
                return false;
            for (const auto& value : snapshot) {
                const auto number = value.toDouble();
                if (number < 94.0 || number > 103.0)
                    return false;
            }
            return true;
        }(),
        3000);

    // Panning back shows an older window and pauses live repaints.
    controller.panWaveform(20);
    QCOMPARE(controller.waveformPanSamples(), 20);
    QVERIFY(controller.waveformPaused());
    QCOMPARE(controller.waveformVisibleSampleCount(), 10);
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            const auto snapshot = controller.waveformSnapshot(0, 2);
            if (snapshot.size() != 4)
                return false;
            for (const auto& value : snapshot) {
                const auto number = value.toDouble();
                if (number < 74.0 || number > 83.0)
                    return false;
            }
            return true;
        }(),
        3000);

    controller.resetWaveformPan();
    QCOMPARE(controller.waveformPanSamples(), 0);
    QVERIFY(!controller.waveformPaused());
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            const auto snapshot = controller.waveformSnapshot(0, 2);
            if (snapshot.size() != 4)
                return false;
            for (const auto& value : snapshot) {
                const auto number = value.toDouble();
                if (number < 94.0 || number > 103.0)
                    return false;
            }
            return true;
        }(),
        3000);

    controller.closeTransport();
}

void TestAppController::demoTransportProducesFrames()
{
    AppController controller;
    controller.openDemo();
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 4, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.framesPerSecond() > 50.0, 4000);
    controller.closeTransport();
}

void TestAppController::channelFixedRangeControlsScale()
{
    AppController controller;
    controller.openDemo();
    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 4, 3000);

    const auto settings = controller.channelSettings();
    QVERIFY(!settings.isEmpty());
    QVERIFY(settings.first().toMap().value(QStringLiteral("autoScale")).toBool());

    controller.setChannelAutoScale(0, false);
    controller.setChannelRange(0, -10.0, 10.0);
    float low = 0.0f;
    float high = 0.0f;
    QVERIFY(controller.channelFixedRange(0, low, high));
    QCOMPARE(low, -10.0f);
    QCOMPARE(high, 10.0f);

    const auto updated = controller.channelSettings().first().toMap();
    QCOMPARE(updated.value(QStringLiteral("autoScale")).toBool(), false);
    QCOMPARE(updated.value(QStringLiteral("rangeMin")).toDouble(), -10.0);
    QCOMPARE(updated.value(QStringLiteral("rangeMax")).toDouble(), 10.0);

    controller.setChannelAutoScale(0, true);
    QVERIFY(!controller.channelFixedRange(0, low, high));
    controller.closeTransport();
}

void TestAppController::waveformContinuousZoom()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("JustFloat"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    auto makeFrame = [](float value) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << value;
        bytes.append("\x00\x00\x80\x7f", 4);
        return bytes;
    };
    for (int i = 0; i < 104; ++i)
        socket->write(makeFrame(static_cast<float>(i)));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !controller.latestValues().isEmpty()
            && controller.latestValues().first().toDouble() > 102.0,
        3000);

    controller.setWaveformWindowSamples(40);
    QCOMPARE(controller.waveformVisibleSampleCount(), 40);

    // Continuous zoom around the centre: 40 -> 20 samples, anchored at
    // sample 84, so the view ends at 94 and pans 10 samples back.
    controller.zoomWaveformAt(0.5, 0.5);
    QCOMPARE(controller.waveformWindowSamples(), 20);
    QCOMPARE(controller.waveformPanSamples(), 10);
    QVERIFY(controller.waveformPaused());

    controller.resetWaveformPan();
    controller.setWaveformWindowSamples(40);
    controller.zoomWaveformToFractions(0.25, 0.75);
    QCOMPARE(controller.waveformWindowSamples(), 20);
    QCOMPARE(controller.waveformPanSamples(), 10);

    controller.closeTransport();
}

void TestAppController::waveformAnalysisModes()
{
    AppController controller;
    controller.openDemo();
    QTRY_VERIFY_WITH_TIMEOUT(controller.channelCount() == 4, 3000);
    QTest::qWait(1200);

    std::vector<float> spectrum;
    controller.buildSpectrum(0, 64, spectrum);
    QCOMPARE(spectrum.size(), 64u);
    float spectrumSum = 0.0f;
    for (const float value : spectrum)
        spectrumSum += value;
    QVERIFY(spectrumSum > 0.0f);

    float low = 0.0f;
    float high = 0.0f;
    std::vector<float> histogram;
    controller.buildHistogram(0, 32, low, high, histogram);
    QCOMPARE(histogram.size(), 32u);
    QVERIFY(high > low);
    float histogramSum = 0.0f;
    for (const float value : histogram)
        histogramSum += value;
    QVERIFY(histogramSum > 0.0f);

    std::vector<float> xy;
    controller.buildXY(0, 1, 256, xy);
    QVERIFY(xy.size() >= 4u);
    QCOMPARE(xy.size() % 2, 0u);

    controller.closeTransport();
}

void TestAppController::autoReconnectAfterPeerClose()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setAutoReconnect(true);
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> first(listener.nextPendingConnection());
    QVERIFY(!first.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    // Simulate the MCU closing the socket; the controller should reconnect.
    first->abort();
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 6000);
    QScopedPointer<QTcpSocket> second(listener.nextPendingConnection());
    QVERIFY(!second.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    // An explicit user close must disable reconnection.
    controller.closeTransport();
    QTest::qWait(2500);
    QVERIFY(!listener.hasPendingConnections());
}

void TestAppController::commandGroupsPartitionBindings()
{
    AppController controller;
    QVERIFY(controller.commandGroups().contains(QStringLiteral("默认")));
    const int defaultGroup = controller.commandGroups().indexOf(QStringLiteral("默认"));
    controller.setCurrentGroup(defaultGroup);
    const int defaultCount = controller.oneClickBindings().size();

    QVERIFY(controller.addCommandGroup(QStringLiteral("项目B")));
    const int projectGroup = controller.commandGroups().indexOf(QStringLiteral("项目B"));
    QVERIFY(projectGroup > 0);

    // New items go to the requested group and stay hidden while another
    // group is selected.
    QVERIFY(controller.addOneClickBinding(QStringLiteral("b1"),
                                          QStringLiteral("b1%.3f"),
                                          0.0, 10.0, 0.01, 1.0, true,
                                          QStringLiteral("项目B")));
    QCOMPARE(controller.oneClickBindings().size(), defaultCount);

    controller.setCurrentGroup(projectGroup);
    QCOMPARE(controller.currentGroupName(), QStringLiteral("项目B"));
    QCOMPARE(controller.oneClickBindings().size(), 1);
    QCOMPARE(controller.oneClickBindings().first().toMap()
                 .value(QStringLiteral("label")).toString(),
             QStringLiteral("b1"));

    QVERIFY(controller.addActionBinding(QStringLiteral("brush"),
                                        QStringLiteral("brushon"),
                                        QStringLiteral("brushoff"),
                                        1, QStringLiteral("项目B")));
    QCOMPARE(controller.actionBindings().size(), 1);

    // One-click sending only touches the current group.
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray received;
    connect(socket.data(), &QTcpSocket::readyRead, this, [&]() {
        received += socket->readAll();
    });

    controller.setOneClickIntervalMs(50);
    controller.startOneClickSend();
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("b11.000"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.oneClickSending(), 3000);
    QVERIFY(!received.contains("kp1"));

    // Rename keeps the items in the group.
    QVERIFY(controller.renameCommandGroup(projectGroup, QStringLiteral("项目C")));
    QCOMPARE(controller.currentGroupName(), QStringLiteral("项目C"));
    QCOMPARE(controller.oneClickBindings().size(), 1);

    // Bulk move moves the whole current group.
    QVERIFY(controller.addCommandGroup(QStringLiteral("项目D")));
    const int projectD = controller.commandGroups().indexOf(QStringLiteral("项目D"));
    QVERIFY(controller.moveCurrentGroupContents(projectD));
    QCOMPARE(controller.currentGroupName(), QStringLiteral("项目D"));
    QCOMPARE(controller.oneClickBindings().size(), 1);
    QCOMPARE(controller.actionBindings().size(), 1);

    // Removing a group moves its items to the first group instead of
    // deleting user data.
    QVERIFY(controller.removeCommandGroup(controller.currentGroup()));
    QCOMPARE(controller.currentGroupName(), QStringLiteral("默认"));
    QCOMPARE(controller.oneClickBindings().size(), defaultCount + 1);
    QCOMPARE(controller.actionBindings().size(), 1);

    controller.closeTransport();
}

void TestAppController::windowClampFitsAvailableGeometry()
{
    const QRect available(0, 0, 1920, 1040);

    // Already inside: untouched.
    QCOMPARE(vofa::app::clampWindowToAvailable(QRect(100, 100, 800, 600), available),
             QRect(100, 100, 800, 600));

    // Hanging off the bottom-right: moved back inside.
    QCOMPARE(vofa::app::clampWindowToAvailable(QRect(1500, 900, 800, 600), available),
             QRect(1120, 440, 800, 600));

    // Partially off the top-left (after a monitor is unplugged).
    QCOMPARE(vofa::app::clampWindowToAvailable(QRect(-200, -100, 800, 600), available),
             QRect(0, 0, 800, 600));

    // Larger than the screen: shrunk and anchored to the top-left.
    QCOMPARE(vofa::app::clampWindowToAvailable(QRect(-100, -50, 3000, 2000), available),
             QRect(0, 0, 1920, 1040));

    // Invalid input must not invent geometry.
    const QRect window(10, 20, 800, 600);
    QCOMPARE(vofa::app::clampWindowToAvailable(window, QRect()), window);
}

void TestAppController::serialBreakResetsOnClose()
{
    AppController controller;
    QVERIFY(controller.metaObject()->indexOfMethod("setSerialBreak(bool)") >= 0);
    QVERIFY(!controller.serialBreak());

    controller.setSerialBreak(true);
    QVERIFY(controller.serialBreak());

    // Closing the transport must not leave a stale latched BREAK in the UI.
    controller.closeTransport();
    QVERIFY(!controller.serialBreak());
}

void TestAppController::sendFilterNarrowsRecentSends()
{
    AppController controller;
    controller.clearRecentSends(); // earlier tests share the config directory
    controller.sendText(QStringLiteral("alpha"));
    controller.sendText(QStringLiteral("beta"));
    controller.sendText(QStringLiteral("Alpha2"));
    QCOMPARE(controller.recentSends().size(), 3);

    controller.setSendFilter(QStringLiteral("alpha"));
    QCOMPARE(controller.filteredRecentSends().size(), 2);

    controller.setSendFilter(QStringLiteral("BETA"));
    const auto filtered = controller.filteredRecentSends();
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("beta"));

    controller.setSendFilter(QStringLiteral("missing"));
    QVERIFY(controller.filteredRecentSends().isEmpty());

    controller.setSendFilter(QString());
    QCOMPARE(controller.filteredRecentSends().size(), 3);
}

void TestAppController::nestedCommandGroups()
{
    AppController controller;
    const auto originalGroupName = controller.currentGroupName();

    QVERIFY(controller.addCommandGroup(QStringLiteral("treeA/treeB")));
    QVERIFY(controller.commandGroups().contains(QStringLiteral("treeA")));
    QVERIFY(controller.commandGroups().contains(QStringLiteral("treeA/treeB")));
    QVERIFY(!controller.addCommandGroup(QStringLiteral("treeA/treeB")));

    QVERIFY(controller.addOneClickBinding(QStringLiteral("treeParam"),
                                          QStringLiteral("tp%.2f"),
                                          0.0, 1.0, 0.1, 0.5, true,
                                          QStringLiteral("treeA/treeB")));
    controller.setCurrentGroup(
        controller.commandGroups().indexOf(QStringLiteral("treeA/treeB")));
    QCOMPARE(controller.oneClickBindings().size(), 1);
    QCOMPARE(controller.oneClickBindings().first().toMap()
                 .value(QStringLiteral("group")).toString(),
             QStringLiteral("treeA/treeB"));

    // Renaming a parent moves the whole subtree and its bindings.
    const int parentIndex = controller.commandGroups().indexOf(QStringLiteral("treeA"));
    QVERIFY(controller.renameCommandGroup(parentIndex, QStringLiteral("treeC")));
    QVERIFY(controller.commandGroups().contains(QStringLiteral("treeC/treeB")));
    QVERIFY(!controller.commandGroups().contains(QStringLiteral("treeA/treeB")));
    QCOMPARE(controller.oneClickBindings().size(), 1);
    QCOMPARE(controller.currentGroupName(), QStringLiteral("treeC/treeB"));

    // Deleting the parent removes the subtree and moves its contents to the
    // first surviving group.
    QVERIFY(controller.removeCommandGroup(
        controller.commandGroups().indexOf(QStringLiteral("treeC"))));
    QVERIFY(!controller.commandGroups().contains(QStringLiteral("treeC/treeB")));
    for (const auto& value : controller.oneClickBindings()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("label")).toString()
            == QStringLiteral("treeParam")) {
            controller.removeOneClickBinding(
                item.value(QStringLiteral("id")).toULongLong());
            break;
        }
    }
    controller.setCurrentGroup(
        controller.commandGroups().indexOf(originalGroupName));
}

void TestAppController::image1080pDecodeBenchmark()
{
    if (!qEnvironmentVariableIsSet("VOFA_IMAGE_BENCH"))
        QSKIP("set VOFA_IMAGE_BENCH=1 to run the 1080p decode benchmark");

    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("FireWater"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QImage image(1920, 1080, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = qRgb((x * 255) / image.width(),
                           (y * 255) / image.height(),
                           ((x + y) * 255) / (image.width() + image.height()));
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPEG", 80));
    buffer.close();

    const auto header = QByteArrayLiteral("image:0,27,1920,1080,")
        + QByteArray::number(jpeg.size()) + '\n';

    constexpr int kFrames = 30;
    QSignalSpy imageSpy(&controller, &AppController::imageUpdated);
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < kFrames; ++i) {
        socket->write(header);
        socket->write(jpeg);
        socket->flush();
        QVERIFY(imageSpy.wait(3000));
        imageSpy.clear();
    }
    const double elapsedMs = static_cast<double>(timer.elapsed());
    qInfo("1080p JPEG frames=%d encoded=%d bytes total=%.1fms per_frame=%.2fms fps=%.1f",
          kFrames, static_cast<int>(jpeg.size()), elapsedMs, elapsedMs / kFrames,
          1000.0 * kFrames / std::max(1.0, elapsedMs));

    controller.closeTransport();
}

void TestAppController::imageDecodeIsAsynchronous()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("FireWater"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QImage image(8, 6, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    buffer.close();

    const auto header = QByteArrayLiteral("image:0,28,8,6,")
        + QByteArray::number(png.size()) + '\n';
    socket->write(header);
    socket->write(png);
    socket->flush();

    // imageCount only advances after the worker thread has decoded the frame.
    QTRY_VERIFY_WITH_TIMEOUT(controller.imageCount() > 0, 3000);
    QVERIFY(controller.imageSource().startsWith(QStringLiteral("image://vofa/0")));
    QVERIFY(controller.imageInfo().contains(QStringLiteral("8×6")));

    socket->write(header);
    socket->write(png);
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(controller.imageCount() >= 2, 3000);

    controller.closeTransport();
}

void TestAppController::tcp20mbBurstNoDrop()
{
    // Heavy: only run when explicitly requested (VOFA_BURST_TEST=1).
    if (!qEnvironmentVariableIsSet("VOFA_BURST_TEST"))
        QSKIP("set VOFA_BURST_TEST=1 to run the 20 MB/s burst test");

    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("JustFloat"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << 1.0f << 2.0f << 3.0f << 4.0f;
    frame.append("\x00\x00\x80\x7f", 4);

    // 1,000,000 frames x 20 bytes = 20 MB.
    constexpr int kFrames = 1000000;
    QByteArray block;
    block.reserve(frame.size() * 1024);
    while (block.size() < 64 * 1024)
        block += frame;

    int sentFrames = 0;
    while (sentFrames < kFrames) {
        const int chunkFrames = std::min(1024, kFrames - sentFrames);
        socket->write(block.constData(), chunkFrames * frame.size());
        sentFrames += chunkFrames;
        // ~20 MB/s (10240 frames x 20 B per 10 ms) while giving the UI thread
        // a chance to drain at 60 Hz.
        if ((sentFrames % 10240) == 0)
            QTest::qWait(10);
    }
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(
        controller.dataAbsoluteEnd(), static_cast<std::uint64_t>(kFrames), 30000);
    QCOMPARE(controller.droppedFrames(), qulonglong(0));

    controller.closeTransport();
}

void TestAppController::typedBindingValueCanExceedSliderRange()
{
    AppController controller;
    QVERIFY(controller.addOneClickBinding(QStringLiteral("typedParam"),
                                          QStringLiteral("typed%.3f"),
                                          -10.0, 10.0, 0.1, 0.0, true,
                                          controller.currentGroupName()));
    const auto items = controller.oneClickBindings();
    QVERIFY(!items.isEmpty());
    const auto item = items.last().toMap();
    const auto id = item.value(QStringLiteral("id")).toULongLong();
    const auto original = item.value(QStringLiteral("value")).toDouble();
    const auto maxValue = item.value(QStringLiteral("max")).toDouble();

    const double typed = maxValue + 12.5;
    QVERIFY(controller.setBindingValueExact(id, typed));

    bool found = false;
    for (const auto& value : controller.oneClickBindings()) {
        const auto map = value.toMap();
        if (map.value(QStringLiteral("id")).toULongLong() != id)
            continue;
        found = true;
        QCOMPARE(map.value(QStringLiteral("value")).toDouble(), typed);
        break;
    }
    QVERIFY(found);

    QVERIFY(!controller.setBindingValueExact(
        id, std::numeric_limits<double>::quiet_NaN()));

    // Leave the shared test workspace as found.
    controller.setBindingValue(id, original);
    controller.commitBindingValue(id);
    QVERIFY(controller.removeOneClickBinding(id));
}

void TestAppController::csvImportAndReplay()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("rec.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("index,CH0,CH1\n0,1,2\n1,3,4\n2,5,6\n");
    file.close();

    AppController controller;
    const auto message = controller.loadCsvRecording(path);
    QVERIFY(message.contains(QStringLiteral("已载入")));
    QVERIFY(controller.hasReplayRecording());
    QCOMPARE(controller.replayRows(), 3);

    QTRY_COMPARE_WITH_TIMEOUT(controller.channelCount(), 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.latestValues().size(), 2, 3000);
    QCOMPARE(controller.latestValues().at(0).toDouble(), 5.0);
    QCOMPARE(controller.latestValues().at(1).toDouble(), 6.0);
    QCOMPARE(controller.replayPosition(), 3);

    // Play from the beginning: the timer feeds the whole 3-row recording.
    controller.setReplayRate(1000.0);
    controller.startReplay();
    QVERIFY(controller.replayActive());
    QTRY_COMPARE_WITH_TIMEOUT(controller.replayPosition(), 3, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.replayActive(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.latestValues().size(), 2, 3000);

    controller.resetReplay();
    QCOMPARE(controller.replayPosition(), 0);

    // Importing while connected is rejected.
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);
    QVERIFY(controller.loadCsvRecording(path).contains(QStringLiteral("断开")));
    controller.closeTransport();
}

void TestAppController::waveformTriggerCapturesCrossing()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("JustFloat"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    auto makeFrame = [](float value) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << value;
        bytes.append("\x00\x00\x80\x7f", 4);
        return bytes;
    };

    controller.setTriggerChannel(0);
    controller.setTriggerLevel(0.5);
    controller.setTriggerEdge(0);
    controller.armTrigger();
    QVERIFY(controller.triggerArmed());

    for (int i = 0; i < 3; ++i)
        socket->write(makeFrame(0.0f));
    socket->flush();
    QTest::qWait(100);
    QCOMPARE(controller.triggerCount(), 0);

    socket->write(makeFrame(1.0f));
    socket->write(makeFrame(0.8f));
    socket->flush();
    QTRY_COMPARE_WITH_TIMEOUT(controller.triggerCount(), 1, 3000);
    QVERIFY(controller.triggerSample() > 0);
    QVERIFY(!controller.triggerArmed());
    QTRY_VERIFY_WITH_TIMEOUT(controller.triggerReady(), 3000);
    QVERIFY(controller.triggerStatus().contains(QStringLiteral("已捕获")));

    // One-shot: further crossings do not retrigger until re-armed.
    socket->write(makeFrame(0.0f));
    socket->write(makeFrame(1.0f));
    socket->flush();
    QTest::qWait(100);
    QCOMPARE(controller.triggerCount(), 1);

    controller.clearTrigger();
    QCOMPARE(controller.triggerSample(), qulonglong(0));
    QVERIFY(!controller.triggerReady());

    controller.closeTransport();
}

void TestAppController::receiveFilterRegexAndDirection()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    AppController controller;
    controller.setProtocol(QStringLiteral("RawData"));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    controller.setReceiveFilter(QStringLiteral("^ERR \\d+"));
    controller.setReceiveFilterRegex(true);
    socket->write("ERR 42\nOK 7\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("ERR 42")), 3000);
    QVERIFY(!controller.receiveText().contains(QStringLiteral("OK 7")));

    // An invalid regex falls back to a literal contains match.
    controller.setReceiveFilter(QStringLiteral("["));
    socket->write("bracket [ here\nplain\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("bracket [ here")), 3000);
    QVERIFY(!controller.receiveText().contains(QStringLiteral("plain")));

    controller.setReceiveFilter(QString());
    controller.setReceiveFilterRegex(false);
    controller.clearReceive();

    // Send-only direction echoes explicit sends but hides incoming data.
    controller.setReceiveFilterDirection(2);
    controller.setReceiveTxEcho(false);
    controller.sendText(QStringLiteral("tx-only-line"));
    socket->write("rx-only-line\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("TX ▸ tx-only-line")), 3000);
    QVERIFY(!controller.receiveText().contains(QStringLiteral("rx-only-line")));

    // Receive-only direction hides the TX echo.
    controller.setReceiveFilterDirection(1);
    controller.setReceiveTxEcho(true);
    controller.clearReceive();
    controller.sendText(QStringLiteral("hidden-tx"));
    socket->write("visible-rx\n");
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("visible-rx")), 3000);
    QVERIFY(!controller.receiveText().contains(QStringLiteral("hidden-tx")));

    // Both directions with echo enabled.
    controller.setReceiveFilterDirection(0);
    controller.setReceiveTxEcho(true);
    controller.clearReceive();
    controller.sendText(QStringLiteral("echoed-tx"));
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.receiveText().contains(QStringLiteral("TX ▸ echoed-tx")), 3000);

    controller.closeTransport();
}

void TestAppController::legacyImportPreservesGroupHierarchy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile commandsFile(dir.filePath(QStringLiteral("vofa+.cmds.json")));
    QVERIFY(commandsFile.open(QIODevice::WriteOnly));
    commandsFile.write(R"({
        "ctx": {
            "name": "root", "is_group": true,
            "subCmds": [
                {"name": "PID", "is_group": true, "subCmds": [
                    {"name": "kp", "is_group": false, "cmd_hex": "6B 70 25 2E 33 66"}
                ]},
                {"name": "start", "is_group": false, "cmd_hex": "73 74 61 72 74"}
            ]
        }
    })");
    commandsFile.close();

    QFile tabviewsFile(dir.filePath(QStringLiteral("vofa+.tabviews.json")));
    QVERIFY(tabviewsFile.open(QIODevice::WriteOnly));
    tabviewsFile.write(R"({
        "ctx": [{"tabs": [{"widgets": [{
            "path": "slider",
            "ctx": {
                ".": {"from": 0, "to": 5, "step_size": 0.01},
                "slider_inside": {"target_value": 1.0},
                "cmd_menu": {"ctx": [0]},
                "name_menu": {"ctx": {"attr": {"name_": "kp"}}}
            }
        }, {
            "path": "ExtraButtonToggle",
            "ctx": {
                "cmd_menu": {"ctx": [1]},
                "name_menu": {"ctx": {"attr": {"name_": "start"}}}
            }
        }]}]}]
    })");
    tabviewsFile.close();

    AppController controller;
    const auto message = controller.importLegacyFromDirectory(dir.path());
    QVERIFY(message.contains(QStringLiteral("已导入")));
    QVERIFY(controller.commandGroups().contains(QStringLiteral("PID")));

    controller.setCurrentGroup(controller.commandGroups().indexOf(QStringLiteral("PID")));
    QCOMPARE(controller.oneClickBindings().size(), 1);
    QCOMPARE(controller.oneClickBindings().first().toMap()
                 .value(QStringLiteral("label")).toString(),
             QStringLiteral("kp"));

    controller.setCurrentGroup(controller.commandGroups().indexOf(QStringLiteral("默认")));
    QCOMPARE(controller.actionBindings().size(), 1);
    QCOMPARE(controller.actionBindings().first().toMap()
                 .value(QStringLiteral("label")).toString(),
             QStringLiteral("start"));
}

void TestAppController::undoRedoRestoresCommandEdits()
{
    AppController controller;
    const auto group = controller.currentGroupName();

    const int bindingsBefore = controller.oneClickBindings().size();
    QVERIFY(controller.addOneClickBinding(QStringLiteral("undoA"),
                                          QStringLiteral("undoA%.2f"),
                                          0.0, 10.0, 0.1, 1.0, true, group));
    QCOMPARE(controller.oneClickBindings().size(), bindingsBefore + 1);
    QVERIFY(controller.canUndo());

    controller.undo();
    QCOMPARE(controller.oneClickBindings().size(), bindingsBefore);
    QVERIFY(controller.canRedo());

    controller.redo();
    QCOMPARE(controller.oneClickBindings().size(), bindingsBefore + 1);
    controller.undo(); // leave the workspace as found

    const int actionsBefore = controller.actionBindings().size();
    QVERIFY(controller.addActionBinding(QStringLiteral("undoButton"),
                                        QStringLiteral("undocmd"),
                                        QString(), 0, group));
    QCOMPARE(controller.actionBindings().size(), actionsBefore + 1);
    controller.undo();
    QCOMPARE(controller.actionBindings().size(), actionsBefore);

    QVERIFY(controller.addCommandGroup(QStringLiteral("undoGroup")));
    QVERIFY(controller.commandGroups().contains(QStringLiteral("undoGroup")));
    controller.undo();
    QVERIFY(!controller.commandGroups().contains(QStringLiteral("undoGroup")));
}

void TestAppController::legacyImportCreatesBindings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile commandsFile(dir.filePath(QStringLiteral("vofa+.cmds.json")));
    QVERIFY(commandsFile.open(QIODevice::WriteOnly));
    commandsFile.write(R"({
        "type": "cmds",
        "vnumber": 140,
        "ctx": {
            "name": "group",
            "is_group": true,
            "subCmds": [
                {"name": "start", "is_group": false, "cmd_hex": "73 74 61 72 74"},
                {"name": "kp1", "is_group": false, "cmd_hex": "6B 70 31 25 2E 33 66"},
                {"name": "skp", "is_group": false, "cmd_hex": "73 6B 70 25 2E 36 66 0A"},
                {"name": "stop", "is_group": false, "cmd_hex": "73 74 6F 70"},
                {"name": "Brushon", "is_group": false, "cmd_hex": "62 72 75 73 68 6F 6E"},
                {"name": "Brushoff", "is_group": false, "cmd_hex": "62 72 75 73 68 6F 66 66"}
            ]
        }
    })");
    commandsFile.close();

    QFile tabviewsFile(dir.filePath(QStringLiteral("vofa+.tabviews.json")));
    QVERIFY(tabviewsFile.open(QIODevice::WriteOnly));
    tabviewsFile.write(R"({
        "type": "tabviews",
        "vnumber": 140,
        "ctx": [{
            "tabs": [{
                "widgets": [{
                    "path": "slider",
                    "ctx": {
                        ".": {"from": 0, "to": 5, "step_size": 0.01},
                        "slider_inside": {"target_value": 1.5},
                        "cmd_menu": {"ctx": [1]},
                        "name_menu": {"ctx": {"attr": {"name_": "kp1"}}}
                    }
                }, {
                    "path": "ExtraButtonToggle",
                    "ctx": {
                        "cmd_menu": {"ctx": [0]},
                        "name_menu": {"ctx": {"attr": {"name_": "name"}}}
                    }
                }, {
                    "path": "ExtraButtonToggle",
                    "ctx": {
                        "cmd_menu": {"ctx": [4]},
                        "name_menu": {"ctx": {"attr": {"name_": "brush"}}}
                    }
                }, {
                    "path": "slider",
                    "ctx": {}
                }, {
                    "path": "WaveChart",
                    "ctx": {}
                }]
            }]
        }]
    })");
    tabviewsFile.close();

    AppController controller;
    const auto message = controller.importLegacyFromDirectory(dir.path());
    QVERIFY(message.contains(QStringLiteral("已导入")));

    // The migration report must capture both imports and skipped items.
    QFile reportFile(controller.migrationReportPath());
    QVERIFY(reportFile.open(QIODevice::ReadOnly));
    const auto report = QJsonDocument::fromJson(reportFile.readAll()).object();
    reportFile.close();
    QCOMPARE(report.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    const auto backupDir = report.value(QStringLiteral("backup")).toString();
    QVERIFY(!backupDir.isEmpty());
    QVERIFY(QFile::exists(backupDir + QStringLiteral("/vofa+.cmds.json")));
    QVERIFY(QFile::exists(backupDir + QStringLiteral("/vofa+.tabviews.json")));
    const auto commandsReport = report.value(QStringLiteral("commands")).toObject();
    QCOMPARE(commandsReport.value(QStringLiteral("total")).toInt(), 6);
    QCOMPARE(commandsReport.value(QStringLiteral("numeric")).toInt(), 2);
    QCOMPARE(commandsReport.value(QStringLiteral("raw")).toInt(), 4);
    QCOMPARE(commandsReport.value(QStringLiteral("skipped")).toArray().size(), 4);
    const auto widgetsReport = report.value(QStringLiteral("widgets")).toObject();
    const auto slidersReport = widgetsReport.value(QStringLiteral("sliders")).toObject();
    QCOMPARE(slidersReport.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(slidersReport.value(QStringLiteral("imported")).toInt(), 1);
    QCOMPARE(slidersReport.value(QStringLiteral("skipped")).toArray().size(), 1);
    const auto togglesReport = widgetsReport.value(QStringLiteral("toggles")).toObject();
    QCOMPARE(togglesReport.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(togglesReport.value(QStringLiteral("imported")).toInt(), 2);
    QCOMPARE(togglesReport.value(QStringLiteral("skipped")).toArray().size(), 0);
    QCOMPARE(report.value(QStringLiteral("unknownWidgetPaths"))
                 .toObject()
                 .value(QStringLiteral("WaveChart"))
                 .toInt(),
             1);

    const auto bindings = controller.oneClickBindings();
    QCOMPARE(bindings.size(), 1);
    const auto binding = bindings.first().toMap();
    QCOMPARE(binding.value(QStringLiteral("label")).toString(), QStringLiteral("kp1"));
    QCOMPARE(binding.value(QStringLiteral("value")).toDouble(), 1.5);
    QCOMPARE(binding.value(QStringLiteral("min")).toDouble(), 0.0);
    QCOMPARE(binding.value(QStringLiteral("max")).toDouble(), 5.0);

    const auto actions = controller.actionBindings();
    QCOMPARE(actions.size(), 2);
    const auto actionStart = actions.at(0).toMap();
    QCOMPARE(actionStart.value(QStringLiteral("label")).toString(),
             QStringLiteral("start"));
    QCOMPARE(actionStart.value(QStringLiteral("on")).toString(),
             QStringLiteral("start"));
    QCOMPARE(actionStart.value(QStringLiteral("off")).toString(),
             QStringLiteral("stop"));
    const auto actionBrush = actions.at(1).toMap();
    QCOMPARE(actionBrush.value(QStringLiteral("label")).toString(),
             QStringLiteral("brush"));
    QCOMPARE(actionBrush.value(QStringLiteral("on")).toString(),
             QStringLiteral("brushon"));
    QCOMPARE(actionBrush.value(QStringLiteral("off")).toString(),
             QStringLiteral("brushoff"));

    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    controller.openTcpClient(QStringLiteral("127.0.0.1"), listener.serverPort(), QString());
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(listener.nextPendingConnection());
    QVERIFY(!socket.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.connectionState(), QStringLiteral("已连接"), 3000);

    QByteArray received;
    connect(socket.data(), &QTcpSocket::readyRead, this, [&]() {
        received += socket->readAll();
    });

    controller.setOneClickIntervalMs(50);
    controller.startOneClickSend();
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("kp11.500"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.oneClickSending(), 3000);

    const auto startId = static_cast<qulonglong>(
        actionStart.value(QStringLiteral("id")).toULongLong());
    const auto brushId = static_cast<qulonglong>(
        actionBrush.value(QStringLiteral("id")).toULongLong());

    received.clear();
    controller.triggerAction(startId);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("start"), 3000);
    controller.setActionState(startId, false);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("stop"), 3000);

    received.clear();
    controller.triggerAction(brushId);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("brushon"), 3000);
    controller.setActionState(brushId, false);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("brushoff"), 3000);

    controller.closeTransport();
}

QTEST_GUILESS_MAIN(TestAppController)
#include "test_app_controller.moc"
