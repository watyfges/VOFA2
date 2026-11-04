#include "RemoteProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace vofa::remote {
namespace {

// Ids travel as JSON numbers, so they must stay inside the exact-integer range.
constexpr double kMaxExactId = 9007199254740991.0;

[[nodiscard]] bool readId(const QJsonValue& value, quint64& out)
{
    if (!value.isDouble())
        return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number > kMaxExactId)
        return false;
    out = static_cast<quint64>(number);
    return true;
}

[[nodiscard]] bool readNumber(const QJsonValue& value, double& out)
{
    if (!value.isDouble())
        return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number))
        return false;
    out = number;
    return true;
}

[[nodiscard]] QJsonArray stringArray(const QStringList& values)
{
    QJsonArray array;
    for (const auto& value : values)
        array.append(value);
    return array;
}

void insertId(QJsonObject& object, const char* key, quint64 id)
{
    object.insert(QLatin1String(key), static_cast<double>(id));
}

} // namespace

ClientMessage parseClientMessage(const QByteArray& payload)
{
    ClientMessage message;

    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        message.error = QStringLiteral("消息不是合法的 JSON 对象");
        return message;
    }

    const auto root = document.object();
    const auto type = root.value(QLatin1String("t")).toString();
    if (type.isEmpty()) {
        message.error = QStringLiteral("消息缺少 t 字段");
        return message;
    }

    if (type == QLatin1String("hello")) {
        message.type = ClientMessageType::Hello;
        message.identity = root.value(QLatin1String("clientId")).toString().left(64);
        message.name = root.value(QLatin1String("name")).toString().left(64);
        message.pin = root.value(QLatin1String("pin")).toString().left(32);
        return message;
    }

    if (type == QLatin1String("sync")) {
        message.type = ClientMessageType::Sync;
        return message;
    }

    if (type == QLatin1String("ping")) {
        message.type = ClientMessageType::Ping;
        return message;
    }

    if (type == QLatin1String("oneclick.stop")) {
        message.type = ClientMessageType::OneClickStop;
        return message;
    }

    if (type == QLatin1String("oneclick")) {
        message.type = ClientMessageType::OneClick;
        message.group = root.value(QLatin1String("group")).toString().left(128);
        const auto interval = root.value(QLatin1String("intervalMs"));
        if (interval.isDouble()) {
            const auto raw = interval.toDouble();
            // Clamp before the integer conversion: casting an out-of-range
            // double to int is undefined behaviour.
            if (std::isfinite(raw) && raw > 0.0)
                message.intervalMs = static_cast<int>(std::clamp(raw, 1.0, 600000.0));
        }
        return message;
    }

    if (type == QLatin1String("value") || type == QLatin1String("commit")) {
        message.type = type == QLatin1String("value") ? ClientMessageType::Value
                                                      : ClientMessageType::Commit;
        if (!readId(root.value(QLatin1String("id")), message.id)) {
            message.type = ClientMessageType::Unknown;
            message.error = QStringLiteral("控件 id 无效");
            return message;
        }
        if (!readNumber(root.value(QLatin1String("v")), message.value)) {
            message.type = ClientMessageType::Unknown;
            message.error = QStringLiteral("数值无效");
            return message;
        }
        message.exact = root.value(QLatin1String("exact")).toBool(false);
        return message;
    }

    if (type == QLatin1String("action")) {
        message.type = ClientMessageType::Action;
        if (!readId(root.value(QLatin1String("id")), message.id)) {
            message.type = ClientMessageType::Unknown;
            message.error = QStringLiteral("动作 id 无效");
            return message;
        }
        message.trigger = root.value(QLatin1String("trigger")).toBool(false);
        message.on = root.value(QLatin1String("on")).toBool(false);
        if (!message.trigger && !root.value(QLatin1String("on")).isBool()) {
            message.type = ClientMessageType::Unknown;
            message.error = QStringLiteral("动作需要 on 或 trigger 字段");
        }
        return message;
    }

    message.error = QStringLiteral("未知消息类型：%1").arg(type.left(32));
    return message;
}

