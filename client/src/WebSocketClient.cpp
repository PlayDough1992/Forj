#include "WebSocketClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static constexpr int kPingIntervalMs = 30'000;

WebSocketClient::WebSocketClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_pingTimer(new QTimer(this))
{
    m_pingTimer->setInterval(kPingIntervalMs);

    connect(m_socket,    &QWebSocket::connected,            this, &WebSocketClient::onConnected);
    connect(m_socket,    &QWebSocket::disconnected,         this, &WebSocketClient::onDisconnected);
    connect(m_socket,    &QWebSocket::textMessageReceived,  this, &WebSocketClient::onTextMessageReceived);
    connect(m_socket,    QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &WebSocketClient::onError);
    connect(m_pingTimer, &QTimer::timeout,                  this, &WebSocketClient::onPingTimer);
}

WebSocketClient::~WebSocketClient()
{
    m_pingTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
}

void WebSocketClient::connectToServer(const QString& wsUrl, const QString& token)
{
    m_token = token;
    m_wsUrl = wsUrl;
    m_socket->open(QUrl(wsUrl));
}

void WebSocketClient::disconnectFromServer()
{
    m_pingTimer->stop();
    m_socket->close();
}

bool WebSocketClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void WebSocketClient::onConnected()
{
    // Send authentication message immediately after connecting
    QJsonObject auth;
    auth["type"]  = "authenticate";
    auth["token"] = m_token;
    m_socket->sendTextMessage(QJsonDocument(auth).toJson(QJsonDocument::Compact));

    m_pingTimer->start();
}

void WebSocketClient::onDisconnected()
{
    m_pingTimer->stop();
    emit disconnected();
}

void WebSocketClient::onTextMessageReceived(const QString& text)
{
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject msg  = doc.object();
    const QString     type = msg["type"].toString();

    if (type == "authenticated") {
        emit connected();
    }
    else if (type == "error") {
        emit authenticationFailed(msg["message"].toString());
    }
    else if (type == "new_message") {
        const QJsonObject data = msg["data"].toObject();
        MessageInfo info;
        info.id               = data["id"].toInt();
        info.channelId        = data["channel_id"].toInt();
        info.authorId         = data["author_id"].toInt();
        info.authorUsername   = data["author_username"].toString();
        info.authorHasAvatar  = data["author_has_avatar"].toBool();
        info.authorIsBot      = data["author_is_bot"].toBool();
        info.content          = data["content"].toString();
        info.createdAt        = data["created_at"].toString();
        info.replyToId        = data["reply_to_id"].toInt();
        info.replyToAuthor    = data["reply_to_author"].toString();
        info.replyToContent   = data["reply_to_content"].toString();
        emit newMessage(info);
    }
    else if (type == "dm_message") {
        const QJsonObject data = msg["data"].toObject();
        DmMessageInfo info;
        info.id               = data["id"].toInt();
        info.conversationId   = data["conversation_id"].toInt();
        info.authorId         = data["author_id"].toInt();
        info.authorUsername   = data["author_username"].toString();
        info.authorHasAvatar  = data["author_has_avatar"].toBool();
        info.authorIsBot      = data["author_is_bot"].toBool();
        info.content          = data["content"].toString();
        info.createdAt        = data["created_at"].toString();
        info.replyToId        = data["reply_to_id"].toInt();
        info.replyToAuthor    = data["reply_to_author"].toString();
        info.replyToContent   = data["reply_to_content"].toString();
        emit newDmMessage(info);
    }
    else if (type == "mention") {
        const QJsonObject data = msg["data"].toObject();
        emit mentioned(
            data["guild_id"].toInt(),
            data["channel_id"].toInt(),
            data["channel_name"].toString(),
            data["author_username"].toString(),
            data["content"].toString()
        );
    }
    else if (type == "user_online") {
        const QJsonObject data = msg["data"].toObject();
        emit userOnline(data["user_id"].toInt(), data["username"].toString());
    }
    else if (type == "user_offline") {
        const QJsonObject data = msg["data"].toObject();
        emit userOffline(data["user_id"].toInt(), data["username"].toString());
    }
    else if (type == "user_status") {
        const QJsonObject data = msg["data"].toObject();
        emit userStatusChanged(
            data["user_id"].toInt(),
            data["username"].toString(),
            data["status"].toString());
    }
    else if (type == "friend_request") {
        const QJsonObject data = msg["data"].toObject();
        emit friendRequest(
            data["from_id"].toInt(),
            data["from_username"].toString(),
            data["from_display_name"].toString());
    }
    else if (type == "friend_accepted") {
        const QJsonObject data = msg["data"].toObject();
        emit friendAccepted(
            data["by_id"].toInt(),
            data["by_username"].toString(),
            data["by_display_name"].toString());
    }
    else if (type == "guild_invite") {
        const QJsonObject data = msg["data"].toObject();
        emit guildInviteReceived(
            data["guild_id"].toInt(),
            data["guild_name"].toString(),
            data["invite_code"].toString(),
            data["inviter_username"].toString());
    }
    else if (type == "voice_join") {
        const QJsonObject data = msg["data"].toObject();
        emit voiceJoin(data["channel_id"].toInt(),
                       data["user_id"].toInt(),
                       data["username"].toString());
    }
    else if (type == "voice_leave") {
        const QJsonObject data = msg["data"].toObject();
        emit voiceLeave(data["channel_id"].toInt(),
                        data["user_id"].toInt(),
                        data["username"].toString());
    }
    else if (type == "audio_data") {
        const QJsonObject data = msg["data"].toObject();
        emit audioData(
            data["channel_id"].toInt(),
            data["user_id"].toInt(),
            QByteArray::fromBase64(data["pcm"].toString().toLatin1()));
    }
    else if (type == "voice_speaking") {
        const QJsonObject data = msg["data"].toObject();
        emit voiceSpeaking(
            data["channel_id"].toInt(),
            data["user_id"].toInt(),
            data["speaking"].toBool());
    }
    // "pong" is silently ignored
    else if (type == "kyc_update") {
        const QJsonObject data = msg["data"].toObject();
        if (data["kyc_status"].toString() == "approved")
            emit kycApproved();
    }
    else if (type == "safety_freeze") {
        const QJsonObject data = msg["data"].toObject();
        emit safetyFreeze(
            data["conversation_id"].toInt(),
            data["message"].toString());
    }
}

void WebSocketClient::onError(QAbstractSocket::SocketError /*error*/)
{
    // The disconnected signal will fire shortly; nothing extra needed here.
}

void WebSocketClient::onPingTimer()
{
    if (isConnected()) {
        QJsonObject ping;
        ping["type"] = "ping";
        m_socket->sendTextMessage(QJsonDocument(ping).toJson(QJsonDocument::Compact));
    }
}

void WebSocketClient::sendAudioFrame(int channelId, const QByteArray& pcm)
{
    if (!isConnected()) return;
    QJsonObject msg;
    msg["type"]       = QStringLiteral("audio_data");
    msg["channel_id"] = channelId;
    msg["pcm"]        = QString::fromLatin1(pcm.toBase64());
    m_socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void WebSocketClient::sendVoiceSpeaking(int channelId, bool speaking)
{
    if (!isConnected()) return;
    QJsonObject msg;
    msg["type"]       = QStringLiteral("voice_speaking");
    msg["channel_id"] = channelId;
    msg["speaking"]   = speaking;
    m_socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}
