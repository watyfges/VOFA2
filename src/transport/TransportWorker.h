#pragma once

#include "TransportTypes.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>

class QSerialPort;
class QTcpServer;
class QTcpSocket;
class QTimer;
class QUdpSocket;

namespace vofa::data {
class ChannelStore;
}

namespace vofa::protocol {
class IStreamParser;
}

namespace vofa::transport {

// Owns all sockets/serial ports and runs entirely in a dedicated QThread.
// Public slots are invoked with queued connections from TransportController.
class TransportWorker final : public QObject {
    Q_OBJECT

public:
    explicit TransportWorker(data::ChannelStore* store, QObject* parent = nullptr);
    ~TransportWorker() override;

public slots:
    void open(const TransportConfig& config);
    void shutdown();
    void send(const QByteArray& data);
    void sendTo(quint64 clientId, const QByteArray& data);
    void setActiveClient(quint64 clientId);
    void setBreak(bool enabled);

signals:
    void stateChanged(vofa::transport::ConnectionState state, const QString& error);
    void received(quint64 generation, quint64 clientId, const QByteArray& data);
    void clientsChanged(const QList<quint64>& ids,
                        const QStringList& labels,
                        quint64 activeId);
    void listening(quint16 port);
    void imageReceived(quint64 generation,
                       quint32 imageId,
                       int format,
                       int width,
                       int height,
                       const QByteArray& data);
    void sendFailed(const QString& message);
    void errorOccurred(const QString& message);
    void streamReset(quint64 generation);

private slots:
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onTcpClientConnected();
    void onTcpClientDisconnected();
    void onTcpClientReadyRead();
    void onTcpClientError();
    void onTcpServerNewConnection();
    void onTcpServerReadyRead();
    void onTcpServerDisconnected();
    void onTcpServerAcceptError();
    void onUdpReadyRead();
    void onDemoTick();

private:
    struct Sink;

    void openSerial(const TransportConfig& config);
    void openTcpClient(const TransportConfig& config);
    void openTcpServer(const TransportConfig& config);
    void openUdp(const TransportConfig& config);
    void openDemo();

    void processIncoming(quint64 clientId, const QByteArray& data, bool feedParser);
    void resetParser();
    void closeAll();
    void emitClients();
    void setState(ConnectionState state, const QString& error = {});
    void sendBytes(quint64 clientId, const QByteArray& data, bool toActive);
    [[nodiscard]] quint64 clientIdForSocket(QTcpSocket* socket) const;
    [[nodiscard]] bool checkBackpressure(QTcpSocket* socket) const;

    data::ChannelStore* store_ = nullptr;
    TransportConfig config_;
    std::unique_ptr<protocol::IStreamParser> parser_;
    std::unique_ptr<Sink> sink_;

    quint64 generation_ = 0;
    quint64 activeClientId_ = 0;
    quint64 nextClientId_ = 1;

    QSerialPort* serial_ = nullptr;
    QTcpSocket* tcpClient_ = nullptr;
    QTcpServer* tcpServer_ = nullptr;
    QHash<quint64, QTcpSocket*> clients_;
    QUdpSocket* udp_ = nullptr;
    QTimer* demoTimer_ = nullptr;
    QElapsedTimer demoClock_;
};

} // namespace vofa::transport
