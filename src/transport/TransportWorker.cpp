#include "TransportWorker.h"

#include "core/Limits.h"
#include "data/ChannelStore.h"
#include "protocol/IStreamParser.h"
#include "protocol/SampleImageParser.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace vofa::transport {
namespace {

std::int64_t nowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QByteArray toByteArray(std::span<const std::byte> bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
}

} // namespace

struct TransportWorker::Sink final : protocol::IFrameSink {
    explicit Sink(TransportWorker* owner)
        : owner(owner)
    {
    }

    TransportWorker* owner;

    void onSamples(std::span<const float> samples, std::int64_t timestampNs) override
    {
        if (owner->store_ != nullptr)
            (void)owner->store_->pushFrame(samples, timestampNs);
    }

    void onImage(const protocol::ImageFrameView& image) override
    {
        emit owner->imageReceived(owner->generation_,
                                  image.id,
                                  static_cast<int>(image.format),
                                  static_cast<int>(image.width),
                                  static_cast<int>(image.height),
                                  toByteArray(image.encoded));
    }
};

TransportWorker::TransportWorker(data::ChannelStore* store, QObject* parent)
    : QObject(parent)
    , store_(store)
    , sink_(std::make_unique<Sink>(this))
{
}

TransportWorker::~TransportWorker()
{
    closeAll();
}

void TransportWorker::open(const TransportConfig& config)
{
    closeAll();
    config_ = config;
    ++generation_;
    activeClientId_ = 0;
    parser_.reset();
    parser_ = protocol::makeSampleParser(config.protocol);
    if (!sink_)
        sink_ = std::make_unique<Sink>(this);

    setState(ConnectionState::Opening);

    switch (config.kind) {
    case TransportKind::Serial:
        openSerial(config);
        break;
    case TransportKind::TcpClient:
        openTcpClient(config);
        break;
    case TransportKind::TcpServer:
        openTcpServer(config);
        break;
    case TransportKind::Udp:
        openUdp(config);
        break;
    case TransportKind::Demo:
        openDemo();
        break;
    }
}

void TransportWorker::shutdown()
{
    closeAll();
    parser_.reset();
    ++generation_;
    setState(ConnectionState::Disconnected);
    emitClients();
}

void TransportWorker::openSerial(const TransportConfig& config)
{
    serial_ = new QSerialPort(this);
    connect(serial_, &QSerialPort::readyRead, this, &TransportWorker::onSerialReadyRead);
    connect(serial_, &QSerialPort::errorOccurred, this, &TransportWorker::onSerialError);

    serial_->setPortName(config.serial.portName);
    serial_->setBaudRate(config.serial.baudRate);
    serial_->setDataBits(config.serial.dataBits);
    serial_->setParity(config.serial.parity);
    serial_->setStopBits(config.serial.stopBits);
    serial_->setFlowControl(config.serial.flowControl);

    if (!serial_->open(QIODevice::ReadWrite)) {
        const auto error = serial_->errorString();
        serial_->deleteLater();
        serial_ = nullptr;
        setState(ConnectionState::Error, error);
        return;
    }

    (void)serial_->setDataTerminalReady(config.serial.dtr);
    (void)serial_->setRequestToSend(config.serial.rts);
    (void)serial_->setBreakEnabled(config.serial.breakEnabled);

    setState(ConnectionState::Connected);
}

void TransportWorker::setBreak(bool enabled)
{
    config_.serial.breakEnabled = enabled;
    if (serial_ != nullptr)
        (void)serial_->setBreakEnabled(enabled);
}

void TransportWorker::onSerialReadyRead()
{
    if (serial_ == nullptr)
        return;
    processIncoming(0, serial_->readAll(), true);
}

void TransportWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || serial_ == nullptr)
        return;

    const auto message = serial_->errorString();
    emit errorOccurred(message);

    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError) {
        auto* port = serial_;
        serial_ = nullptr;
        port->disconnect(this);
        port->close();
        port->deleteLater();
        setState(ConnectionState::Error, message);
    }
}

