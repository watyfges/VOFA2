#pragma once

// Shared helpers for the remote-tuning tests: a dependency-free WebSocket
// client that can also send deliberately malformed traffic.

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

namespace remotetest {

constexpr char kHandshakeKey[] = "dGhlIHNhbXBsZSBub25jZQ==";
// RFC 6455 §1.3 example answer for the key above.
constexpr char kHandshakeAccept[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

inline QByteArray handshakeRequest(const QByteArray& path = "/")
{
    QByteArray request = "GET ";
    request += path;
    request += " HTTP/1.1\r\n"
               "Host: 127.0.0.1\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: ";
    request += kHandshakeKey;
    request += "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    return request;
}

// Client-to-server frame: masked, as required by the RFC.
inline QByteArray clientFrame(const QByteArray& payload, quint8 opcode = 0x1, bool masked = true)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    const auto size = payload.size();
    if (size < 126) {
        frame.append(static_cast<char>((masked ? 0x80 : 0x00) | size));
    } else {
        frame.append(static_cast<char>((masked ? 0x80 : 0x00) | 126));
        frame.append(static_cast<char>((size >> 8) & 0xff));
        frame.append(static_cast<char>(size & 0xff));
    }
    if (!masked)
        return frame + payload;

    const QByteArray mask("\x11\x22\x33\x44", 4);
    frame.append(mask);
    for (int i = 0; i < payload.size(); ++i)
        frame.append(static_cast<char>(payload[i] ^ mask[i % 4]));
    return frame;
}

inline QByteArray clientJson(const QJsonObject& object)
{
    return clientFrame(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// A frame header only: used to declare a length without sending the body.
inline QByteArray frameHeader(quint64 length, quint8 opcode = 0x1)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    if (length < 126) {
        frame.append(static_cast<char>(0x80 | length));
    } else if (length <= 0xffff) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((length >> 8) & 0xff));
        frame.append(static_cast<char>(length & 0xff));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((length >> (8 * i)) & 0xff));
    }
    frame.append(QByteArray("\x11\x22\x33\x44", 4));
    return frame;
}

// Extracts complete application frames from a raw server-to-client buffer.
inline QList<QByteArray> takeFrames(QByteArray& buffer)
{
    QList<QByteArray> messages;
    while (buffer.size() >= 2) {
        const auto* data = reinterpret_cast<const quint8*>(buffer.constData());
        quint64 length = data[1] & 0x7f;
        int offset = 2;
        if (length == 126) {
            if (buffer.size() < 4)
                break;
            length = (static_cast<quint64>(data[2]) << 8) | data[3];
            offset = 4;
        } else if (length == 127) {
            if (buffer.size() < 10)
                break;
            length = 0;
            for (int i = 0; i < 8; ++i)
                length = (length << 8) | data[2 + i];
            offset = 10;
        }
        if (buffer.size() < offset + static_cast<qsizetype>(length))
            break;
        messages.append(buffer.mid(offset, static_cast<qsizetype>(length)));
        buffer.remove(0, offset + static_cast<qsizetype>(length));
    }
    return messages;
}

inline quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const auto port = probe.serverPort();
    probe.close();
    return port;
}

// Runs the event loop briefly so in-process servers make progress. Blocking
// waits would deadlock: server and client share this thread.
inline void pump(int budget = 20)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, budget);
}

class WsClient {
public:
    QTcpSocket socket;

    bool open(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (!socket.waitForConnected(4000))
            return false;
        socket.write(handshakeRequest());
        socket.flush();

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 4000) {
            pull();
            // Drop the HTTP response so the frame parser only sees frames.
            if (const int end = buffer_.indexOf("\r\n\r\n"); end >= 0) {
                response_ = buffer_.left(end + 4);
                buffer_.remove(0, end + 4);
                return true;
            }
            QThread::msleep(2);
        }
        return false;
    }

    [[nodiscard]] QByteArray handshakeResponse() const { return response_; }

    void send(const QJsonObject& object) { socket.write(clientJson(object)); }
    void sendRaw(const QByteArray& bytes) { socket.write(bytes); }

    QJsonObject waitFor(const QString& type, int timeoutMs = 4000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (auto found = take(type); !found.isEmpty())
                return found;
            pull();
            pending_.append(takeFrames(buffer_));
            QThread::msleep(2);
        }
        return {};
    }

    bool waitForDisconnect(int timeoutMs = 4000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            pull();
            if (socket.state() != QAbstractSocket::ConnectedState)
                return true;
            QThread::msleep(2);
        }
        return false;
    }

    void drain()
    {
        pull();
        pending_.append(takeFrames(buffer_));
    }

    void closeAbruptly()
    {
        socket.abort();
    }

private:
    // Runs the event loop (so the in-process server makes progress) and moves
    // whatever the socket has produced into the parse buffer.
    void pull()
    {
        pump();
        buffer_.append(socket.readAll());
    }

    QJsonObject take(const QString& type)
    {
        for (int i = 0; i < pending_.size(); ++i) {
            const auto document = QJsonDocument::fromJson(pending_.at(i));
            if (document.isObject()
                && document.object().value(QStringLiteral("t")).toString() == type) {
                pending_.removeAt(i);
                return document.object();
            }
        }
        return {};
    }

    QByteArray response_;
    QByteArray buffer_;
    QList<QByteArray> pending_;
};

} // namespace remotetest
