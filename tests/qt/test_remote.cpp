#include "app/AppController.h"
#include "remote/RemoteControlServer.h"
#include "remote/RemoteProtocol.h"
#include "RemoteTestClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>
#include <QtTest>

#include <memory>

using namespace vofa;
using remote::ClientMessageType;
using remote::RemoteControlServer;

using namespace remotetest;

namespace {

// Reads `controls[].inc` for one id out of a `state` message.
bool controlIncluded(const QJsonObject& state, quint64 id, bool* included)
{
    for (const auto& entry : state.value(QStringLiteral("controls")).toArray()) {
        const auto item = entry.toObject();
        if (static_cast<quint64>(item.value(QStringLiteral("id")).toDouble()) == id) {
            if (included != nullptr)
                *included = item.value(QStringLiteral("inc")).toBool();
            return true;
        }
    }
    return false;
}

} // namespace


class TestRemote : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void protocolEncodesState();
    void protocolParsesClientMessages();
    void webSocketHandshakeAndValue();
    void wrongPinIsRejected();
    void discoveryResponds();
    void controllerSyncsRemoteValue();
    void controllerSyncsRemoteAction();
    void controllerSyncsRemoteInclusion();
    void controllerSyncsRemoteLink();
    void kickDropsTheClient();
};

void TestRemote::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("VOFA2RemoteTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .removeRecursively();
}

void TestRemote::cleanupTestCase()
{
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .removeRecursively();
}

void TestRemote::protocolEncodesState()
{
    remote::StateDoc doc;
    doc.serverName = QStringLiteral("调试台");
    doc.version = QStringLiteral("0.1.0");
    doc.groups = QStringList {QStringLiteral("默认"), QStringLiteral("底盘/PID")};
    doc.pcGroup = QStringLiteral("默认");

    remote::ControlDoc control;
    control.id = 1000;
    control.label = QStringLiteral("kp");
    control.group = QStringLiteral("默认");
    control.value = 1.5;
    control.minValue = 0.0;
    control.maxValue = 10.0;
    control.step = 0.01;
    doc.controls.push_back(control);

    remote::ActionDoc action;
    action.id = 3;
    action.label = QStringLiteral("启动");
    action.kind = 0;
    doc.actions.push_back(action);

    doc.transportConnected = true;
    doc.transportState = QStringLiteral("已连接");
    doc.oneClick.intervalMs = 100;

    const auto json = remote::stateToJson(doc, 7);
    QCOMPARE(json.value(QStringLiteral("t")).toString(), QStringLiteral("state"));
    QCOMPARE(json.value(QStringLiteral("seq")).toInt(), 7);
    QCOMPARE(json.value(QStringLiteral("server")).toString(), QStringLiteral("调试台"));

    const auto controls = json.value(QStringLiteral("controls")).toArray();
    QCOMPARE(controls.size(), 1);
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("id")).toDouble(), 1000.0);
    QCOMPARE(controls.at(0).toObject().value(QStringLiteral("v")).toDouble(), 1.5);

    const auto actions = json.value(QStringLiteral("actions")).toArray();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.at(0).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("启动"));

    // Round trip through the wire format.
    const auto parsed = QJsonDocument::fromJson(
        QJsonDocument(json).toJson(QJsonDocument::Compact));
    QVERIFY(parsed.isObject());
    QCOMPARE(parsed.object().value(QStringLiteral("groups")).toArray().size(), 2);
}

