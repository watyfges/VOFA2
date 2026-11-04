#include "RemoteControlServer.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace vofa::remote {

RemoteControlServer::RemoteControlServer(QObject* parent)
    : QObject(parent)
    , name_(QStringLiteral("VOFA2"))
{
    heartbeat_ = new QTimer(this);
    heartbeat_->setInterval(kHeartbeatMs);
    connect(heartbeat_, &QTimer::timeout, this, &RemoteControlServer::onHeartbeat);
}

RemoteControlServer::~RemoteControlServer()
{
    stop();
}

QString RemoteControlServer::versionString()
{
    return QString::fromLatin1(kServerVersion);
}

bool RemoteControlServer::running() const
{
    return server_ != nullptr && server_->isListening();
}

quint16 RemoteControlServer::port() const
{
    return server_ != nullptr ? server_->serverPort() : 0;
}

void RemoteControlServer::setServerName(const QString& name)
{
    const auto cleaned = name.trimmed().left(64);
    if (cleaned.isEmpty() || cleaned == name_)
        return;
    name_ = cleaned;
}

bool RemoteControlServer::start(quint16 port,
                                const QString& name,
                                const QString& pin,
                                QString* error)
{
    stop();

    name_ = name.trimmed().left(64);
    if (name_.isEmpty())
        name_ = QStringLiteral("VOFA2");
    pin_ = pin.trimmed().left(32);

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &RemoteControlServer::onNewConnection);
    // IPv4 only: the discovery probe is an IPv4 broadcast, and plain IPv4
    // peer addresses are what the PC dialog shows to the user.
    if (!server_->listen(QHostAddress::AnyIPv4, port)) {
        if (error != nullptr) {
            *error = tr("端口 %1 监听失败：%2").arg(port).arg(server_->errorString());
        }
        delete server_;
        server_ = nullptr;
        return false;
    }

    // Discovery is a convenience: a failed bind only costs automatic lookup.
    discovery_ = new QUdpSocket(this);
    if (discovery_->bind(QHostAddress::AnyIPv4,
                         kDiscoveryPort,
                         QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(discovery_, &QUdpSocket::readyRead,
                this, &RemoteControlServer::onDiscoveryDatagram);
    } else {
        emit logLine(tr("UDP 发现端口 %1 绑定失败，手机端需要手动输入地址")
                         .arg(kDiscoveryPort));
        delete discovery_;
        discovery_ = nullptr;
    }

    heartbeat_->start();
    emit logLine(tr("远程调参服务已启动：%1:%2").arg(name_).arg(server_->serverPort()));
    emit clientsChanged();
    return true;
}

void RemoteControlServer::stop()
{
    if (heartbeat_ != nullptr)
        heartbeat_->stop();

    for (auto& client : clients_) {
        if (client->connection != nullptr) {
            client->connection->disconnect(this);
            client->connection->close(1001, QStringLiteral("server stopped"));
            client->connection->deleteLater();
            client->connection = nullptr;
        }
    }
    const bool hadClients = !clients_.empty();
    clients_.clear();

    if (discovery_ != nullptr) {
        discovery_->close();
        delete discovery_;
        discovery_ = nullptr;
    }
    if (server_ != nullptr) {
        server_->close();
        delete server_;
        server_ = nullptr;
    }
    if (hadClients)
        emit clientsChanged();
}

int RemoteControlServer::clientCount() const
{
    return static_cast<int>(clients_.size());
}

int RemoteControlServer::greetedCount() const
{
    int count = 0;
    for (const auto& client : clients_) {
        if (client->greeted)
            ++count;
    }
    return count;
}

QList<RemoteControlServer::Client> RemoteControlServer::clients() const
{
    QList<Client> result;
    result.reserve(static_cast<qsizetype>(clients_.size()));
    for (const auto& client : clients_)
        result.push_back(*client);
    return result;
}

QString RemoteControlServer::clientAddress(quint64 session) const
{
    const auto* client = findClient(session);
    return client != nullptr ? client->address : QString();
}

