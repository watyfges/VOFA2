#include "WebSocketConnection.h"

#include "RemoteProtocol.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

namespace vofa::remote {
namespace {

constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr int kMaxHandshakeBytes = 8 * 1024;

enum Opcode : quint8 {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

quint64 readBigEndian(const quint8* data, int bytes)
{
    quint64 value = 0;
    for (int i = 0; i < bytes; ++i)
        value = (value << 8) | data[i];
    return value;
}

void appendBigEndian(QByteArray& out, quint64 value, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.append(static_cast<char>((value >> (8 * i)) & 0xff));
}

} // namespace

WebSocketConnection::WebSocketConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , socket_(socket)
{
    socket_->setParent(this);
    connect(socket_, &QTcpSocket::readyRead, this, &WebSocketConnection::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &WebSocketConnection::onDisconnected);

    // The first request may already be buffered before the readyRead connection
    // is in place; drain it once the event loop turns.
    if (socket_->bytesAvailable() > 0) {
        QTimer::singleShot(0, this, [this]() {
            if (state_ != State::Closed)
                onReadyRead();
        });
    }
}

QHostAddress WebSocketConnection::peerAddress() const
{
    return socket_ != nullptr ? socket_->peerAddress() : QHostAddress();
}

quint16 WebSocketConnection::peerPort() const
{
    return socket_ != nullptr ? socket_->peerPort() : 0;
}

void WebSocketConnection::onReadyRead()
{
    if (state_ == State::Closed || socket_ == nullptr)
        return;

    buffer_.append(socket_->readAll());

    if (state_ == State::Handshake && !performHandshake())
        return;

    if (state_ == State::Open && !consumeFrames())
        return;
}

void WebSocketConnection::onDisconnected()
{
    state_ = State::Closed;
    if (closedEmitted_)
        return;
    closedEmitted_ = true;
    emit closed();
}

bool WebSocketConnection::performHandshake()
{
    const int headerEnd = buffer_.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer_.size() > kMaxHandshakeBytes)
            fail(1002, QStringLiteral("握手报文过长"));
        return false;
    }

    const QByteArray header = buffer_.left(headerEnd);
    buffer_.remove(0, headerEnd + 4);

