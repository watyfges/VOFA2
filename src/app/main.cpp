#include "AppController.h"
#include "Logging.h"
#include "TimePlotItem.h"
#include "WaveformItem.h"
#include "WindowGeometry.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLibraryInfo>
#include <QLocale>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPointer>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QTranslator>
#include <QVariant>
#include <QtQml/qqml.h>

#include <algorithm>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>

namespace {
QString g_dumpDir;

LONG WINAPI vofaCrashHandler(EXCEPTION_POINTERS* info)
{
    static LONG handling = 0;
    if (InterlockedExchange(&handling, 1) != 0)
        return EXCEPTION_EXECUTE_HANDLER;

    if (!g_dumpDir.isEmpty()) {
        QDir().mkpath(g_dumpDir);
        const auto path = g_dumpDir + QStringLiteral("/vofa2-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
            + QStringLiteral(".dmp");
        HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                  GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION infoToWrite {};
            infoToWrite.ThreadId = GetCurrentThreadId();
            infoToWrite.ExceptionPointers = info;
            infoToWrite.ClientPointers = FALSE;
            (void)MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                    file, MiniDumpNormal, &infoToWrite,
                                    nullptr, nullptr);
            CloseHandle(file);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace
#endif

namespace {

// Keep the main window inside the usable area of whatever screen it is on.
// This runs on startup and whenever the window is moved to another monitor or
// that monitor's geometry / DPI changes (undocking, Windows display settings).
void clampMainWindow(QQuickWindow* window)
{
    if (window == nullptr)
        return;
    if (window->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen))
        return;

    const auto* screen = window->screen();
    if (screen == nullptr)
        return;

    const QRect current(window->x(), window->y(), window->width(), window->height());
    const QRect clamped =
        vofa::app::clampWindowToAvailable(current, screen->availableGeometry());
    if (clamped == current)
        return;

    qInfo().noquote() << "clamped window to screen" << screen->name()
                      << current << "->" << clamped;
    window->setGeometry(clamped);
}

// Re-subscribes to the current screen's geometry signals after the window
// moves across monitors.
class ScreenClampWatcher final : public QObject {
public:
    explicit ScreenClampWatcher(QQuickWindow* window)
        : QObject(window)
        , window_(window)
    {
        QObject::connect(window_, &QWindow::screenChanged, this, [this](QScreen* screen) {
            qInfo().noquote()
                << "screen changed:" << (screen != nullptr ? screen->name() : QStringLiteral("null"))
                << "screenRect" << (screen != nullptr ? screen->geometry() : QRect())
                << "screenDpr" << (screen != nullptr ? screen->devicePixelRatio() : 0.0)
                << "windowRect" << window_->geometry()
                << "windowDpr" << window_->devicePixelRatio();
            attachToScreen();
            // The new screen's geometry is applied after screenChanged.
            QTimer::singleShot(0, this, [this]() { clampMainWindow(window_); });
        });
        // A monitor can disappear before the window's screenChanged arrives.
        QObject::connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) {
            QTimer::singleShot(0, this, [this]() {
                attachToScreen();
                clampMainWindow(window_);
            });
        });
        QObject::connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen*) {
            QTimer::singleShot(0, this, [this]() { clampMainWindow(window_); });
        });
        attachToScreen();
    }

private:
    void attachToScreen()
    {
        if (!screen_.isNull())
            QObject::disconnect(screen_, nullptr, this, nullptr);

        screen_ = window_->screen();
        if (screen_.isNull())
            return;

        QObject::connect(screen_, &QScreen::availableGeometryChanged, this,
                         [this](const QRect&) { clampMainWindow(window_); });
        QObject::connect(screen_, &QScreen::geometryChanged, this,
                         [this](const QRect&) { clampMainWindow(window_); });
        QObject::connect(screen_, &QScreen::logicalDotsPerInchChanged, this,
                         [this](qreal dpi) {
                             qInfo().noquote() << "screen dpi changed:" << dpi
                                               << "windowDpr" << window_->devicePixelRatio();
                             clampMainWindow(window_);
                         });
    }

    QQuickWindow* window_ = nullptr;
    QPointer<QScreen> screen_;
};

} // namespace