void TestRemote::protocolParsesClientMessages()
{
    auto message = remote::parseClientMessage(R"({"t":"hello","clientId":"p1","name":"Pixel"})");
    QCOMPARE(message.type, ClientMessageType::Hello);
    QCOMPARE(message.identity, QStringLiteral("p1"));

    message = remote::parseClientMessage(R"({"t":"value","id":1000,"v":2.5})");
    QCOMPARE(message.type, ClientMessageType::Value);
    QCOMPARE(message.id, quint64(1000));
    QCOMPARE(message.value, 2.5);

    message = remote::parseClientMessage(R"({"t":"commit","id":1000,"v":9.5,"exact":true})");
    QCOMPARE(message.type, ClientMessageType::Commit);
    QVERIFY(message.exact);

    message = remote::parseClientMessage(R"({"t":"action","id":3,"on":true})");
    QCOMPARE(message.type, ClientMessageType::Action);
    QVERIFY(message.on);
    QVERIFY(!message.trigger);

    message = remote::parseClientMessage(R"({"t":"oneclick","group":"默认","intervalMs":50})");
    QCOMPARE(message.type, ClientMessageType::OneClick);
    QCOMPARE(message.group, QStringLiteral("默认"));
    QCOMPARE(message.intervalMs, 50);

    message = remote::parseClientMessage(R"({"t":"oneclick.stop"})");
    QCOMPARE(message.type, ClientMessageType::OneClickStop);

    message = remote::parseClientMessage("not json");
    QCOMPARE(message.type, ClientMessageType::Unknown);
    QVERIFY(!message.error.isEmpty());

    message = remote::parseClientMessage(R"({"t":"value","id":-5,"v":1})");
    QCOMPARE(message.type, ClientMessageType::Unknown);

    message = remote::parseClientMessage(R"({"t":"value","id":5,"v":"abc"})");
    QCOMPARE(message.type, ClientMessageType::Unknown);

    message = remote::parseClientMessage(R"({"t":"setBindingValue","id":5})");
    QCOMPARE(message.type, ClientMessageType::Unknown);

    message = remote::parseClientMessage(R"({"t":"link","cmd":"open","kind":2,"port":1347})");
    QCOMPARE(message.type, ClientMessageType::Link);
    QCOMPARE(message.link.cmd, QStringLiteral("open"));
    QCOMPARE(message.link.kind, 2);
    QCOMPARE(message.link.tcpPort, 1347);

    message = remote::parseClientMessage(R"({"t":"link","cmd":"open","kind":0,"port":"COM3","baud":115200})");
    QCOMPARE(message.type, ClientMessageType::Link);
    QCOMPARE(message.link.port, QStringLiteral("COM3"));
    QCOMPARE(message.link.baud, 115200);

    message = remote::parseClientMessage(R"({"t":"link"})");
    QCOMPARE(message.type, ClientMessageType::Unknown);
}

void TestRemote::webSocketHandshakeAndValue()
{
    RemoteControlServer server;
    QObject::connect(&server, &RemoteControlServer::stateRequested, &server,
                     [&server](quint64 session) {
                         remote::StateDoc doc;
                         doc.serverName = QStringLiteral("测试台");
                         server.sendState(session, remote::stateToJson(doc, 0));
                     });

    QString error;
    QVERIFY2(server.start(0, QStringLiteral("测试台"), QString(), &error),
             qPrintable(error));
    QVERIFY(server.running());
    QVERIFY(server.port() != 0);

    QSignalSpy valueSpy(&server, &RemoteControlServer::valueRequested);

    WsClient client;
    QVERIFY(client.open(server.port()));

    const auto response = client.handshakeResponse();
    QVERIFY(response.startsWith("HTTP/1.1 101"));
    QVERIFY(response.contains("Sec-WebSocket-Accept: "));
    QVERIFY(response.contains(kHandshakeAccept));

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("proto"), 1},
        {QStringLiteral("clientId"), QStringLiteral("test")},
        {QStringLiteral("name"), QStringLiteral("测试手机")},
    });

    const auto welcome = client.waitFor(QStringLiteral("welcome"));
    QVERIFY(!welcome.isEmpty());
    QCOMPARE(welcome.value(QStringLiteral("name")).toString(), QStringLiteral("测试台"));

    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    QCOMPARE(state.value(QStringLiteral("server")).toString(), QStringLiteral("测试台"));

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 2000);

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("value")},
        {QStringLiteral("id"), 1000},
        {QStringLiteral("v"), 3.25},
    });
    QTRY_COMPARE_WITH_TIMEOUT(valueSpy.count(), 1, 2000);
    const auto arguments = valueSpy.takeFirst();
    QCOMPARE(arguments.at(1).toULongLong(), quint64(1000));
    QCOMPARE(arguments.at(2).toDouble(), 3.25);
    QCOMPARE(arguments.at(3).toBool(), false);

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("commit")},
        {QStringLiteral("id"), 1000},
        {QStringLiteral("v"), 4.5},
        {QStringLiteral("exact"), true},
    });
    QTRY_COMPARE_WITH_TIMEOUT(valueSpy.count(), 1, 2000);
    const auto commitArguments = valueSpy.takeFirst();
    QCOMPARE(commitArguments.at(3).toBool(), true);
    QCOMPARE(commitArguments.at(4).toBool(), true);
}