RemoteControlServer::Client* RemoteControlServer::findClient(quint64 session)
{
    const auto it = std::find_if(clients_.begin(), clients_.end(),
                                 [session](const auto& client) {
                                     return client->session == session;
                                 });
    return it != clients_.end() ? it->get() : nullptr;
}

const RemoteControlServer::Client* RemoteControlServer::findClient(quint64 session) const
{
    const auto it = std::find_if(clients_.begin(), clients_.end(),
                                 [session](const auto& client) {
                                     return client->session == session;
                                 });
    return it != clients_.end() ? it->get() : nullptr;
}

void RemoteControlServer::kick(quint64 session, const QString& reason)
{
    auto* client = findClient(session);
    if (client == nullptr || client->connection == nullptr)
        return;
    client->connection->close(4001, reason);
}

void RemoteControlServer::onNewConnection()
{
    // Bounded per event-loop turn: a SYN flood must not monopolise the thread
    // that also refreshes the plot.
    int budget = 32;
    while (server_ != nullptr && server_->hasPendingConnections() && budget-- > 0) {
        auto* socket = server_->nextPendingConnection();
        if (socket == nullptr)
            continue;

        const auto address = socket->peerAddress().toString();
        int sameAddress = 0;
        for (const auto& existing : clients_) {
            if (existing->address == address)
                ++sameAddress;
        }

        const auto reject = [socket](const QByteArray& body) {
            socket->write("HTTP/1.1 503 Service Unavailable\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            socket->deleteLater();
        };

        if (static_cast<int>(clients_.size()) >= kMaxClients) {
            reject("too many clients");
            emit logLine(tr("连接被拒绝：客户端数量已达上限"));
            continue;
        }
        if (sameAddress >= kMaxClientsPerAddress) {
            reject("too many connections from this address");
            emit logLine(tr("连接被拒绝：同一地址连接过多（%1）").arg(address));
            continue;
        }

        auto client = std::make_unique<Client>();
        client->session = nextSession_++;
        client->address = address;
        client->connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        client->lastRxMs = client->connectedAtMs;
        client->tokens = kClientMessageBurst;
        client->lastRefillMs = client->connectedAtMs;
        client->connection = new WebSocketConnection(socket, this);

        const auto session = client->session;
        connect(client->connection, &WebSocketConnection::textMessage, this,
                [this, session](const QByteArray& payload) {
                    onClientMessage(session, payload);
                });
        connect(client->connection, &WebSocketConnection::activity, this,
                [this, session]() {
                    if (auto* entry = findClient(session))
                        entry->lastRxMs = QDateTime::currentMSecsSinceEpoch();
                });
        connect(client->connection, &WebSocketConnection::closed, this,
                [this, session]() { onClientClosed(session); });

        logThrottled(tr("新的远程连接：%1").arg(client->address));
        clients_.push_back(std::move(client));
        emit clientsChanged();
    }

    // Hit the per-turn budget: come back once the UI has had a chance to
    // breathe, otherwise the backlog would wait for the next arrival.
    if (server_ != nullptr && server_->hasPendingConnections()) {
        QTimer::singleShot(0, this, &RemoteControlServer::onNewConnection);
    }
}

// Connection storms (or a hostile client) must not turn into thousands of
// synchronous log-file writes on the UI thread.
void RemoteControlServer::logThrottled(const QString& text)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastLogMs_ < 250) {
        ++suppressedLogs_;
        return;
    }
    lastLogMs_ = now;
    if (suppressedLogs_ > 0) {
        emit logLine(tr("%1（另省略 %2 条连接日志）").arg(text).arg(suppressedLogs_));
        suppressedLogs_ = 0;
        return;
    }
    emit logLine(text);
}