int main(int argc, char* argv[])
{
    // Fractional Windows scaling (125%/150%) must not be rounded to 100%/200%.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("VOFA2"));

    // Basic is the themable style; the Windows native style is not used for
    // the application chrome.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // i18n scaffold: Chinese is the source language, so no .qm is required.
    // Put vofa2_<locale>.qm / qtbase_<locale>.qm in <exe>/translations (qtbase
    // translations can also come from the Qt installation) to enable another
    // UI language.
    // `--lang en` forces a locale for testing.
    QString requestedLanguage;
    const auto startupArguments = QCoreApplication::arguments();
    for (int i = 1; i + 1 < startupArguments.size(); ++i) {
        if (startupArguments.at(i) == QStringLiteral("--lang"))
            requestedLanguage = startupArguments.at(i + 1);
    }
    QTranslator appTranslator;
    QTranslator qtTranslator;
    const auto uiLocale = requestedLanguage.isEmpty()
        ? QLocale::system()
        : QLocale(requestedLanguage);
    const auto translationsDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    if (appTranslator.load(uiLocale, QStringLiteral("vofa2"), QStringLiteral("_"),
                           translationsDir)) {
        app.installTranslator(&appTranslator);
    }
    if (qtTranslator.load(uiLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    vofa::app::AppController controller;

    // A single instance owns the workspace/settings files. A second instance
    // would race on autosave and confuse the MCU connection.
    // The config directory may not exist on a fresh install; QLockFile needs
    // its parent directory, otherwise the lock fails and the app treats a
    // clean first launch as "already running".
    const auto configDir = controller.configDir();
    if (!QDir().mkpath(configDir))
        qWarning().noquote() << "cannot create config directory" << configDir;

    QLockFile instanceLock(QDir(configDir).filePath(QStringLiteral("vofa2.lock")));
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(200)) {
        qWarning().noquote() << "instance lock failed:" << instanceLock.fileName()
                             << "error" << instanceLock.error();
#ifdef Q_OS_WIN
        MessageBoxW(nullptr, L"VOFA2 已经在运行。", L"VOFA2",
                    MB_OK | MB_ICONINFORMATION);
#endif
        return 0;
    }

    vofa::app::installFileLogger(controller.configDir());
    qInfo().noquote() << "VOFA2 start, config dir:" << controller.configDir();
#ifdef Q_OS_WIN
    g_dumpDir = controller.configDir() + QStringLiteral("/dumps");
    SetUnhandledExceptionFilter(vofaCrashHandler);
#endif

    qmlRegisterType<vofa::app::WaveformItem>("Vofa2.Native", 1, 0, "WaveformItem");
    qmlRegisterType<vofa::app::TimePlotItem>("Vofa2.Native", 1, 0, "TimePlotItem");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("vofa"), controller.releaseImageProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Vofa2"), QStringLiteral("App"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Runtime DPI / monitor changes: clamp the restored or moved window into
    // the current screen's available geometry. Startup geometry from the
    // workspace is applied by QML during Component.onCompleted, hence the
    // single-shot 0 ms pass.
    if (auto* mainWindow =
            qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        new ScreenClampWatcher(mainWindow);
        QTimer::singleShot(0, mainWindow, [mainWindow]() { clampMainWindow(mainWindow); });
    }

    // Small debug/test helpers: connect before the UI loop, and optionally
    // trigger one-click sending without manual interaction.
    const auto arguments = QCoreApplication::arguments();
    QString protocol;
    int tcpServerPort = -1;
    QString tcpClient;
    bool autoOneClick = false;
    bool remoteServer = false;
    int remotePortArg = -1;
    bool uiCycle = false;
    bool demoSource = false;
    bool screenshotQuit = false;
    int uiPreset = -1;
    int uiTheme = -1;
    QString screenshotPath;
    QString openDialog;
    QString importLegacyDir;
    bool importLegacyDefault = false;
    QString importCsvPath;
    int qmlPreset = -1;
    bool qmlResetWindow = false;
    bool demoGroups = false;
    QString selectGroup;
    bool logHex = false;
    bool logTimestamps = false;
    bool logAnsi = false;
    QString logFilter;
    bool crashTest = false;
    bool comboPopupTest = false;
    bool colorPopupTest = false;
    bool debugLayout = false;
    bool cursorsTest = false;
    int waveformMode = -1;
    int screenshotDelayMs = 2500;

    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == QStringLiteral("--protocol") && i + 1 < arguments.size()) {
            protocol = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--tcp-server")
                   && i + 1 < arguments.size()) {
            tcpServerPort = arguments[++i].toInt();
        } else if (arguments[i] == QStringLiteral("--tcp-client")
                   && i + 1 < arguments.size()) {
            tcpClient = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--auto-one-click")) {
            autoOneClick = true;
        } else if (arguments[i] == QStringLiteral("--remote")) {
            remoteServer = true;
        } else if (arguments[i] == QStringLiteral("--remote-port")
                   && i + 1 < arguments.size()) {
            remoteServer = true;
            remotePortArg = arguments[++i].toInt();
        } else if (arguments[i] == QStringLiteral("--ui-preset")
                   && i + 1 < arguments.size()) {
            uiPreset = arguments[++i].toInt();
        } else if (arguments[i] == QStringLiteral("--ui-theme")
                   && i + 1 < arguments.size()) {
            uiTheme = arguments[++i].toInt();
        } else if (arguments[i] == QStringLiteral("--ui-cycle")) {
            uiCycle = true;
        } else if (arguments[i] == QStringLiteral("--demo")) {
            demoSource = true;
        } else if (arguments[i] == QStringLiteral("--screenshot")
                   && i + 1 < arguments.size()) {
            screenshotPath = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--screenshot-quit")) {
            screenshotQuit = true;
        } else if (arguments[i] == QStringLiteral("--screenshot-delay")
                   && i + 1 < arguments.size()) {
            screenshotDelayMs = std::max(0, arguments[++i].toInt());
        } else if (arguments[i] == QStringLiteral("--open-dialog")
                   && i + 1 < arguments.size()) {
            openDialog = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--import-legacy")
                   && i + 1 < arguments.size()) {
            importLegacyDir = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--import-legacy-default")) {
            importLegacyDefault = true;
        } else if (arguments[i] == QStringLiteral("--import-csv")
                   && i + 1 < arguments.size()) {
            importCsvPath = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--qml-preset")
                   && i + 1 < arguments.size()) {
            qmlPreset = arguments[++i].toInt();
        } else if (arguments[i] == QStringLiteral("--qml-reset-window")) {
            qmlResetWindow = true;
        } else if (arguments[i] == QStringLiteral("--demo-groups")) {
            demoGroups = true;
        } else if (arguments[i] == QStringLiteral("--select-group")
                   && i + 1 < arguments.size()) {
            selectGroup = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--log-hex")) {
            logHex = true;
        } else if (arguments[i] == QStringLiteral("--log-timestamps")) {
            logTimestamps = true;
        } else if (arguments[i] == QStringLiteral("--log-ansi")) {
            logAnsi = true;
        } else if (arguments[i] == QStringLiteral("--log-filter")
                   && i + 1 < arguments.size()) {
            logFilter = arguments[++i];
        } else if (arguments[i] == QStringLiteral("--crash-test")) {
            crashTest = true;
        } else if (arguments[i] == QStringLiteral("--qml-combo-popup")) {
            comboPopupTest = true;
        } else if (arguments[i] == QStringLiteral("--qml-color-popup")) {
            colorPopupTest = true;
        } else if (arguments[i] == QStringLiteral("--qml-debug-layout")) {
            debugLayout = true;
        } else if (arguments[i] == QStringLiteral("--qml-cursors")) {
            cursorsTest = true;
        } else if (arguments[i] == QStringLiteral("--waveform-mode")
                   && i + 1 < arguments.size()) {
            waveformMode = arguments[++i].toInt();
        }
    }

    if (!protocol.isEmpty())
        controller.setProtocol(protocol);

    if (uiPreset >= 0)
        controller.setUiPreset(uiPreset);

    if (uiTheme >= 0)
        controller.setUiTheme(uiTheme);

    if (waveformMode >= 0)
        controller.setWaveformMode(waveformMode);

    if (uiCycle) {
        controller.setUiPreset(0);
        QTimer::singleShot(2500, &controller, [&controller]() {
            controller.setUiPreset(2);
        });
    }

    if (tcpServerPort > 0)
        controller.openTcpServer(tcpServerPort);

    if (demoSource)
        controller.openDemo();

    if (!tcpClient.isEmpty()) {
        const auto parts = tcpClient.split(QLatin1Char(':'));
        if (parts.size() == 2)
            controller.openTcpClient(parts[0], parts[1].toInt(), QString());
    }

    if (!importLegacyDir.isEmpty()) {
        const auto message = controller.importLegacyFromDirectory(importLegacyDir);
        qInfo().noquote() << message;
    }

    if (importLegacyDefault) {
        const auto message = controller.importLegacyFromDefault();
        qInfo().noquote() << message;
    }

    if (!importCsvPath.isEmpty()) {
        const auto message = controller.loadCsvRecording(importCsvPath);
        qInfo().noquote() << "import CSV:" << message;
    }

    if (demoGroups) {
        (void)controller.addCommandGroup(QStringLiteral("项目B"));
        (void)controller.addOneClickBinding(
            QStringLiteral("motor"), QStringLiteral("motor%.3f"),
            0.0, 100.0, 0.1, 50.0, true, QStringLiteral("项目B"));
        (void)controller.addActionBinding(
            QStringLiteral("自检"), QStringLiteral("selfcheck"),
            QString(), 0, QStringLiteral("项目B"));
    }

    if (!selectGroup.isEmpty()) {
        const auto index = controller.commandGroups().indexOf(selectGroup);
        if (index >= 0)
            controller.setCurrentGroup(index);
    }

    if (logHex)
        controller.setReceiveHex(true);
    if (logTimestamps)
        controller.setReceiveTimestamps(true);
    if (logAnsi) {
        controller.setReceiveHex(false);
        controller.setReceiveAnsi(true);
    }
    if (!logFilter.isEmpty())
        controller.setReceiveFilter(logFilter);

    if (crashTest) {
        QTimer::singleShot(2500, &app, []() {
            volatile int* invalid = nullptr;
            *invalid = 1; // deliberate crash to verify the minidump handler
        });
    }

    // Deterministic QML-path check for the density presets: call the QML
    // function, which calls app.setUiPreset() exactly like the UI buttons.
    if (qmlPreset >= 0) {
        QTimer::singleShot(1500, &app, [&engine, qmlPreset]() {
            if (engine.rootObjects().isEmpty())
                return;
            const bool ok = QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "applyUiPreset",
                Q_ARG(QVariant, qmlPreset));
            qInfo() << "applyUiPreset" << qmlPreset << "ok" << ok;
        });
    }

    if (cursorsTest) {
        QTimer::singleShot(2200, &app, [&engine]() {
            if (engine.rootObjects().isEmpty())
                return;
            (void)QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "debugEnableCursors");
        });
    }

    if (debugLayout) {
        QTimer::singleShot(2200, &app, [&engine]() {
            if (engine.rootObjects().isEmpty())
                return;
            (void)QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "debugLayout");
        });
    }

    if (colorPopupTest) {
        QTimer::singleShot(1800, &app, [&engine]() {
            if (engine.rootObjects().isEmpty())
                return;
            (void)QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "debugOpenPalette");
        });
    }

    if (comboPopupTest) {
        QTimer::singleShot(1500, &app, [&engine]() {
            if (engine.rootObjects().isEmpty())
                return;
            (void)QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "debugOpenProtocolCombo");
        });
    }

    if (qmlResetWindow) {
        QTimer::singleShot(1500, &app, [&engine]() {
            if (engine.rootObjects().isEmpty())
                return;
            const bool ok = QMetaObject::invokeMethod(
                engine.rootObjects().constFirst(), "resetWindowSize");
            qInfo() << "resetWindowSize ok" << ok;
        });
    }

    if (autoOneClick) {
        QTimer::singleShot(1500, &controller, [&controller]() {
            controller.startOneClickSend();
        });
    }

    if (remoteServer) {
        if (remotePortArg > 0)
            controller.setRemotePort(remotePortArg);
        controller.setRemoteEnabled(true);
    }

    if (!openDialog.isEmpty()) {
        QTimer::singleShot(1200, &controller, [&controller, openDialog]() {
            controller.requestDialog(openDialog);
        });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelayMs, &app,
                           [&engine, screenshotPath, screenshotQuit]() {
            if (engine.rootObjects().isEmpty())
                return;
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (window == nullptr)
                return;
            const auto image = window->grabWindow();
            if (!image.isNull())
                (void)image.save(screenshotPath);
            if (screenshotQuit)
                QCoreApplication::quit();
        });
    }

    if (qEnvironmentVariableIsSet("VOFA_SMOKE_TEST")) {
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