    const auto lines = header.split('\n');
    QHash<QByteArray, QByteArray> fields;
    for (int i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        fields.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    const auto upgrade = fields.value(QByteArrayLiteral("upgrade")).toLower();
    const auto connection = fields.value(QByteArrayLiteral("connection")).toLower();
    const auto key = fields.value(QByteArrayLiteral("sec-websocket-key"));
    const auto version = fields.value(QByteArrayLiteral("sec-websocket-version"));

    if (key.isEmpty() || !upgrade.contains("websocket") || !connection.contains("upgrade")) {
        // Plain HTTP request: answer with a tiny liveness document so a phone
        // or a browser can verify that the server is reachable.
        replyHttp("HTTP/1.1 200 OK", "application/json",
                  "{\"app\":\"vofa2\",\"remote\":\"websocket\",\"proto\":1}");
        return false;
    }

    if (version != "13") {
        replyHttp("HTTP/1.1 426 Upgrade Required", "text/plain",
                  "unsupported websocket version");
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(key + kWebSocketGuid);
    const auto accept = hash.result().toBase64();

    QByteArray response = "HTTP/1.1 101 Switching Protocols\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: ";
    response += accept;
    response += "\r\n\r\n";
    socket_->write(response);
    socket_->flush();

    state_ = State::Open;
    emit opened();
    return true;
}

void WebSocketConnection::replyHttp(const QByteArray& statusLine,
                                    const QByteArray& contentType,
                                    const QByteArray& body)
{
    QByteArray response = statusLine;
    response += "\r\nContent-Type: ";
    response += contentType;
    response += "\r\nContent-Length: ";
    response += QByteArray::number(body.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body;
    socket_->write(response);
    socket_->flush();
    state_ = State::Closed;
    socket_->disconnectFromHost();
}

bool WebSocketConnection::consumeFrames()
{
    while (state_ == State::Open) {
        if (buffer_.size() < 2)
            return true;

        const auto* data = reinterpret_cast<const quint8*>(buffer_.constData());
        const bool fin = (data[0] & 0x80) != 0;
        const quint8 opcode = data[0] & 0x0f;
        const bool masked = (data[1] & 0x80) != 0;
        quint64 length = data[1] & 0x7f;
        int offset = 2;

        if (length == 126) {
            if (buffer_.size() < 4)
                return true;
            length = readBigEndian(data + 2, 2);
            offset = 4;
        } else if (length == 127) {
            if (buffer_.size() < 10)
                return true;
            length = readBigEndian(data + 2, 8);
            offset = 10;
        }

        // Control frames may be interleaved and are never fragmented.
        if (opcode >= kClose && (!fin || length > 125)) {
            fail(1002, QStringLiteral("控制帧格式错误"));
            return false;
        }
        if (length > static_cast<quint64>(kMaxMessageBytes)) {
            fail(1009, QStringLiteral("消息超过上限"));
            return false;
        }

        // Clients must mask (RFC 6455 §5.1); unmasking is only done for them.
        if (!masked) {
            fail(1002, QStringLiteral("客户端帧未加掩码"));
            return false;
        }

        const int maskOffset = offset;
        const auto total = static_cast<qsizetype>(offset + 4 + length);
        if (buffer_.size() < total)
            return true;

        QByteArray payload = buffer_.mid(offset + 4, static_cast<qsizetype>(length));
        const auto* mask = data + maskOffset;
        for (qsizetype i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);

        buffer_.remove(0, total);
        emit activity();

        if (opcode == kClose) {
            sendFrame(kClose, payload);
            state_ = State::Closed;
            socket_->flush();
            socket_->disconnectFromHost();
            return false;
        }
        if (opcode == kPing) {
            sendFrame(kPong, payload);
            continue;
        }
        if (opcode == kPong)
            continue;

        if (opcode == kText || opcode == kBinary || opcode == kContinuation) {
            if (!handleDataFrame(opcode, fin, payload))
                return false;
            continue;
        }

        fail(1003, QStringLiteral("不支持的帧类型"));
        return false;
    }
    return state_ == State::Open;
}

bool WebSocketConnection::handleDataFrame(quint8 opcode, bool fin, const QByteArray& payload)
{
    if (opcode == kBinary) {
        fail(1003, QStringLiteral("只接受文本帧"));
        return false;
    }

    if (opcode == kContinuation) {
        if (fragmentOpcode_ == 0) {
            fail(1002, QStringLiteral("续帧缺少起始帧"));
            return false;
        }
        fragment_.append(payload);
        if (fragment_.size() > kMaxMessageBytes) {
            fail(1009, QStringLiteral("消息超过上限"));
            return false;
        }
        if (!fin)
            return true;

        const auto complete = fragment_;
        fragment_.clear();
        fragmentOpcode_ = 0;
        emit textMessage(complete);
        return true;
    }

    if (!fin) {
        if (!fragment_.isEmpty()) {
            fail(1002, QStringLiteral("分片状态错误"));
            return false;
        }
        fragmentOpcode_ = opcode;
        fragment_ = payload;
        return true;
    }

    emit textMessage(payload);
    return true;
}

void WebSocketConnection::sendFrame(quint8 opcode, const QByteArray& payload)
{
    if (socket_ == nullptr || state_ == State::Closed)
        return;

    QByteArray frame;
    frame.reserve(payload.size() + 10);
    frame.append(static_cast<char>(0x80 | opcode));

    const auto size = static_cast<quint64>(payload.size());
    if (size < 126) {
        frame.append(static_cast<char>(size));
    } else if (size <= 0xffff) {
        frame.append(static_cast<char>(126));
        appendBigEndian(frame, size, 2);
    } else {
        frame.append(static_cast<char>(127));
        appendBigEndian(frame, size, 8);
    }

    frame.append(payload);
    socket_->write(frame);
}

void WebSocketConnection::sendText(const QByteArray& payload)
{
    if (state_ != State::Open)
        return;
    // A phone that stopped reading (screen off, Wi-Fi gone) must not make the
    // server buffer indefinitely.
    if (socket_->bytesToWrite() > kMaxSendBacklogBytes) {
        close(1011, QStringLiteral("send backlog"));
        return;
    }
    sendFrame(kText, payload);
}

void WebSocketConnection::sendJson(const QJsonObject& object)
{
    sendText(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void WebSocketConnection::ping()
{
    if (state_ == State::Open)
        sendFrame(kPing, QByteArray());
}

void WebSocketConnection::close(quint16 code, const QString& reason)
{
    if (state_ == State::Closed)
        return;

    QByteArray payload;
    payload.append(static_cast<char>((code >> 8) & 0xff));
    payload.append(static_cast<char>(code & 0xff));
    payload.append(reason.toUtf8());

    sendFrame(kClose, payload);
    socket_->flush();
    state_ = State::Closed;
    socket_->disconnectFromHost();
}

void WebSocketConnection::fail(quint16 code, const QString& reason)
{
    close(code, reason);
}

} // namespace vofa::remote
