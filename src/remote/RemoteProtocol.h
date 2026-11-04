#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// Wire format for the remote tuning channel. One WebSocket text frame carries
// exactly one JSON object; `t` selects the message type. Everything here is a
// pure value type so it can be tested without sockets.
namespace vofa::remote {

inline constexpr int kProtocolVersion = 1;
inline constexpr quint16 kDefaultControlPort = 47900;
inline constexpr quint16 kDiscoveryPort = 47901;
inline constexpr int kMaxClients = 16;
// One misbehaving device must not be able to occupy every slot and lock the
// real phone out.
inline constexpr int kMaxClientsPerAddress = 4;
inline constexpr int kMaxMessageBytes = 64 * 1024;
inline constexpr char kServerVersion[] = "0.1.0";

// `hello` must arrive within this window or the socket is dropped.
inline constexpr int kHelloTimeoutMs = 5000;
// Keepalive cadence and the point at which a silent peer is considered gone.
inline constexpr int kHeartbeatMs = 15000;
inline constexpr int kClientTimeoutMs = 45000;
// Abuse guard. A real drag is ~30 messages/s and one-click sends a handful, so
// this leaves a lot of headroom while still stopping a runaway client. Large
// messages cost proportionally more, so a 64 KB flood cannot monopolise the
// UI thread that also drives the serial link.
inline constexpr double kClientMessagesPerSecond = 300.0;
inline constexpr double kClientMessageBurst = 160.0;
// A client that stops reading must not make the server buffer forever.
inline constexpr qint64 kMaxSendBacklogBytes = 512 * 1024;

// A slider/dial binding, flattened for the mobile UI. Values mirror what the
// PC writes into `oneClickBindings_`.
struct ControlDoc {
    quint64 id = 0;
    QString label;
    QString group;
    double value = 0.0;
    double minValue = 0.0;
    double maxValue = 1.0;
    double step = 0.01;
    bool included = true;
    int order = 0;
};

// A button (kind 0) or toggle (kind 1).
struct ActionDoc {
    quint64 id = 0;
    QString label;
    QString group;
    int kind = 0;
    bool state = false;
};

struct OneClickDoc {
    bool sending = false;
    int sent = 0;
    int total = 0;
    QString status;
    int intervalMs = 100;
};

// Full snapshot. The client replaces its model with this document; deltas
// (`value`, `action`, `oneclick`, `conn`) only ever adjust existing entries.
struct StateDoc {
    QString serverName;
    QString version;
    QStringList groups;
    QString pcGroup;
    QVector<ControlDoc> controls;
    QVector<ActionDoc> actions;
    bool transportConnected = false;
    QString transportState;
    OneClickDoc oneClick;
};

enum class ClientMessageType {
    Unknown,
    Hello,
    Value,
    Commit,
    Action,
    OneClick,
    OneClickStop,
    Sync,
    Ping,
};

struct ClientMessage {
    ClientMessageType type = ClientMessageType::Unknown;
    QString error;

    // hello
    QString identity;
    QString name;
    QString pin;

    // value / commit / action
    quint64 id = 0;
    double value = 0.0;
    bool exact = false;

    // action
    bool on = false;
    bool trigger = false;

    // oneclick
    QString group;
    int intervalMs = 0;
};

// Returns Unknown with `error` set when the payload is not a usable message.
[[nodiscard]] ClientMessage parseClientMessage(const QByteArray& payload);

[[nodiscard]] QJsonObject stateToJson(const StateDoc& doc, quint64 seq);
[[nodiscard]] QJsonObject welcomeMessage(quint64 session,
                                         const QString& serverName,
                                         const QString& version,
                                         bool pinRequired);
[[nodiscard]] QJsonObject valueMessage(quint64 controlId,
                                       double value,
                                       const QString& from);
[[nodiscard]] QJsonObject actionMessage(quint64 actionId,
                                        int kind,
                                        bool on,
                                        const QString& from,
                                        bool flash);
[[nodiscard]] QJsonObject oneClickMessage(const QString& phase,
                                          int sent,
                                          int total,
                                          const QString& status);
[[nodiscard]] QJsonObject connMessage(bool connected, const QString& state);
[[nodiscard]] QJsonObject clientsMessage(int count);
[[nodiscard]] QJsonObject errorMessage(const QString& code, const QString& message);
[[nodiscard]] QJsonObject pongMessage();

// UDP discovery. Probes are tiny and deliberately unauthenticated so a phone
// can list servers before connecting.
[[nodiscard]] bool isDiscoveryProbe(const QByteArray& payload);
[[nodiscard]] QJsonObject discoveryOffer(const QString& name,
                                         quint16 port,
                                         bool pinRequired,
                                         const QStringList& addresses,
                                         const QString& version);

} // namespace vofa::remote
