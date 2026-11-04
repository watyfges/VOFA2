#include "TransportController.h"

#include "TransportWorker.h"

#include <QMetaObject>

namespace vofa::transport {

TransportController::TransportController(data::ChannelStore* store, QObject* parent)
    : QObject(parent)
    , store_(store)
{
    qRegisterMetaType<ConnectionState>("vofa::transport::ConnectionState");
    qRegisterMetaType<TransportConfig>("vofa::transport::TransportConfig");
}

TransportController::~TransportController()
{
    stopWorker();
}

void TransportController::open(const TransportConfig& config)
{
    ensureWorker();
    QMetaObject::invokeMethod(worker_,
                              "open",
                              Qt::QueuedConnection,
                              Q_ARG(vofa::transport::TransportConfig, config));
}

void TransportController::close()
{
    stopWorker();
    state_ = ConnectionState::Disconnected;
    emit stateChanged(ConnectionState::Disconnected, {});
}

void TransportController::send(const QByteArray& data)
{
    if (worker_ == nullptr) {
        emit sendFailed(QStringLiteral("transport is not open"));
        return;
    }
    QMetaObject::invokeMethod(worker_, "send", Qt::QueuedConnection, Q_ARG(QByteArray, data));
}

void TransportController::sendTo(quint64 clientId, const QByteArray& data)
{
    if (worker_ == nullptr) {
        emit sendFailed(QStringLiteral("transport is not open"));
        return;
    }
    QMetaObject::invokeMethod(worker_,
                              "sendTo",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, clientId),
                              Q_ARG(QByteArray, data));
}

void TransportController::setActiveClient(quint64 clientId)
{
    if (worker_ == nullptr)
        return;
    QMetaObject::invokeMethod(worker_,
                              "setActiveClient",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, clientId));
}

void TransportController::setBreak(bool enabled)
{
    if (worker_ == nullptr)
        return;
    QMetaObject::invokeMethod(worker_,
                              "setBreak",
                              Qt::QueuedConnection,
                              Q_ARG(bool, enabled));
}

void TransportController::ensureWorker()
{
    if (worker_ != nullptr)
        return;

    thread_ = new QThread(this);
    worker_ = new TransportWorker(store_);
    worker_->moveToThread(thread_);

    connect(worker_, &TransportWorker::stateChanged, this,
            [this](ConnectionState state, const QString& error) {
                state_ = state;
                emit stateChanged(state, error);
            });
    connect(worker_, &TransportWorker::received, this, &TransportController::received);
    connect(worker_, &TransportWorker::clientsChanged, this, &TransportController::clientsChanged);
    connect(worker_, &TransportWorker::listening, this, &TransportController::listening);
    connect(worker_, &TransportWorker::imageReceived, this, &TransportController::imageReceived);
    connect(worker_, &TransportWorker::sendFailed, this, &TransportController::sendFailed);
    connect(worker_, &TransportWorker::errorOccurred, this, &TransportController::errorOccurred);
    connect(worker_, &TransportWorker::streamReset, this, &TransportController::streamReset);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    thread_->start();
}

void TransportController::stopWorker()
{
    if (thread_ == nullptr)
        return;

    if (worker_ != nullptr && thread_->isRunning()) {
        QMetaObject::invokeMethod(worker_, "shutdown", Qt::BlockingQueuedConnection);
    }

    thread_->quit();
    thread_->wait(5000);
    thread_->deleteLater();

    thread_ = nullptr;
    worker_ = nullptr;
    state_ = ConnectionState::Disconnected;
}

} // namespace vofa::transport