void TestRemote::wrongPinIsRejected()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("带码"), QStringLiteral("1234"), &error),
             qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("x")},
        {QStringLiteral("pin"), QStringLiteral("9999")},
    });

    const auto failure = client.waitFor(QStringLiteral("error"));
    QVERIFY(!failure.isEmpty());
    QCOMPARE(failure.value(QStringLiteral("code")).toString(), QStringLiteral("bad_pin"));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
}

void TestRemote::discoveryResponds()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("发现测试"), QString(), &error), qPrintable(error));

    QUdpSocket probe;
    QVERIFY(probe.bind(QHostAddress::LocalHost, 0));

    const QByteArray request = R"({"t":"discover","proto":1})";
    QCOMPARE(probe.writeDatagram(request, QHostAddress::LocalHost, remote::kDiscoveryPort),
             static_cast<qint64>(request.size()));

    QTRY_VERIFY_WITH_TIMEOUT(probe.hasPendingDatagrams(), 3000);
    QByteArray answer(static_cast<qsizetype>(probe.pendingDatagramSize()), Qt::Uninitialized);
    QVERIFY(probe.readDatagram(answer.data(), answer.size()) > 0);

    const auto offer = QJsonDocument::fromJson(answer).object();
    QCOMPARE(offer.value(QStringLiteral("t")).toString(), QStringLiteral("offer"));
    QCOMPARE(offer.value(QStringLiteral("name")).toString(), QStringLiteral("发现测试"));
    QCOMPARE(offer.value(QStringLiteral("port")).toInt(), server.port());
}

void TestRemote::controllerSyncsRemoteValue()
{
    const auto port = freePort();
    QVERIFY(port != 0);

    app::AppController controller;
    // PC 界面靠这条信号刷新被远程改动的行；它必须真的被发出来。
    QSignalSpy appliedSpy(&controller, &app::AppController::remoteValueApplied);
    QVERIFY(appliedSpy.isValid());
    QVERIFY(controller.addOneClickBinding(QStringLiteral("remoteKp"),
                                          QStringLiteral("remoteKp%.3f"),
                                          0.0, 10.0, 0.01, 1.0, true,
                                          QStringLiteral("默认")));
    controller.setRemotePort(port);
    controller.setRemoteEnabled(true);
    QVERIFY(controller.remoteEnabled());
    QVERIFY(controller.remoteStatus().contains(QStringLiteral("运行中")));

    WsClient client;
    QVERIFY(client.open(port));

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("phone-1")},
        {QStringLiteral("name"), QStringLiteral("测试手机")},
    });

    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    const auto controls = state.value(QStringLiteral("controls")).toArray();
    QVERIFY(!controls.isEmpty());

    // The default workspace already seeds sliders, so find our own control.
    quint64 controlId = 0;
    for (const auto& value : controls) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("label")).toString() == QStringLiteral("remoteKp")) {
            controlId = static_cast<quint64>(item.value(QStringLiteral("id")).toDouble());
            break;
        }
    }
    QVERIFY(controlId != 0);

    QTRY_COMPARE_WITH_TIMEOUT(controller.remoteClientCount(), 1, 3000);

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("commit")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("v"), 4.5},
        {QStringLiteral("exact"), false},
    });

    // Applied to the PC model...
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            for (const auto& item : controller.oneClickBindings()) {
                const auto map = item.toMap();
                if (map.value(QStringLiteral("id")).toULongLong() == controlId) {
                    return qFuzzyCompare(map.value(QStringLiteral("value")).toDouble(), 4.5);
                }
            }
            return false;
        }(),
        3000);

    // ...and echoed back to the client that wrote it.
    const auto echo = client.waitFor(QStringLiteral("value"));
    QVERIFY(!echo.isEmpty());
    QCOMPARE(echo.value(QStringLiteral("v")).toDouble(), 4.5);
    QVERIFY(echo.value(QStringLiteral("from")).toString().startsWith(QLatin1Char('c')));

    // ...and the PC UI is told exactly which row moved.
    QCOMPARE(appliedSpy.count(), 1);
    const auto applied = appliedSpy.at(0);
    QCOMPARE(applied.at(0).toULongLong(), controlId);
    QCOMPARE(applied.at(1).toDouble(), 4.5);

    // The other direction: a change made on the PC reaches the phone.
    controller.setBindingValue(controlId, 7.5);
    const auto pushed = client.waitFor(QStringLiteral("value"));
    QVERIFY(!pushed.isEmpty());
    QCOMPARE(pushed.value(QStringLiteral("v")).toDouble(), 7.5);
    QCOMPARE(pushed.value(QStringLiteral("from")).toString(), QStringLiteral("pc"));
    // PC 本地改动不走这条信号，否则界面会自己覆盖自己。
    QCOMPARE(appliedSpy.count(), 1);

    // Multi-client fan-out: a second phone sees the first phone's writes too.
    WsClient other;
    QVERIFY(other.open(port));
    other.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("phone-2")},
    });
    QVERIFY(!other.waitFor(QStringLiteral("state")).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(controller.remoteClientCount(), 2, 3000);

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("commit")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("v"), 6.25},
    });

    for (auto* peer : {&client, &other}) {
        const auto broadcast = peer->waitFor(QStringLiteral("value"));
        QVERIFY(!broadcast.isEmpty());
        QCOMPARE(broadcast.value(QStringLiteral("v")).toDouble(), 6.25);
        QVERIFY(broadcast.value(QStringLiteral("from")).toString().startsWith(QLatin1Char('c')));
    }

    // Typed value (exact escape hatch) must refresh the PC row as well.
    const auto before = appliedSpy.count();
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("commit")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("v"), 8.75},
        {QStringLiteral("exact"), true},
    });
    QTRY_COMPARE_WITH_TIMEOUT(appliedSpy.count(), before + 1, 3000);
    const auto exactApplied = appliedSpy.last();
    QCOMPARE(exactApplied.at(0).toULongLong(), controlId);
    QCOMPARE(exactApplied.at(1).toDouble(), 8.75);
}

