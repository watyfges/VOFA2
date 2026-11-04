#include "ImageProvider.h"

#include <QBuffer>
#include <QImageReader>
#include <QMutexLocker>

class ImageDecodeWorker final : public QObject {
public:
    explicit ImageDecodeWorker(vofa::app::ImageProvider* provider)
        : provider_(provider)
    {
    }

    void decode(int imageId) { provider_->decodeLatest(imageId); }

private:
    vofa::app::ImageProvider* provider_;
};

namespace vofa::app {

ImageProvider::ImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
    // Bounded decode: the parser already validates the header, and this stops
    // a malicious/degenerate encoded image from allocating unbounded memory.
    QImageReader::setAllocationLimit(128);

    thread_ = new QThread(this);
    worker_ = new ImageDecodeWorker(this);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
}

ImageProvider::~ImageProvider()
{
    if (thread_ != nullptr) {
        thread_->quit();
        thread_->wait(3000);
    }
}

quint64 ImageProvider::setImage(int imageId, const QByteArray& encoded)
{
    if (encoded.isEmpty())
        return 0;

    quint64 sequence = 0;
    bool schedule = false;
    {
        QMutexLocker locker(&mutex_);
        sequence = ++nextSequence_;
        pending_.insert(imageId, PendingFrame { encoded, sequence });
        if (!scheduled_.contains(imageId)) {
            scheduled_.insert(imageId);
            schedule = true;
        }
    }

    if (schedule) {
        QMetaObject::invokeMethod(
            worker_, [this, imageId]() { decodeLatest(imageId); },
            Qt::QueuedConnection);
    }
    return sequence;
}

void ImageProvider::decodeLatest(int imageId)
{
    QByteArray encoded;
    quint64 sequence = 0;
    {
        QMutexLocker locker(&mutex_);
        const auto it = pending_.find(imageId);
        if (it == pending_.end()) {
            scheduled_.remove(imageId);
            return;
        }
        encoded = it->data;
        sequence = it->sequence;
        pending_.erase(it);
        scheduled_.remove(imageId);
    }

    QBuffer buffer(&encoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QImage image = reader.read();
    if (image.isNull())
        return;

    bool accepted = false;
    {
        QMutexLocker locker(&mutex_);
        const auto previous = lastDecoded_.value(imageId, 0);
        if (sequence >= previous) {
            images_.insert(imageId, image);
            lastDecoded_.insert(imageId, sequence);
            accepted = true;
        }
    }

    if (accepted) {
        QMetaObject::invokeMethod(
            this, [this, imageId, sequence]() { emit imageDecoded(imageId, sequence); },
            Qt::QueuedConnection);
    }
}

QImage ImageProvider::requestImage(const QString& id,
                                   QSize* size,
                                   const QSize& requestedSize)
{
    const auto query = id.indexOf(QLatin1Char('?'));
    const auto idText = query >= 0 ? id.left(query) : id;
    bool ok = false;
    const auto imageId = idText.toInt(&ok);
    if (!ok)
        return {};

    QMutexLocker locker(&mutex_);
    const auto it = images_.constFind(imageId);
    if (it == images_.constEnd())
        return {};

    QImage image = it.value();
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace vofa::app