QJsonObject stateToJson(const StateDoc& doc, quint64 seq)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("state"));
    root.insert(QStringLiteral("proto"), kProtocolVersion);
    insertId(root, "seq", seq);
    root.insert(QStringLiteral("server"), doc.serverName);
    root.insert(QStringLiteral("version"), doc.version);
    root.insert(QStringLiteral("groups"), stringArray(doc.groups));
    root.insert(QStringLiteral("pcGroup"), doc.pcGroup);

    QJsonArray controls;
    for (const auto& control : doc.controls) {
        QJsonObject item;
        insertId(item, "id", control.id);
        item.insert(QStringLiteral("label"), control.label);
        item.insert(QStringLiteral("group"), control.group);
        item.insert(QStringLiteral("v"), control.value);
        item.insert(QStringLiteral("min"), control.minValue);
        item.insert(QStringLiteral("max"), control.maxValue);
        item.insert(QStringLiteral("step"), control.step);
        item.insert(QStringLiteral("inc"), control.included);
        item.insert(QStringLiteral("order"), control.order);
        controls.append(item);
    }
    root.insert(QStringLiteral("controls"), controls);

    QJsonArray actions;
    for (const auto& action : doc.actions) {
        QJsonObject item;
        insertId(item, "id", action.id);
        item.insert(QStringLiteral("label"), action.label);
        item.insert(QStringLiteral("group"), action.group);
        item.insert(QStringLiteral("kind"), action.kind);
        item.insert(QStringLiteral("on"), action.state);
        actions.append(item);
    }
    root.insert(QStringLiteral("actions"), actions);

    QJsonObject conn;
    conn.insert(QStringLiteral("connected"), doc.transportConnected);
    conn.insert(QStringLiteral("state"), doc.transportState);
    root.insert(QStringLiteral("conn"), conn);

    QJsonObject oneClick;
    oneClick.insert(QStringLiteral("sending"), doc.oneClick.sending);
    oneClick.insert(QStringLiteral("sent"), doc.oneClick.sent);
    oneClick.insert(QStringLiteral("total"), doc.oneClick.total);
    oneClick.insert(QStringLiteral("status"), doc.oneClick.status);
    oneClick.insert(QStringLiteral("interval"), doc.oneClick.intervalMs);
    root.insert(QStringLiteral("oneclick"), oneClick);

    return root;
}

QJsonObject welcomeMessage(quint64 session,
                           const QString& serverName,
                           const QString& version,
                           bool pinRequired)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("welcome"));
    root.insert(QStringLiteral("proto"), kProtocolVersion);
    insertId(root, "session", session);
    root.insert(QStringLiteral("name"), serverName);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("pin"), pinRequired);
    root.insert(QStringLiteral("maxClients"), kMaxClients);
    return root;
}

QJsonObject valueMessage(quint64 controlId, double value, const QString& from)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("value"));
    insertId(root, "id", controlId);
    root.insert(QStringLiteral("v"), value);
    root.insert(QStringLiteral("from"), from);
    return root;
}

QJsonObject actionMessage(quint64 actionId,
                          int kind,
                          bool on,
                          const QString& from,
                          bool flash)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("action"));
    insertId(root, "id", actionId);
    root.insert(QStringLiteral("kind"), kind);
    root.insert(QStringLiteral("on"), on);
    root.insert(QStringLiteral("from"), from);
    if (flash)
        root.insert(QStringLiteral("flash"), true);
    return root;
}

QJsonObject oneClickMessage(const QString& phase,
                            int sent,
                            int total,
                            const QString& status)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("oneclick"));
    root.insert(QStringLiteral("phase"), phase);
    root.insert(QStringLiteral("sent"), sent);
    root.insert(QStringLiteral("total"), total);
    root.insert(QStringLiteral("status"), status);
    return root;
}

QJsonObject connMessage(bool connected, const QString& state)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("conn"));
    root.insert(QStringLiteral("connected"), connected);
    root.insert(QStringLiteral("state"), state);
    return root;
}

QJsonObject clientsMessage(int count)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("clients"));
    root.insert(QStringLiteral("count"), count);
    return root;
}

QJsonObject errorMessage(const QString& code, const QString& message)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("error"));
    root.insert(QStringLiteral("code"), code);
    root.insert(QStringLiteral("message"), message);
    return root;
}

QJsonObject pongMessage()
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("pong"));
    return root;
}

bool isDiscoveryProbe(const QByteArray& payload)
{
    if (payload.isEmpty() || payload.size() > 512)
        return false;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return false;
    const auto root = document.object();
    return root.value(QLatin1String("t")).toString() == QLatin1String("discover")
        && root.value(QLatin1String("proto")).toInt(0) == kProtocolVersion;
}

QJsonObject discoveryOffer(const QString& name,
                           quint16 port,
                           bool pinRequired,
                           const QStringList& addresses,
                           const QString& version)
{
    QJsonObject root;
    root.insert(QStringLiteral("t"), QStringLiteral("offer"));
    root.insert(QStringLiteral("proto"), kProtocolVersion);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("port"), static_cast<int>(port));
    root.insert(QStringLiteral("pin"), pinRequired);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("ips"), stringArray(addresses));
    return root;
}

} // namespace vofa::remote