void RemoteControlServer::onClientMessage(quint64 session, const QByteArray& payload)
{
    auto* client = findClient(session);
    if (client == nullptr || client->connection == nullptr)
        return;

    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto elapsed = std::max<qint64>(0, now - client->lastRefillMs);
    client->tokens = std::min(kClientMessageBurst,
                              client->tokens
                                  + static_cast<double>(elapsed) * kClientMessagesPerSecond
                                      / 1000.0);
    client->lastRefillMs = now;

    // Large messages cost proportionally more: parsing a 64 KB frame is far
    // more expensive than parsing a slider update, and the UI thread that also
    // drives the serial link must not be starved by a flood.
    const auto cost = 1.0 + static_cast<double>(payload.size()) / 1024.0;
    if (client->tokens < cost) {
        client->connection->close(1008, QStringLiteral("rate limit"));
        return;
    }
    client->tokens -= cost;
    client->lastRxMs = now;

    const auto message = parseClientMessage(payload);
    if (message.type == ClientMessageType::Unknown) {
        sendError(session, QStringLiteral("bad_request"), message.error);
        return;
    }

    if (!client->greeted) {
        if (message.type != ClientMessageType::Hello) {
            sendError(session, QStringLiteral("hello_required"),
                      tr("请先发送 hello 消息"));
            return;
        }
        if (pinRequired() && message.pin != pin_) {
            sendHelloError(client->connection, QStringLiteral("bad_pin"),
                           tr("访问码错误"));
            return;
        }

        client->greeted = true;
        client->identity = message.identity;
        client->name = message.name.trimmed().isEmpty()
            ? tr("手机 %1").arg(session)
            : message.name.trimmed().left(64);
        client->connection->sendJson(
            welcomeMessage(session, name_, versionString(), pinRequired()));
        emit logLine(tr("手机已连接：%1（%2）").arg(client->name, client->address));
        emit clientsChanged();
        emit stateRequested(session);
        return;
    }

    switch (message.type) {
    case ClientMessageType::Hello:
        break;
    case ClientMessageType::Value:
        emit valueRequested(session, message.id, message.value, false, false);
        break;
    case ClientMessageType::Commit:
        emit valueRequested(session, message.id, message.value, true, message.exact);
        break;
    case ClientMessageType::Action:
        emit actionRequested(session, message.id, message.on, message.trigger);
        break;
    case ClientMessageType::OneClick:
        emit oneClickRequested(session, message.group, message.intervalMs);
        break;
    case ClientMessageType::OneClickStop:
        emit oneClickStopRequested(session);
        break;
    case ClientMessageType::Sync:
        emit stateRequested(session);
        break;
    case ClientMessageType::Ping:
        client->connection->sendJson(pongMessage());
        break;
    case ClientMessageType::Unknown:
        break;
    }
}

void RemoteControlServer::onClientClosed(quint64 session)
{
    const auto it = std::find_if(clients_.begin(), clients_.end(),
                                 [session](const auto& client) {
                                     return client->session == session;
                                 });
    if (it == clients_.end())
        return;

    auto* connection = (*it)->connection;
    const auto address = (*it)->address;
    clients_.erase(it);
    if (connection != nullptr)
        connection->deleteLater();
    logThrottled(tr("远程连接已断开：%1").arg(address));
    emit clientsChanged();
}

void RemoteControlServer::sendHelloError(WebSocketConnection* connection,
                                         const QString& code,
                                         const QString& message)
{
    if (connection == nullptr)
        return;
    connection->sendJson(errorMessage(code, message));
    connection->close(4003, message);
}

void RemoteControlServer::onHeartbeat()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();

    // close() may disconnect synchronously, which erases the entry from
    // clients_. Collect first, then act, so the vector is never mutated while
    // it is being walked.
    QVector<quint64> expired;
    expired.reserve(static_cast<qsizetype>(clients_.size()));
    for (const auto& client : clients_) {
        if (client->connection == nullptr || !client->connection->isOpen())
            continue;
        if (!client->greeted) {
            if (now - client->connectedAtMs > kHelloTimeoutMs)
                expired.push_back(client->session);
            continue;
        }
        if (now - client->lastRxMs > kClientTimeoutMs) {
            expired.push_back(client->session);
            continue;
        }
        client->connection->ping();
    }

    for (const auto session : expired) {
        auto* client = findClient(session);
        if (client != nullptr && client->connection != nullptr)
            client->connection->close(1001, QStringLiteral("timeout"));
    }
}

