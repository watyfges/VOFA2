#pragma once

#include "protocol/ParserFactory.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QtSerialPort/QSerialPort>

namespace vofa::transport {

enum class TransportKind {
    Serial,
    TcpClient,
    TcpServer,
    Udp,
    Demo,
};

enum class ConnectionState {
    Disconnected,
    Opening,
    Connected,
    Closing,
    Error,
};

struct SerialConfig {
    QString portName;
    qint32 baudRate = 921600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool dtr = false;
    bool rts = false;
    bool breakEnabled = false;
};

struct TcpClientConfig {
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0;
    QByteArray banner;
};

struct TcpServerConfig {
    quint16 listenPort = 0;
};

struct UdpConfig {
    QString remoteHost = QStringLiteral("127.0.0.1");
    quint16 remotePort = 0;
    quint16 localPort = 0;
};

struct TransportConfig {
    TransportKind kind = TransportKind::Serial;
    protocol::ProtocolKind protocol = protocol::ProtocolKind::JustFloat;
    SerialConfig serial;
    TcpClientConfig tcpClient;
    TcpServerConfig tcpServer;
    UdpConfig udp;
};

} // namespace vofa::transport

Q_DECLARE_METATYPE(vofa::transport::ConnectionState)
Q_DECLARE_METATYPE(vofa::transport::TransportConfig)
