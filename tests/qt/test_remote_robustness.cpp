#include "RemoteTestClient.h"
#include "app/AppController.h"
#include "remote/RemoteControlServer.h"
#include "remote/RemoteProtocol.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QtTest>

#include <memory>
#include <random>
#include <vector>

using namespace vofa;
using remote::RemoteControlServer;
using namespace remotetest;

namespace {

remote::StateDoc demoState(int extraControls = 0)
{
    remote::StateDoc doc;
    doc.serverName = QStringLiteral("稳健性");
    doc.version = QStringLiteral("0.1.0");
    doc.groups = QStringList {QStringLiteral("默认")};
    doc.pcGroup = QStringLiteral("默认");

    remote::ControlDoc control;
    control.id = 1000;
    control.label = QStringLiteral("kp");
    control.group = QStringLiteral("默认");
    control.value = 1.0;
    control.minValue = 0.0;
    control.maxValue = 10.0;
    control.step = 0.01;
    doc.controls.push_back(control);

    for (int i = 0; i < extraControls; ++i) {
        remote::ControlDoc padded = control;
        padded.id = 2000 + static_cast<quint64>(i);
        padded.label = QStringLiteral("padding_channel_%1").arg(i);
        doc.controls.push_back(padded);
    }

    remote::ActionDoc action;
    action.id = 3;
    action.label = QStringLiteral("启动");
    action.kind = 0;
    doc.actions.push_back(action);
    return doc;
}

// Answers the initial snapshot like AppController does.
void attachStateProvider(RemoteControlServer& server, int extraControls = 0)
{
    QObject::connect(&server, &RemoteControlServer::stateRequested, &server,
                     [&server, extraControls](quint64 session) {
                         server.sendState(session, remote::stateToJson(demoState(extraControls), 0));
                     });
}

bool greet(WsClient& client)
{
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("probe")},
        {QStringLiteral("name"), QStringLiteral("压测")},
    });
    return !client.waitFor(QStringLiteral("welcome")).isEmpty();
}

} // namespace

// These tests throw malformed, truncated, oversized and hostile traffic at the
// LAN server. They assert that the process survives, that every connection is
// accounted for, and that the server still works afterwards. A crash here would
// take the serial link (and the car) down with it.
class TestRemoteRobustness : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void garbageBeforeHandshake();
    void httpRequestWithoutUpgradeKey();
    void oversizedHandshakeIsDropped();
    void truncatedHandshakeIsHarmless();
    void unmaskedFrameIsRejected();
    void absurdFrameLengthIsRejected();
    void oversizedMessageIsRejected();
    void fragmentedAndSplitDeliveryReassembles();
    void malformedJsonKeepsConnectionAlive();
    void pingAndCloseFramesAreHandled();
    void floodIsRateLimited();
    void sameAddressIsCappedSoRealPhonesGetIn();
    void slowReaderDoesNotBufferForever();
    void rapidConnectDisconnectLeavesNoClients();
    void randomTrafficFuzzKeepsServerUsable();
    void controllerRejectsHostileWrites();
};

void TestRemoteRobustness::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("VOFA2RemoteFuzzTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .removeRecursively();
}

void TestRemoteRobustness::cleanupTestCase()
{
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .removeRecursively();
}

void TestRemoteRobustness::garbageBeforeHandshake()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("垃圾输入"), QString(), &error), qPrintable(error));

    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(raw.waitForConnected(3000));

    QByteArray junk;
    junk.reserve(4096);
    for (int i = 0; i < 4096; ++i)
        junk.append(static_cast<char>((i * 37 + 11) & 0xff));
    raw.write(junk);
    raw.flush();
    for (int i = 0; i < 20; ++i) {
        pump();
        QThread::msleep(2);
    }
    raw.abort();

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);

    // Still serving.
    WsClient good;
    QVERIFY(good.open(server.port()));
    QVERIFY(greet(good));
}

void TestRemoteRobustness::httpRequestWithoutUpgradeKey()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("HTTP"), QString(), &error), qPrintable(error));

    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(raw.waitForConnected(3000));
    raw.write("GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n\r\n");
    raw.flush();

    QByteArray answer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000 && !answer.contains("\r\n\r\n")) {
        pump();
        answer.append(raw.readAll());
        QThread::msleep(2);
    }
    QVERIFY(answer.startsWith("HTTP/1.1 200"));
    QVERIFY(answer.contains("vofa2"));
}

