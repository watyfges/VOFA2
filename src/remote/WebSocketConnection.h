#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

class QJsonObject;
class QTcpSocket;

namespace vofa::remote {

// Server side of a single RFC 6455 connection: handshake, text frames,
// fragmentation, ping/pong and close. The Qt WebSockets module is not part of
// every Qt installation, and the control channel only needs text JSON, so the
// used subset is implemented here instead of adding a hard dependency.
class WebSocketConnection final : public QObject {
    Q_OBJECT

public:
    // Takes ownership of `socket`.
    explicit WebSocketConnection(QTcpSocket* socket, QObject* parent = nullptr);

    void sendText(const QByteArray& payload);
    void sendJson(const QJsonObject& object);
    void ping();
    void close(quint16 code = 1000, const QString& reason = QString());

    [[nodiscard]] QHostAddress peerAddress() const;
    [[nodiscard]] quint16 peerPort() const;
    [[nodiscard]] bool isOpen() const { return state_ == State::Open; }

signals:
    void opened();
    void textMessage(const QByteArray& payload);
    // Any well-formed inbound frame, including pong: used as a liveness tick.
    void activity();
    void closed();

private:
    enum class State {
        Handshake,
        Open,
        Closed,
    };

    void onReadyRead();
    void onDisconnected();
    // Returns true once the socket has been upgraded to WebSocket.
    bool performHandshake();
    void replyHttp(const QByteArray& statusLine,
                   const QByteArray& contentType,
                   const QByteArray& body);
    bool consumeFrames();
    bool handleDataFrame(quint8 opcode, bool fin, const QByteArray& payload);
    void sendFrame(quint8 opcode, const QByteArray& payload);
    void fail(quint16 code, const QString& reason);

    QTcpSocket* socket_ = nullptr;
    State state_ = State::Handshake;
    QByteArray buffer_;
    QByteArray fragment_;
    quint8 fragmentOpcode_ = 0;
    bool closedEmitted_ = false;
};

} // namespace vofa::remote