void TransportWorker::openTcpClient(const TransportConfig& config)
{
    tcpClient_ = new QTcpSocket(this);
    connect(tcpClient_, &QTcpSocket::connected, this, &TransportWorker::onTcpClientConnected);
    connect(tcpClient_, &QTcpSocket::disconnected, this, &TransportWorker::onTcpClientDisconnected);
    connect(tcpClient_, &QTcpSocket::readyRead, this, &TransportWorker::onTcpClientReadyRead);
    connect(tcpClient_, &QTcpSocket::errorOccurred, this, &TransportWorker::onTcpClientError);

    tcpClient_->connectToHost(config.tcpClient.host, config.tcpClient.port);
}

void TransportWorker::onTcpClientConnected()
{
    if (tcpClient_ == nullptr)
        return;
    setState(ConnectionState::Connected);
    if (!config_.tcpClient.banner.isEmpty())
        tcpClient_->write(config_.tcpClient.banner);
}

void TransportWorker::onTcpClientDisconnected()
{
    setState(ConnectionState::Disconnected);
    if (tcpClient_ != nullptr) {
        tcpClient_->deleteLater();
        tcpClient_ = nullptr;
    }
}

void TransportWorker::onTcpClientReadyRead()
{
    if (tcpClient_ == nullptr)
        return;
    processIncoming(0, tcpClient_->readAll(), true);
}

void TransportWorker::onTcpClientError()
{
    if (tcpClient_ == nullptr)
        return;

    auto* socket = tcpClient_;
    tcpClient_ = nullptr;
    const auto message = socket->errorString();
    const auto error = socket->error();

    // Detach first: abort() may emit disconnected() synchronously, and the
    // slot must not touch the socket after it has been torn down.
    socket->disconnect(this);

    // A peer closing the connection is a normal disconnect, not an error.
    if (error == QAbstractSocket::RemoteHostClosedError) {
        setState(ConnectionState::Disconnected);
        socket->deleteLater();
        return;
    }

    emit errorOccurred(message);
    setState(ConnectionState::Error, message);
    socket->abort();
    socket->deleteLater();
}

void TransportWorker::openTcpServer(const TransportConfig& config)
{
    tcpServer_ = new QTcpServer(this);
    connect(tcpServer_, &QTcpServer::newConnection, this, &TransportWorker::onTcpServerNewConnection);
    connect(tcpServer_, &QTcpServer::acceptError, this, &TransportWorker::onTcpServerAcceptError);

    if (!tcpServer_->listen(QHostAddress::Any, config.tcpServer.listenPort)) {
        const auto error = tcpServer_->errorString();
        tcpServer_->deleteLater();
        tcpServer_ = nullptr;
        setState(ConnectionState::Error, error);
        return;
    }

    setState(ConnectionState::Connected);
    emit listening(tcpServer_->serverPort());
    emitClients();
}

void TransportWorker::onTcpServerNewConnection()
{
    if (tcpServer_ == nullptr)
        return;

    while (tcpServer_->hasPendingConnections()) {
        auto* socket = tcpServer_->nextPendingConnection();
        if (socket == nullptr)
            continue;

        const auto id = nextClientId_++;
        clients_.insert(id, socket);
        socket->setProperty("vofaClientId", QVariant::fromValue<qulonglong>(id));
        connect(socket, &QTcpSocket::readyRead, this, &TransportWorker::onTcpServerReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &TransportWorker::onTcpServerDisconnected);

        if (activeClientId_ == 0) {
            activeClientId_ = id;
            ++generation_;
            resetParser();
            emit streamReset(generation_);
        }
    }

    emitClients();
}

