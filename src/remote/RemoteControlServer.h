#pragma once

#include "RemoteProtocol.h"
#include "WebSocketConnection.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QTcpServer;
class QUdpSocket;
class QTimer;

namespace vofa::remote {

// LAN endpoint for mobile tuning. Owns the TCP listener, the UDP discovery
// responder and the client registry. It is transport only: every accepted
// write is forwarded as a signal so the application stays the single source of
// truth for tuning values.
class RemoteControlServer final : public QObject {
    Q_OBJECT

public:
    struct Client {
        quint64 session = 0;
        QString identity;
        QString name;
        QString address;
        qint64 connectedAtMs = 0;
        qint64 lastRxMs = 0;
        bool greeted = false;
        // Token bucket, so a flood of garbage cannot saturate the UI thread.
        double tokens = 0.0;
        qint64 lastRefillMs = 0;
        WebSocketConnection* connection = nullptr;
    };

    explicit RemoteControlServer(QObject* parent = nullptr);
    ~RemoteControlServer() override;

    // `port` 0 picks an ephemeral port (used by tests). Discovery uses the
    // fixed kDiscoveryPort; failing to bind it is not fatal.
    bool start(quint16 port, const QString& name, const QString& pin, QString* error);
    void stop();

    [[nodiscard]] bool running() const;
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] QString serverName() const { return name_; }
    void setServerName(const QString& name);
    [[nodiscard]] bool pinRequired() const { return !pin_.isEmpty(); }
    void setPin(const QString& pin) { pin_ = pin.trimmed().left(32); }

    [[nodiscard]] int clientCount() const;
    // Connections that completed the hello handshake (what the UI reports).
    [[nodiscard]] int greetedCount() const;
    [[nodiscard]] QList<Client> clients() const;
    // "" when the session does not exist.
    [[nodiscard]] QString clientAddress(quint64 session) const;
    void kick(quint64 session, const QString& reason = QStringLiteral("已被电脑端断开"));

    // IPv4 addresses a phone can reach, loopback excluded.
    [[nodiscard]] QStringList localAddresses() const;
    [[nodiscard]] static QString versionString();

    void broadcastState(const QJsonObject& state);
    void sendState(quint64 session, const QJsonObject& state);
    void broadcastValue(quint64 controlId, double value, const QString& from);
    void broadcastAction(quint64 actionId,
                         int kind,
                         bool on,
                         const QString& from,
                         bool flash);
    void broadcastOneClick(const QJsonObject& message);
    void broadcastConn(const QJsonObject& message);
    void broadcastToAll(const QJsonObject& message);
    void sendError(quint64 session, const QString& code, const QString& message);

signals:
    void clientsChanged();
    void stateRequested(quint64 session);
    void valueRequested(quint64 session,
                        quint64 controlId,
                        double value,
                        bool commit,
                        bool exact);
    void actionRequested(quint64 session, quint64 actionId, bool on, bool trigger);
    // Phone toggled "参与一键发送数据" for a slider.
    void inclusionRequested(quint64 session, quint64 controlId, bool included);
    void oneClickRequested(quint64 session, const QString& group, int intervalMs);
    void oneClickStopRequested(quint64 session);
    void logLine(const QString& text);

private:
    void onNewConnection();
    void onClientMessage(quint64 session, const QByteArray& payload);
    void onClientClosed(quint64 session);
    void onHeartbeat();
    void onDiscoveryDatagram();
    void logThrottled(const QString& text);
    void sendHelloError(WebSocketConnection* connection,
                        const QString& code,
                        const QString& message);
    [[nodiscard]] Client* findClient(quint64 session);
    [[nodiscard]] const Client* findClient(quint64 session) const;

    std::vector<std::unique_ptr<Client>> clients_;
    QTcpServer* server_ = nullptr;
    QUdpSocket* discovery_ = nullptr;
    QTimer* heartbeat_ = nullptr;
    QString name_;
    QString pin_;
    quint64 nextSession_ = 1;
    quint64 stateSeq_ = 0;
    // Interface enumeration is not free; discovery probes are throttled to one
    // refresh every couple of seconds.
    mutable QStringList addressCache_;
    mutable qint64 addressCacheMs_ = 0;
    qint64 lastDiscoveryReplyMs_ = 0;
    qint64 lastLogMs_ = 0;
    int suppressedLogs_ = 0;
};

} // namespace vofa::remote
