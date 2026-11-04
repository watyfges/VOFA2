#include "data/ChannelStore.h"
#include "transport/TransportController.h"

#include <QByteArray>
#include <QDataStream>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

using namespace vofa;
using transport::ConnectionState;
using transport::TransportConfig;
using transport::TransportController;
using transport::TransportKind;

namespace {

TransportConfig serverConfig(quint16 port, protocol::ProtocolKind protocolKind)
{
    TransportConfig config;
    config.kind = TransportKind::TcpServer;
    config.protocol = protocolKind;
    config.tcpServer.listenPort = port;
    return config;
}

TransportConfig clientConfig(quint16 port, protocol::ProtocolKind protocolKind)
{
    TransportConfig config;
    config.kind = TransportKind::TcpClient;
    config.protocol = protocolKind;
    config.tcpClient.host = QStringLiteral("127.0.0.1");
    config.tcpClient.port = port;
    return config;
}

TransportConfig udpConfig(quint16 localPort, quint16 remotePort)
{
    TransportConfig config;
    config.kind = TransportKind::Udp;
    config.protocol = protocol::ProtocolKind::RawData;
    config.udp.localPort = localPort;
    config.udp.remotePort = remotePort;
    config.udp.remoteHost = QStringLiteral("127.0.0.1");
    return config;
}

QByteArray justFloatFrame(float a, float b)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << a << b;
    bytes.append("\x00\x00\x80\x7f", 4);
    return bytes;
}

} // namespace

class TestTransport : public QObject {
    Q_OBJECT

private slots:
    void tcpClientServerRoundTrip();
    void tcpServerOnlyFeedsActiveClient();
    void udpLoopback();
    void serialPtyRoundTrip();
    void tcpClientConnectCloseStress();
};

void TestTransport::tcpClientServerRoundTrip()
{
    data::ChannelStore store;
    TransportController server(&store);
    TransportController client(&store);

    QSignalSpy listening(&server, &TransportController::listening);
    QSignalSpy received(&server, &TransportController::received);

    server.open(serverConfig(0, protocol::ProtocolKind::RawData));
    QTRY_VERIFY_WITH_TIMEOUT(listening.count() > 0, 3000);
    const auto port = listening.takeFirst().at(0).value<quint16>();
    QVERIFY(port != 0);

    int clientCount = 0;
    connect(&server,
            &TransportController::clientsChanged,
            &server,
            [&](const QList<quint64>& ids, const QStringList&, quint64) {
                clientCount = static_cast<int>(ids.size());
            });

    client.open(clientConfig(port, protocol::ProtocolKind::RawData));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ConnectionState::Connected, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(clientCount, 1, 3000);

    client.send(QByteArrayLiteral("hello"));
    QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 3000);
    QCOMPARE(received.last().at(2).toByteArray(), QByteArrayLiteral("hello"));

    client.close();
    server.close();
    QCOMPARE(client.state(), ConnectionState::Disconnected);
    QCOMPARE(server.state(), ConnectionState::Disconnected);
}