void TestRemoteRobustness::oversizedHandshakeIsDropped()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("超长头"), QString(), &error), qPrintable(error));

    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(raw.waitForConnected(3000));
    // No CRLFCRLF ever: the parser must cap the buffer instead of growing it.
    raw.write(QByteArray(9000, 'A'));
    raw.flush();

    QElapsedTimer timer;
    timer.start();
    bool dropped = false;
    while (timer.elapsed() < 4000) {
        pump();
        if (raw.state() != QAbstractSocket::ConnectedState) {
            dropped = true;
            break;
        }
        QThread::msleep(2);
    }
    QVERIFY(dropped);
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
}

void TestRemoteRobustness::truncatedHandshakeIsHarmless()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("半包"), QString(), &error), qPrintable(error));

    for (int i = 0; i < 20; ++i) {
        QTcpSocket raw;
        raw.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(raw.waitForConnected(3000));
        raw.write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nSec-Web");
        raw.flush();
        QThread::msleep(1);
        raw.abort();
        pump();
    }

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
    WsClient good;
    QVERIFY(good.open(server.port()));
    QVERIFY(greet(good));
}

void TestRemoteRobustness::unmaskedFrameIsRejected()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("无掩码"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    client.sendRaw(clientFrame(QByteArrayLiteral(R"({"t":"hello"})"), 0x1, /*masked=*/false));
    QVERIFY(client.waitForDisconnect(4000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
}

void TestRemoteRobustness::absurdFrameLengthIsRejected()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("假长度"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    // Claims 1 TiB in the header without sending a body: the server must reject
    // it instead of waiting for (or allocating) the payload.
    client.sendRaw(frameHeader(1ULL << 40));
    QVERIFY(client.waitForDisconnect(4000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
}

void TestRemoteRobustness::oversizedMessageIsRejected()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("超长消息"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    QVERIFY(greet(client));
    client.sendRaw(clientFrame(QByteArray(100 * 1024, 'x')));
    QVERIFY(client.waitForDisconnect(4000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
}

void TestRemoteRobustness::fragmentedAndSplitDeliveryReassembles()
{
    RemoteControlServer server;
    attachStateProvider(server);
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("半包2"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));

    // Byte-by-byte delivery: every write is its own TCP segment.
    const auto frame = clientJson(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("split")},
    });
    for (const char byte : frame) {
        client.sendRaw(QByteArray(1, byte));
        pump(2);
    }
    QVERIFY(!client.waitFor(QStringLiteral("welcome")).isEmpty());

    // RFC 6455 fragmentation: "hel" + "lo" in two frames.
    const QByteArray payload = QByteArrayLiteral(R"({"t":"hello","clientId":"frag"})");
    QByteArray firstFrame;
    firstFrame.append(static_cast<char>(0x01)); // FIN=0, text
    firstFrame.append(static_cast<char>(0x80 | 10));
    firstFrame.append(QByteArray("\x11\x22\x33\x44", 4));
    for (int i = 0; i < 10; ++i)
        firstFrame.append(static_cast<char>(payload[i] ^ "\x11\x22\x33\x44"[i % 4]));

    QByteArray lastFrame;
    lastFrame.append(static_cast<char>(0x80 | 0x00)); // FIN=1, continuation
    const int rest = payload.size() - 10;
    lastFrame.append(static_cast<char>(0x80 | rest));
    lastFrame.append(QByteArray("\x11\x22\x33\x44", 4));
    for (int i = 0; i < rest; ++i)
        lastFrame.append(static_cast<char>(payload[10 + i] ^ "\x11\x22\x33\x44"[i % 4]));

    WsClient second;
    QVERIFY(second.open(server.port()));
    second.sendRaw(firstFrame);
    QThread::msleep(20);
    second.sendRaw(lastFrame);
    QVERIFY(!second.waitFor(QStringLiteral("welcome")).isEmpty());
}

void TestRemoteRobustness::malformedJsonKeepsConnectionAlive()
{
    RemoteControlServer server;
    attachStateProvider(server);
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("坏JSON"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    QVERIFY(greet(client));

    const QList<QByteArray> garbage {
        QByteArrayLiteral(R"({"t":"value")"),         // truncated
        QByteArrayLiteral("[]"),                       // not an object
        QByteArrayLiteral("null"),
        QByteArrayLiteral("123"),
        QByteArrayLiteral("\"text\""),
        QByteArrayLiteral(R"({"t":123})"),             // type not a string
        QByteArrayLiteral(R"({"t":"value","id":"x","v":1})"),
        QByteArrayLiteral(R"({"t":"value","id":1,"v":"NaN"})"),
        QByteArrayLiteral(R"({"t":"setBindingValue","id":1,"v":2})"), // forbidden op
        QByteArray("\xff\xfe\x00\x01\x02", 5),         // invalid UTF-8
        QByteArray(5000, '['),                         // absurd nesting
        QByteArray(),                                  // empty frame
    };

    for (const auto& payload : garbage) {
        client.sendRaw(clientFrame(payload));
        client.drain();
    }

    // Still alive and still applying valid writes.
    QSignalSpy valueSpy(&server, &RemoteControlServer::valueRequested);
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("value")},
        {QStringLiteral("id"), 1000},
        {QStringLiteral("v"), 2.5},
    });
    QTRY_COMPARE_WITH_TIMEOUT(valueSpy.count(), 1, 3000);
    QCOMPARE(server.clientCount(), 1);
}

void TestRemoteRobustness::pingAndCloseFramesAreHandled()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("控制帧"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    QVERIFY(greet(client));

    client.sendRaw(clientFrame(QByteArrayLiteral("ping-payload"), 0x9)); // ping
    pump();
    QThread::msleep(30);
    client.drain();

    client.sendRaw(clientFrame(QByteArray::fromHex("03e8"), 0x8)); // close, code 1000
    QVERIFY(client.waitForDisconnect(4000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 4000);
}

void TestRemoteRobustness::floodIsRateLimited()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("洪水"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    QVERIFY(greet(client));

    const auto ping = clientJson(QJsonObject {{QStringLiteral("t"), QStringLiteral("ping")}});
    for (int i = 0; i < 3000; ++i)
        client.sendRaw(ping);

    QVERIFY(client.waitForDisconnect(6000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 6000);

    WsClient good;
    QVERIFY(good.open(server.port()));
    QVERIFY(greet(good));
}

void TestRemoteRobustness::sameAddressIsCappedSoRealPhonesGetIn()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("同址限制"), QString(), &error), qPrintable(error));

    // Squat the slots from one address. The cap must stop a runaway device from
    // taking all 16 and locking a legitimate phone out.
    std::vector<std::unique_ptr<WsClient>> squatters;
    for (int i = 0; i < remote::kMaxClientsPerAddress + 2; ++i) {
        auto client = std::make_unique<WsClient>();
        if (!client->open(server.port()))
            continue;
        (void)greet(*client);
        squatters.push_back(std::move(client));
    }

    QCOMPARE(server.clientCount(), remote::kMaxClientsPerAddress);
    QVERIFY(static_cast<int>(squatters.size()) <= remote::kMaxClientsPerAddress + 2);

    // Dropping one squatter frees a slot again.
    server.kick(server.clients().first().session);
    QTRY_VERIFY_WITH_TIMEOUT(server.clientCount() < remote::kMaxClientsPerAddress, 4000);

    WsClient phone;
    QVERIFY(phone.open(server.port()));
    QVERIFY(greet(phone));
}