void TestRemote::controllerSyncsRemoteAction()
{
    const auto port = freePort();
    QVERIFY(port != 0);

    app::AppController controller;
    QVERIFY(controller.addActionBinding(QStringLiteral("自检"),
                                        QStringLiteral("selfcheck"),
                                        QString(), 0, QStringLiteral("默认")));
    controller.setRemotePort(port);
    controller.setRemoteEnabled(true);

    WsClient client;
    QVERIFY(client.open(port));

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("phone-2")},
    });

    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    const auto actions = state.value(QStringLiteral("actions")).toArray();
    QVERIFY(!actions.isEmpty());
    const auto actionId = static_cast<quint64>(
        actions.at(0).toObject().value(QStringLiteral("id")).toDouble());

    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("action")},
        {QStringLiteral("id"), static_cast<double>(actionId)},
        {QStringLiteral("trigger"), true},
    });

    const auto echo = client.waitFor(QStringLiteral("action"));
    QVERIFY(!echo.isEmpty());
    QCOMPARE(echo.value(QStringLiteral("flash")).toBool(), true);

    // A write to a control that does not exist must not crash or silently pass.
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("value")},
        {QStringLiteral("id"), 999999},
        {QStringLiteral("v"), 1.0},
    });
    const auto failure = client.waitFor(QStringLiteral("error"));
    QVERIFY(!failure.isEmpty());
    QCOMPARE(failure.value(QStringLiteral("code")).toString(),
             QStringLiteral("unknown_control"));
}

void TestRemote::controllerSyncsRemoteInclusion()
{
    const auto port = freePort();
    QVERIFY(port != 0);

    app::AppController controller;
    QVERIFY(controller.addOneClickBinding(QStringLiteral("incKp"),
                                          QStringLiteral("incKp%.3f"),
                                          0.0, 10.0, 0.01, 2.5, true,
                                          QStringLiteral("默认")));
    controller.setRemotePort(port);
    controller.setRemoteEnabled(true);

    WsClient client;
    QVERIFY(client.open(port));
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("phone-inc")},
    });

    // 设置是持久化的，前面用例留下的绑定也会出现在 state 里，所以按 label 找自己那条。
    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    quint64 controlId = 0;
    double initialValue = 0.0;
    bool initialIncluded = false;
    for (const auto& entry : state.value(QStringLiteral("controls")).toArray()) {
        const auto item = entry.toObject();
        if (item.value(QStringLiteral("label")).toString() == QStringLiteral("incKp")) {
            controlId = static_cast<quint64>(item.value(QStringLiteral("id")).toDouble());
            initialValue = item.value(QStringLiteral("v")).toDouble();
            initialIncluded = item.value(QStringLiteral("inc")).toBool();
            break;
        }
    }
    QVERIFY(controlId != 0);
    QCOMPARE(initialIncluded, true);

    // 手机取消「参与一键发送数据」：PC 要按新的计划重发 state。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("inc")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("inc"), false},
    });

    bool included = true;
    const auto afterOff = client.waitFor(QStringLiteral("state"));
    QVERIFY(!afterOff.isEmpty());
    QVERIFY(controlIncluded(afterOff, controlId, &included));
    QCOMPARE(included, false);

    // 再勾回来，值不能被顺手改掉。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("inc")},
        {QStringLiteral("id"), static_cast<double>(controlId)},
        {QStringLiteral("inc"), true},
    });

    const auto afterOn = client.waitFor(QStringLiteral("state"));
    QVERIFY(!afterOn.isEmpty());
    QVERIFY(controlIncluded(afterOn, controlId, &included));
    QCOMPARE(included, true);
    for (const auto& entry : afterOn.value(QStringLiteral("controls")).toArray()) {
        const auto item = entry.toObject();
        if (static_cast<quint64>(item.value(QStringLiteral("id")).toDouble()) == controlId)
            QCOMPARE(item.value(QStringLiteral("v")).toDouble(), initialValue);
    }

    // 不存在的控件要报错，不能静默吞掉。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("inc")},
        {QStringLiteral("id"), 999999},
        {QStringLiteral("inc"), false},
    });
    const auto failure = client.waitFor(QStringLiteral("error"));
    QVERIFY(!failure.isEmpty());
    QCOMPARE(failure.value(QStringLiteral("code")).toString(),
             QStringLiteral("unknown_control"));
}

