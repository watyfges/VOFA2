#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSet>
#include <QThread>

class ImageDecodeWorker;

namespace vofa::app {

// Image provider for the QML Image element:
//   image://vofa/<imageId>?rev=<revision>
//
// Encoded frames are handed off to a dedicated decode thread. Only the newest
// pending frame per image id is decoded (latest-frame priority), so a slow
// decoder drops old frames instead of blocking the transport/UI thread.
class ImageProvider final : public QQuickImageProvider {
    Q_OBJECT

public:
    ImageProvider();
    ~ImageProvider() override;

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

    // Queues an encoded frame and returns its sequence number (0 on empty
    // input). The decoded image is announced through imageDecoded().
    quint64 setImage(int imageId, const QByteArray& encoded);

signals:
    void imageDecoded(int imageId, quint64 sequence);

private:
    friend class ::ImageDecodeWorker;
    struct PendingFrame {
        QByteArray data;
        quint64 sequence = 0;
    };

    void decodeLatest(int imageId); // worker thread

    mutable QMutex mutex_;
    QHash<int, QImage> images_;
    QHash<int, PendingFrame> pending_;
    QSet<int> scheduled_;
    QHash<int, quint64> lastDecoded_;
    quint64 nextSequence_ = 0;

    QThread* thread_ = nullptr;
    ImageDecodeWorker* worker_ = nullptr;
};

} // namespace vofa::app