void TransportWorker::onTcpServerReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket == nullptr)
        return;

    const auto id = clientIdForSocket(socket);
    if (id == 0)
        return;

    processIncoming(id, socket->readAll(), id == activeClientId_);
}

void TransportWorker::onTcpServerDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket == nullptr)
        return;

    const auto id = clientIdForSocket(socket);
    if (id == 0)
        return;

    clients_.remove(id);
    socket->deleteLater();

    if (activeClientId_ == id) {
        activeClientId_ = clients_.isEmpty() ? 0 : clients_.constBegin().key();
        ++generation_;
        resetParser();
        emit streamReset(generation_);
    }

    emitClients();
}

void TransportWorker::onTcpServerAcceptError()
{
    if (tcpServer_ == nullptr)
        return;
    emit errorOccurred(tcpServer_->errorString());
}

void TransportWorker::openUdp(const TransportConfig& config)
{
    udp_ = new QUdpSocket(this);
    connect(udp_, &QUdpSocket::readyRead, this, &TransportWorker::onUdpReadyRead);

    if (!udp_->bind(QHostAddress::AnyIPv4, config.udp.localPort)) {
        const auto error = udp_->errorString();
        udp_->deleteLater();
        udp_ = nullptr;
        setState(ConnectionState::Error, error);
        return;
    }

    setState(ConnectionState::Connected);
    emit listening(udp_->localPort());
}

void TransportWorker::openDemo()
{
    // Synthetic 4-channel source for UI/perf testing without hardware.
    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(10);
    connect(demoTimer_, &QTimer::timeout, this, &TransportWorker::onDemoTick);
    demoClock_.start();
    demoTimer_->start();
    setState(ConnectionState::Connected);
    emitClients();
}

void TransportWorker::onDemoTick()
{
    const double t = static_cast<double>(demoClock_.elapsed()) / 1000.0;
    const std::array<float, 4> values {
        static_cast<float>(std::sin(t)),
        static_cast<float>(std::sin(2.0 * t) * 0.6),
        static_cast<float>(std::cos(1.3 * t) * 2.0),
        static_cast<float>(std::sin(0.25 * t) * 3.0),
    };
    if (store_ != nullptr)
        (void)store_->pushFrame(values, nowNs());
}

void TransportWorker::onUdpReadyRead()
{
    if (udp_ == nullptr)
        return;

    while (udp_->hasPendingDatagrams()) {
        const auto size = udp_->pendingDatagramSize();
        if (size <= 0)
            continue;

        QByteArray data;
        data.resize(static_cast<int>(size));
        const auto read = udp_->readDatagram(data.data(), data.size());
        if (read <= 0)
            continue;
        if (read < data.size())
            data.truncate(static_cast<int>(read));
        processIncoming(0, data, true);
    }
}

void TransportWorker::send(const QByteArray& data)
{
    sendBytes(activeClientId_, data, true);
}

void TransportWorker::sendTo(quint64 clientId, const QByteArray& data)
{
    sendBytes(clientId, data, false);
}

void TransportWorker::setActiveClient(quint64 clientId)
{
    if (clientId != 0 && !clients_.contains(clientId)) {
        emit sendFailed(QStringLiteral("client %1 is not connected").arg(clientId));
        return;
    }
    if (activeClientId_ == clientId)
        return;

    activeClientId_ = clientId;
    ++generation_;
    resetParser();
    emit streamReset(generation_);
    emitClients();
}

void TransportWorker::processIncoming(quint64 clientId, const QByteArray& data, bool feedParser)
{
    if (data.isEmpty())
        return;

    emit received(generation_, clientId, data);

    if (!feedParser || !parser_ || !sink_)
        return;

    parser_->feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.constData()),
                                   static_cast<std::size_t>(data.size())),
        *sink_,
        nowNs());
}