void RemoteControlServer::onDiscoveryDatagram()
{
    int budget = 32;
    while (discovery_ != nullptr && discovery_->hasPendingDatagrams() && budget-- > 0) {
        QByteArray datagram;
        datagram.resize(static_cast<qsizetype>(discovery_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const auto read = discovery_->readDatagram(datagram.data(), datagram.size(),
                                                   &sender, &senderPort);
        if (read <= 0 || !isDiscoveryProbe(datagram))
            continue;

        // A phone scans with a handful of probes; anything beyond ~200 replies
        // per second is a flood, and dropping it keeps the UI smooth.
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastDiscoveryReplyMs_ < 5)
            continue;
        lastDiscoveryReplyMs_ = now;

        const auto offer = discoveryOffer(name_, port(), pinRequired(),
                                          localAddresses(), versionString());
        const auto payload = QJsonDocument(offer).toJson(QJsonDocument::Compact);
        discovery_->writeDatagram(payload, sender, senderPort);
    }

    if (discovery_ != nullptr && discovery_->hasPendingDatagrams()) {
        QTimer::singleShot(0, this, &RemoteControlServer::onDiscoveryDatagram);
    }
}

QStringList RemoteControlServer::localAddresses() const
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - addressCacheMs_ < 2000)
        return addressCache_;

    QStringList result;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto& entry : iface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol
                || address.isLoopback()) {
                continue;
            }
            const auto text = address.toString();
            if (!result.contains(text))
                result.push_back(text);
        }
    }
    result.sort();

    addressCache_ = result;
    addressCacheMs_ = now;
    return result;
}

void RemoteControlServer::broadcastToAll(const QJsonObject& message)
{
    const auto payload = QJsonDocument(message).toJson(QJsonDocument::Compact);

    // Same reason as onHeartbeat(): sending can end in a disconnect, and a
    // disconnect removes the client from the vector.
    QVector<quint64> sessions;
    sessions.reserve(static_cast<qsizetype>(clients_.size()));
    for (const auto& client : clients_) {
        if (client->greeted && client->connection != nullptr)
            sessions.push_back(client->session);
    }

    for (const auto session : sessions) {
        auto* client = findClient(session);
        if (client != nullptr && client->connection != nullptr)
            client->connection->sendText(payload);
    }
}

void RemoteControlServer::broadcastState(const QJsonObject& state)
{
    QJsonObject payload = state;
    payload.insert(QStringLiteral("seq"), static_cast<double>(++stateSeq_));
    broadcastToAll(payload);
}

void RemoteControlServer::sendState(quint64 session, const QJsonObject& state)
{
    auto* client = findClient(session);
    if (client == nullptr || client->connection == nullptr)
        return;
    QJsonObject payload = state;
    payload.insert(QStringLiteral("seq"), static_cast<double>(stateSeq_));
    client->connection->sendJson(payload);
}

void RemoteControlServer::broadcastValue(quint64 controlId, double value, const QString& from)
{
    broadcastToAll(valueMessage(controlId, value, from));
}

void RemoteControlServer::broadcastAction(quint64 actionId,
                                          int kind,
                                          bool on,
                                          const QString& from,
                                          bool flash)
{
    broadcastToAll(actionMessage(actionId, kind, on, from, flash));
}

void RemoteControlServer::broadcastOneClick(const QJsonObject& message)
{
    broadcastToAll(message);
}

void RemoteControlServer::broadcastConn(const QJsonObject& message)
{
    broadcastToAll(message);
}

void RemoteControlServer::sendError(quint64 session,
                                    const QString& code,
                                    const QString& message)
{
    auto* client = findClient(session);
    if (client == nullptr || client->connection == nullptr)
        return;
    client->connection->sendJson(errorMessage(code, message));
}

} // namespace vofa::remote
