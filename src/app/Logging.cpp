#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace vofa::app {
namespace {

constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;
constexpr int kMaxLogFiles = 5;

QMutex g_mutex;
QString g_logPath;
QtMessageHandler g_previousHandler = nullptr;

void rotateIfNeeded()
{
    const QFileInfo info(g_logPath);
    if (!info.exists() || info.size() < kMaxLogBytes)
        return;

    QFile::remove(g_logPath + QStringLiteral(".%1").arg(kMaxLogFiles - 1));
    for (int i = kMaxLogFiles - 2; i >= 1; --i) {
        const auto from = g_logPath + QStringLiteral(".%1").arg(i);
        const auto to = g_logPath + QStringLiteral(".%1").arg(i + 1);
        QFile::remove(to);
        (void)QFile::rename(from, to);
    }
    QFile::remove(g_logPath + QStringLiteral(".1"));
    (void)QFile::rename(g_logPath, g_logPath + QStringLiteral(".1"));
}

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "LOG";
}

void fileMessageHandler(QtMsgType type,
                        const QMessageLogContext& context,
                        const QString& message)
{
    {
        QMutexLocker locker(&g_mutex);
        QFile file(g_logPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            const auto line =
                QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                + QStringLiteral(" [") + QLatin1String(levelName(type))
                + QStringLiteral("] ") + message + QLatin1Char('\n');
            (void)file.write(line.toUtf8());
            file.close();
        }
        rotateIfNeeded();
    }

    if (g_previousHandler != nullptr)
        g_previousHandler(type, context, message);
}

} // namespace

void installFileLogger(const QString& configDir)
{
    const auto dir = configDir + QStringLiteral("/logs");
    if (!QDir().mkpath(dir))
        return;

    g_logPath = dir + QStringLiteral("/vofa2.log");
    {
        QMutexLocker locker(&g_mutex);
        rotateIfNeeded();
    }
    g_previousHandler = qInstallMessageHandler(fileMessageHandler);
}

} // namespace vofa::app
