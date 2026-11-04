#pragma once

#include "TransportTypes.h"

#include <QObject>
#include <QThread>

namespace vofa::data {
class ChannelStore;
}

namespace vofa::transport {

class TransportWorker;

// UI-side facade. Owns a QThread and a TransportWorker; all socket/serial
// objects live in that thread. Methods are safe to call from the UI thread.
class TransportController final : public QObject {
    Q_OBJECT

public:
    explicit TransportController(data::ChannelStore* store, QObject* parent = nullptr);
    ~TransportController() override;

    void open(const TransportConfig& config);
    void close();
    void send(const QByteArray& data);
    void sendTo(quint64 clientId, const QByteArray& data);
    void setActiveClient(quint64 clientId);
    void setBreak(bool enabled);

    [[nodiscard]] ConnectionState state() const noexcept { return state_; }

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

private:
    void ensureWorker();
    void stopWorker();

    QThread* thread_ = nullptr;
    TransportWorker* worker_ = nullptr;
    data::ChannelStore* store_ = nullptr;
    ConnectionState state_ = ConnectionState::Disconnected;
};

} // namespace vofa::transport