void TestRemote::controllerSyncsRemoteLink()
{
    const auto port = freePort();
    QVERIFY(port != 0);

    app::AppController controller;
    controller.refreshSerialPorts();
    controller.setRemotePort(port);
    controller.setRemoteEnabled(true);

    WsClient client;
    QVERIFY(client.open(port));
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("phone-link")},
    });

    const auto state = client.waitFor(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    QVERIFY(state.contains(QStringLiteral("link")));
    const auto initialLink = state.value(QStringLiteral("link")).toObject();
    QCOMPARE(initialLink.value(QStringLiteral("kind")).toInt(), 0);
    QVERIFY(!initialLink.value(QStringLiteral("connected")).toBool());

    // 手机开演示连接：链路状态要推给手机。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("link")},
        {QStringLiteral("cmd"), QStringLiteral("open")},
        {QStringLiteral("kind"), 4},
    });

    bool sawDemo = false;
    bool sawConnected = false;
    for (int i = 0; i < 5 && !(sawDemo && sawConnected); ++i) {
        const auto link = client.waitFor(QStringLiteral("link"), 4000);
        if (link.isEmpty())
            break;
        if (link.value(QStringLiteral("kind")).toInt() == 4)
            sawDemo = true;
        if (link.value(QStringLiteral("connected")).toBool())
            sawConnected = true;
    }
    QVERIFY2(sawDemo, "open 后应推送 kind=4 的 link");
    QVERIFY2(sawConnected, "演示连接应立即建立");

    // 已连接时再 open 要被拒绝，不能悄悄切换。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("link")},
        {QStringLiteral("cmd"), QStringLiteral("open")},
        {QStringLiteral("kind"), 1},
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), 1346},
    });
    const auto busy = client.waitFor(QStringLiteral("error"));
    QVERIFY(!busy.isEmpty());
    QCOMPARE(busy.value(QStringLiteral("code")).toString(), QStringLiteral("link_busy"));

    // 断开后 connected 变回 false。
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("link")},
        {QStringLiteral("cmd"), QStringLiteral("close")},
    });
    bool sawDisconnected = false;
    for (int i = 0; i < 5 && !sawDisconnected; ++i) {
        const auto link = client.waitFor(QStringLiteral("link"), 4000);
        if (link.isEmpty())
            break;
        if (!link.value(QStringLiteral("connected")).toBool())
            sawDisconnected = true;
    }
    QVERIFY2(sawDisconnected, "close 后应推送 disconnected");
}

void TestRemote::kickDropsTheClient()
{
    RemoteControlServer server;
    QString error;
    QVERIFY2(server.start(0, QStringLiteral("踢人"), QString(), &error), qPrintable(error));

    WsClient client;
    QVERIFY(client.open(server.port()));
    client.send(QJsonObject {
        {QStringLiteral("t"), QStringLiteral("hello")},
        {QStringLiteral("clientId"), QStringLiteral("kick-me")},
    });
    QVERIFY(!client.waitFor(QStringLiteral("welcome")).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);

    const auto session = server.clients().first().session;
    server.kick(session);

    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(client.socket.state() != QAbstractSocket::ConnectedState, 3000);
}

QTEST_GUILESS_MAIN(TestRemote)
#include "test_remote.moc"