void TransportWorker::sendBytes(quint64 clientId, const QByteArray& data, bool toActive)
{
    if (data.isEmpty())
        return;

    if (serial_ != nullptr && serial_->isOpen()) {
        if (serial_->write(data) < 0)
            emit sendFailed(serial_->errorString());
        return;
    }

    if (tcpClient_ != nullptr && tcpClient_->state() == QAbstractSocket::ConnectedState) {
        if (!checkBackpressure(tcpClient_)) {
            emit sendFailed(QStringLiteral("link busy"));
            return;
        }
        if (tcpClient_->write(data) < 0)
            emit sendFailed(tcpClient_->errorString());
        return;
    }

    if (tcpServer_ != nullptr) {
        const auto id = toActive ? activeClientId_ : clientId;
        if (id == 0) {
            emit sendFailed(QStringLiteral("no active client"));
            return;
        }
        auto* socket = clients_.value(id, nullptr);
        if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState) {
            emit sendFailed(QStringLiteral("client %1 is not connected").arg(id));
            return;
        }
        if (!checkBackpressure(socket)) {
            emit sendFailed(QStringLiteral("link busy"));
            return;
        }
        if (socket->write(data) < 0)
            emit sendFailed(socket->errorString());
        return;
    }

    if (udp_ != nullptr) {
        const QHostAddress remote(config_.udp.remoteHost);
        if (remote.isNull()) {
            emit sendFailed(QStringLiteral("invalid UDP remote host"));
            return;
        }
        if (udp_->writeDatagram(data, remote, config_.udp.remotePort) < 0)
            emit sendFailed(udp_->errorString());
        return;
    }

    emit sendFailed(QStringLiteral("transport is not open"));
}

void TransportWorker::resetParser()
{
    if (parser_)
        parser_->reset();
}

void TransportWorker::closeAll()
{
    if (serial_ != nullptr) {
        serial_->close();
        delete serial_;
        serial_ = nullptr;
    }

    if (tcpClient_ != nullptr) {
        tcpClient_->abort();
        delete tcpClient_;
        tcpClient_ = nullptr;
    }

    if (tcpServer_ != nullptr) {
        tcpServer_->close();
        for (auto* socket : std::as_const(clients_)) {
            socket->abort();
            delete socket;
        }
        clients_.clear();
        delete tcpServer_;
        tcpServer_ = nullptr;
    }

    if (udp_ != nullptr) {
        udp_->close();
        delete udp_;
        udp_ = nullptr;
    }

    if (demoTimer_ != nullptr) {
        demoTimer_->stop();
        delete demoTimer_;
        demoTimer_ = nullptr;
    }

    activeClientId_ = 0;
}

void TransportWorker::emitClients()
{
    auto ids = clients_.keys();
    std::sort(ids.begin(), ids.end());

    QStringList labels;
    labels.reserve(ids.size());
    for (const auto id : ids) {
        auto* socket = clients_.value(id, nullptr);
        if (socket == nullptr) {
            labels.push_back(QStringLiteral("client %1").arg(id));
        } else {
            labels.push_back(QStringLiteral("%1:%2 (client %3)")
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort())
                                 .arg(id));
        }
    }

    emit clientsChanged(ids, labels, activeClientId_);
}

void TransportWorker::setState(ConnectionState state, const QString& error)
{
    emit stateChanged(state, error);
}

quint64 TransportWorker::clientIdForSocket(QTcpSocket* socket) const
{
    if (socket == nullptr)
        return 0;

    bool ok = false;
    const auto id = socket->property("vofaClientId").toULongLong(&ok);
    if (ok && clients_.contains(id))
        return id;

    for (auto it = clients_.constBegin(); it != clients_.constEnd(); ++it) {
        if (it.value() == socket)
            return it.key();
    }
    return 0;
}

bool TransportWorker::checkBackpressure(QTcpSocket* socket) const
{
    return socket != nullptr
        && static_cast<std::uint64_t>(socket->bytesToWrite()) <= limits::kMaxPendingSendBytes;
}

} // namespace vofa::transport