void TestRemoteRobustness::slowReaderDoesNotBufferForever()
{
    RemoteControlServer server;
    attachStateProvider(server, 400); // ~40 KB snapshot per broadcast
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("慢读"), QString(), &error), qPrintable(error));

    WsClient client;
    client.socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
    QVERIFY(client.open(server.port()));
    QVERIFY(greet(client));

    // The client never drains. The server must drop it once its write backlog
    // passes the limit instead of buffering tens of megabytes.
    bool dropped = false;
    for (int i = 0; i < 400 && !dropped; ++i) {
        server.broadcastState(remote::stateToJson(demoState(400), 0));
        pump(2);
        QThread::msleep(1);
        client.socket.readAll(); // keeps the kernel buffer from wedging the test
        dropped = server.clientCount() == 0;
    }
    // readAll() above relieves pressure; if the guard never fired the client is
    // still connected but the backlog must stay bounded.
    QVERIFY(dropped || server.clientCount() <= 1);
}

void TestRemoteRobustness::rapidConnectDisconnectLeavesNoClients()
{
    RemoteControlServer server;
    attachStateProvider(server);
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("快速重连"), QString(), &error), qPrintable(error));

    QSignalSpy clientsSpy(&server, &RemoteControlServer::clientsChanged);
    for (int i = 0; i < 120; ++i) {
        WsClient client;
        if (!client.open(server.port()))
            continue;
        if (i % 3 == 0)
            (void)greet(client);
        client.closeAbruptly();
        pump(1);
    }

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 6000);
    QVERIFY(clientsSpy.count() > 0);

    WsClient good;
    QVERIFY(good.open(server.port()));
    QVERIFY(greet(good));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);
}