void TestTransport::tcpServerOnlyFeedsActiveClient()
{
    data::ChannelStore store(4096, 64);
    TransportController server(&store);
    TransportController client1(&store);
    TransportController client2(&store);

    QSignalSpy listening(&server, &TransportController::listening);
    QSignalSpy resetSpy(&server, &TransportController::streamReset);

    server.open(serverConfig(0, protocol::ProtocolKind::JustFloat));
    QTRY_VERIFY_WITH_TIMEOUT(listening.count() > 0, 3000);
    const auto port = listening.takeFirst().at(0).value<quint16>();

    QList<quint64> ids;
    connect(&server,
            &TransportController::clientsChanged,
            &server,
            [&](const QList<quint64>& newIds, const QStringList&, quint64) {
                ids = newIds;
            });

    client1.open(clientConfig(port, protocol::ProtocolKind::RawData));
    QTRY_COMPARE_WITH_TIMEOUT(client1.state(), ConnectionState::Connected, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(ids.size(), 1, 3000);

    client2.open(clientConfig(port, protocol::ProtocolKind::RawData));
    QTRY_COMPARE_WITH_TIMEOUT(client2.state(), ConnectionState::Connected, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(ids.size(), 2, 3000);

    server.setActiveClient(ids[0]);
    QTRY_VERIFY_WITH_TIMEOUT(resetSpy.count() > 0, 3000);
    const auto resetsAfterFirstSwitch = resetSpy.count();

    client1.send(justFloatFrame(1.0f, 2.0f));
    QTRY_VERIFY_WITH_TIMEOUT((store.drain(), store.framesStored() >= 1), 3000);
    QCOMPARE(store.channelCount(), 2u);

    float value = 0.0f;
    const auto* channel0 = store.channel(0);
    QVERIFY(channel0 != nullptr);
    QVERIFY(channel0->copyRange(channel0->absoluteEnd() - 1, 1, &value));
    QCOMPARE(value, 1.0f);

    const auto framesBeforeSwitch = store.framesStored();
    server.setActiveClient(ids[1]);
    QTRY_VERIFY_WITH_TIMEOUT(resetSpy.count() > resetsAfterFirstSwitch, 3000);

    client2.send(justFloatFrame(3.0f, 4.0f));
    QTRY_VERIFY_WITH_TIMEOUT((store.drain(), store.framesStored() > framesBeforeSwitch), 3000);

    channel0 = store.channel(0);
    QVERIFY(channel0 != nullptr);
    QVERIFY(channel0->copyRange(channel0->absoluteEnd() - 1, 1, &value));
    QCOMPARE(value, 3.0f);

    const auto framesAfterSwitch = store.framesStored();
    client1.send(justFloatFrame(9.0f, 9.0f));
    QTest::qWait(200);
    (void)store.drain();
    QCOMPARE(store.framesStored(), framesAfterSwitch);

    client1.close();
    client2.close();
    server.close();
}

void TestTransport::tcpClientConnectCloseStress()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));

    // CI keeps this quick; set VOFA_STRESS_CYCLES=10000 for the full soak.
    const int configured = qEnvironmentVariableIntValue("VOFA_STRESS_CYCLES");
    const int cycles = configured > 0 ? configured : 100;

    data::ChannelStore store(4096, 64);
    TransportController client(&store);

    for (int i = 0; i < cycles; ++i) {
        client.open(clientConfig(listener.serverPort(),
                                 protocol::ProtocolKind::RawData));
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), ConnectionState::Connected, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 3000);
        while (listener.hasPendingConnections()) {
            auto* socket = listener.nextPendingConnection();
            socket->deleteLater();
        }
        client.close();
        QCOMPARE(client.state(), ConnectionState::Disconnected);
    }

    // Rapid open/close must not leak a thread or leave a half-open state.
    // Bound this loop separately so the 10k connect soak stays under the
    // QtTest function watchdog.
    const int rapidCycles = std::min(cycles, 1000);
    for (int i = 0; i < rapidCycles; ++i) {
        client.open(clientConfig(listener.serverPort(),
                                 protocol::ProtocolKind::RawData));
        client.close();
    }
    QCOMPARE(client.state(), ConnectionState::Disconnected);
}

void TestTransport::udpLoopback()
{
    data::ChannelStore store;
    TransportController receiver(&store);
    TransportController sender(&store);

    QSignalSpy receiverListening(&receiver, &TransportController::listening);
    QSignalSpy received(&receiver, &TransportController::received);

    receiver.open(udpConfig(0, 0));
    QTRY_VERIFY_WITH_TIMEOUT(receiverListening.count() > 0, 3000);
    const auto receiverPort = receiverListening.takeFirst().at(0).value<quint16>();
    QVERIFY(receiverPort != 0);

    sender.open(udpConfig(0, receiverPort));
    QTRY_COMPARE_WITH_TIMEOUT(sender.state(), ConnectionState::Connected, 3000);

    sender.send(QByteArrayLiteral("udp"));
    QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 3000);
    QCOMPARE(received.last().at(2).toByteArray(), QByteArrayLiteral("udp"));

    sender.close();
    receiver.close();
}

void TestTransport::serialPtyRoundTrip()
{
#ifdef Q_OS_UNIX
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    QVERIFY(master >= 0);
    QVERIFY(grantpt(master) == 0);
    QVERIFY(unlockpt(master) == 0);
    const char* slaveName = ptsname(master);
    QVERIFY(slaveName != nullptr);
    QVERIFY(fcntl(master, F_SETFL, O_NONBLOCK) == 0);

    data::ChannelStore store;
    TransportController transport(&store);

    TransportConfig config;
    config.kind = TransportKind::Serial;
    config.protocol = protocol::ProtocolKind::RawData;
    config.serial.portName = QString::fromLocal8Bit(slaveName);
    config.serial.baudRate = 921600;
    transport.open(config);
    QTRY_COMPARE_WITH_TIMEOUT(transport.state(), ConnectionState::Connected, 3000);

    QSignalSpy received(&transport, &TransportController::received);
    const QByteArray incoming = QByteArrayLiteral("hello-serial");
    QCOMPARE(static_cast<qsizetype>(::write(master, incoming.constData(), incoming.size())),
             incoming.size());
    QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 3000);
    QCOMPARE(received.last().at(2).toByteArray(), incoming);

    transport.send(QByteArrayLiteral("from-transport"));
    QByteArray outgoing;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() -> bool {
            char buffer[256];
            const auto count = ::read(master, buffer, sizeof(buffer));
            if (count > 0)
                outgoing.append(buffer, static_cast<int>(count));
            return outgoing.contains("from-transport");
        }(),
        3000);

    transport.close();
    ::close(master);
#else
    QSKIP("PTY serial test is Unix-only");
#endif
}

QTEST_GUILESS_MAIN(TestTransport)
#include "test_transport.moc"