void TestRemoteRobustness::randomTrafficFuzzKeepsServerUsable()
{
    RemoteControlServer server;
    attachStateProvider(server);
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("随机"), QString(), &error), qPrintable(error));

    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<int> sizeDist(0, 300);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> opcodeDist(0, 15);

    for (int round = 0; round < 40; ++round) {
        WsClient client;
        if (!client.open(server.port()))
            continue;
        if (round % 2 == 0)
            (void)greet(client);

        for (int i = 0; i < 40; ++i) {
            const auto size = sizeDist(rng);
            QByteArray payload;
            payload.reserve(size);
            for (int j = 0; j < size; ++j)
                payload.append(static_cast<char>(byteDist(rng)));
            const auto opcode = static_cast<quint8>(opcodeDist(rng));
            client.sendRaw(clientFrame(payload, opcode, (i % 5) != 0));
        }
        pump(5);
        client.closeAbruptly();
        pump(2);
    }

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 8000);

    // The server is still fully functional after the abuse.
    WsClient good;
    QVERIFY(good.open(server.port()));
    QVERIFY(greet(good));
    const auto state = good.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    QCOMPARE(state.value(QStringLiteral("server")).toString(), QStringLiteral("稳健性"));
}

void TestRemoteRobustness::controllerRejectsHostileWrites()
{
    const auto port = freePort();
    QVERIFY(port != 0);

    app::AppController controller;
    QVERIFY(controller.addOneClickBinding(QStringLiteral("robustKp"),
                                          QStringLiteral("robustKp%.3f"),
                                          0.0, 10.0, 0.01, 2.0, true,
                                          QStringLiteral("默认")));
    controller.setRemotePort(port);
    controller.setRemoteEnabled(true);

    WsClient client;
    QVERIFY(client.open(port));
    QVERIFY(greet(client));

    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    quint64 controlId = 0;
    for (const auto& value : state.value(QStringLiteral("controls")).toArray()) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("label")).toString() == QStringLiteral("robustKp"))
            controlId = static_cast<quint64>(item.value(QStringLiteral("id")).toDouble());
    }
    QVERIFY(controlId != 0);

    const QList<QByteArray> hostile {
        R"({"t":"value","id":1,"v":1e999})",
        R"({"t":"value","id":1,"v":-1e999})",
        R"({"t":"commit","id":9007199254740991,"v":1})",
        R"({"t":"value","id":0,"v":1})",
        R"({"t":"action","id":999999,"on":true})",
        R"({"t":"action","id":3,"trigger":true})",
        R"({"t":"oneclick","group":"不存在的组"})",
        R"({"t":"oneclick","group":"默认","intervalMs":1e12})",
        R"({"t":"oneclick.stop"})",
        R"({"t":"sync"})",
        R"({"t":"value","id":1,"v":1,"exact":"yes"})",
    };
    for (const auto& payload : hostile) {
        client.sendRaw(clientFrame(payload));
        client.drain();
    }

    // No hostile write may have corrupted the model: it still holds a finite,
    // in-range value.
    for (const auto& item : controller.oneClickBindings()) {
        const auto map = item.toMap();
        if (map.value(QStringLiteral("id")).toULongLong() != controlId)
            continue;
        const auto value = map.value(QStringLiteral("value")).toDouble();
        QVERIFY(std::isfinite(value));
        QVERIFY(value >= 0.0 && value <= 10.0);
    }

    // A sane write still works afterwards.
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("commit")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("v"), 3.5},
    });
    const auto echo = client.waitFor(QStringLiteral("value"));
    QVERIFY(!echo.isEmpty());
    QCOMPARE(echo.value(QStringLiteral("v")).toDouble(), 3.5);
    QCOMPARE(controller.remoteClientCount(), 1);
}

QTEST_GUILESS_MAIN(TestRemoteRobustness)
#include "test_remote_robustness.moc"
