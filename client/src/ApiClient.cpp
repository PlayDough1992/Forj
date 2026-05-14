#include "ApiClient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void ApiClient::setBaseUrl(const QString& url)
{
    m_baseUrl = url.endsWith('/') ? url.chopped(1) : url;
}

void ApiClient::setToken(const QString& token)  { m_token = token; }
bool ApiClient::hasToken() const                { return !m_token.isEmpty(); }

// ── Private helpers ───────────────────────────────────────────────────────────

QNetworkRequest ApiClient::makeRequest(const QString& endpoint) const
{
    QNetworkRequest req(QUrl(m_baseUrl + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return req;
}

void ApiClient::doPost(const QString& endpoint,
                        const QJsonObject& body,
                        std::function<void(int, QByteArray)> handler)
{
    QNetworkReply* reply = m_nam->post(
        makeRequest(endpoint),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, h = std::move(handler)]() {
        h(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          reply->readAll());
        reply->deleteLater();
    });
}

void ApiClient::doGet(const QString& endpoint,
                       std::function<void(int, QByteArray)> handler)
{
    QNetworkReply* reply = m_nam->get(makeRequest(endpoint));
    connect(reply, &QNetworkReply::finished, this, [reply, h = std::move(handler)]() {
        h(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          reply->readAll());
        reply->deleteLater();
    });
}

void ApiClient::doDelete(const QString& endpoint,
                          std::function<void(int, QByteArray)> handler)
{
    QNetworkReply* reply = m_nam->deleteResource(makeRequest(endpoint));
    connect(reply, &QNetworkReply::finished, this, [reply, h = std::move(handler)]() {
        h(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          reply->readAll());
        reply->deleteLater();
    });
}

void ApiClient::doPatch(const QString& endpoint,
                         const QJsonObject& body,
                         std::function<void(int, QByteArray)> handler)
{
    QNetworkReply* reply = m_nam->sendCustomRequest(
        makeRequest(endpoint), "PATCH",
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, h = std::move(handler)]() {
        h(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          reply->readAll());
        reply->deleteLater();
    });
}

void ApiClient::doMultipart(const QString& endpoint,
                              const QByteArray& data,
                              const QString& mimeType,
                              const QString& fieldName,
                              const QString& fileName,
                              std::function<void(int, QByteArray)> handler)
{
    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                       .arg(fieldName, fileName));
    part.setBody(data);
    mp->append(part);

    QNetworkRequest req = makeRequest(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + mp->boundary());

    QNetworkReply* reply = m_nam->post(req, mp);
    mp->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [reply, h = std::move(handler)]() {
        h(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          reply->readAll());
        reply->deleteLater();
    });
}

// ── Parsing helpers ───────────────────────────────────────────────────────────

UserInfo ApiClient::parseUser(const QJsonObject& o)
{
    return UserInfo{
        o["id"].toInt(),
        o["username"].toString(),
        o["email"].toString(),
        o["display_name"].toString(),
        o["bio"].toString(),
        o["has_avatar"].toBool(),
        o["is_bot"].toBool(),
        o["created_at"].toString(),
        o["status"].toString(),
        o["kyc_status"].toString("unverified"),
    };
}

GuildInfo ApiClient::parseGuild(const QJsonObject& o)
{
    return GuildInfo{
        o["id"].toInt(),
        o["name"].toString(),
        o["owner_id"].toInt(),
        o["invite_code"].toString(),
        o["has_icon"].toBool(),
    };
}

ChannelInfo ApiClient::parseChannel(const QJsonObject& o)
{
    return ChannelInfo{
        o["id"].toInt(),
        o["guild_id"].toInt(),
        o["name"].toString(),
        o["channel_type"].toString(),
    };
}

MessageInfo ApiClient::parseMessage(const QJsonObject& o)
{
    MessageInfo m;
    m.id              = o["id"].toInt();
    m.channelId       = o["channel_id"].toInt();
    m.authorId        = o["author_id"].toInt();
    m.authorUsername  = o["author_username"].toString();
    m.authorHasAvatar = o["author_has_avatar"].toBool();
    m.authorIsBot     = o["author_is_bot"].toBool();
    m.content         = o["content"].toString();
    m.createdAt       = o["created_at"].toString();
    m.replyToId       = o["reply_to_id"].toInt();
    m.replyToAuthor   = o["reply_to_author"].toString();
    m.replyToContent  = o["reply_to_content"].toString();
    return m;
}

MemberInfo ApiClient::parseMember(const QJsonObject& o)
{
    MemberInfo m;
    m.userId      = o["user_id"].toInt();
    m.username    = o["username"].toString();
    m.displayName = o["display_name"].toString();
    m.nickname    = o["nickname"].toString();
    m.role        = o["role"].toString();
    m.isOnline    = o["is_online"].toBool();
    m.hasAvatar   = o["has_avatar"].toBool();
    m.isBot       = o["is_bot"].toBool();
    m.status      = o["status"].toString();
    for (const auto& v : o["roles"].toArray())
        m.roles.append(parseRole(v.toObject()));
    return m;
}

RoleInfo ApiClient::parseRole(const QJsonObject& o)
{
    return RoleInfo{
        o["id"].toInt(),
        o["guild_id"].toInt(),
        o["name"].toString(),
        o["color"].toString(),
        o["permissions"].toInt(),
        o["position"].toInt(),
        o["hoist"].toBool(),
    };
}

ServerProfileInfo ApiClient::parseServerProfile(const QJsonObject& o)
{
    return ServerProfileInfo{
        o["guild_id"].toInt(),
        o["user_id"].toInt(),
        o["nickname"].toString(),
        o["has_avatar"].toBool(),
    };
}

BanInfo ApiClient::parseBan(const QJsonObject& o)
{
    return BanInfo{
        o["guild_id"].toInt(),
        o["user_id"].toInt(),
        o["username"].toString(),
        o["reason"].toString(),
    };
}

DmConversationInfo ApiClient::parseDmConv(const QJsonObject& o)
{
    return DmConversationInfo{
        o["id"].toInt(),
        o["other_user_id"].toInt(),
        o["other_username"].toString(),
        o["other_display_name"].toString(),
        o["other_has_avatar"].toBool(),
    };
}

DmMessageInfo ApiClient::parseDmMsg(const QJsonObject& o)
{
    DmMessageInfo m;
    m.id              = o["id"].toInt();
    m.conversationId  = o["conversation_id"].toInt();
    m.authorId        = o["author_id"].toInt();
    m.authorUsername  = o["author_username"].toString();
    m.authorHasAvatar = o["author_has_avatar"].toBool();
    m.authorIsBot     = o["author_is_bot"].toBool();
    m.content         = o["content"].toString();
    m.createdAt       = o["created_at"].toString();
    m.replyToId       = o["reply_to_id"].toInt();
    m.replyToAuthor   = o["reply_to_author"].toString();
    m.replyToContent  = o["reply_to_content"].toString();
    return m;
}

BotInfo ApiClient::parseBot(const QJsonObject& o)
{
    return BotInfo{
        o["id"].toInt(),
        o["user_id"].toInt(),
        o["username"].toString(),
        o["name"].toString(),
        o["token"].toString(),
    };
}

CommandInfo ApiClient::parseCommand(const QJsonObject& o)
{
    return CommandInfo{
        o["id"].toInt(),
        o["guild_id"].toInt(),
        o["name"].toString(),
        o["description"].toString(),
        o["bot_username"].toString(),
    };
}

WebhookInfo ApiClient::parseWebhook(const QJsonObject& o)
{
    return WebhookInfo{
        o["id"].toInt(),
        o["channel_id"].toInt(),
        o["name"].toString(),
        o["token"].toString(),
        o["url"].toString(),
    };
}

FriendInfo ApiClient::parseFriend(const QJsonObject& o)
{
    return FriendInfo{
        o["user_id"].toInt(),
        o["username"].toString(),
        o["display_name"].toString(),
        o["has_avatar"].toBool(),
        o["status"].toString(),
    };
}

FriendRequestInfo ApiClient::parseFriendRequest(const QJsonObject& o)
{
    return FriendRequestInfo{
        o["from_user_id"].toInt(),
        o["from_username"].toString(),
        o["from_display_name"].toString(),
        o["from_has_avatar"].toBool(),
    };
}

// ── Auth ──────────────────────────────────────────────────────────────────────

void ApiClient::registerUser(const QString& username,
                              const QString& email,
                              const QString& password,
                              AuthCb callback)
{
    doPost("/auth/register",
           {{"username", username}, {"email", email}, {"password", password}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if (status == 201 && doc.isObject()) {
                   auto obj   = doc.object();
                   auto token = obj["access_token"].toString();
                   auto user  = parseUser(obj["user"].toObject());
                   // Carry KYC URL so the UI can open it immediately
                   user.kycVerificationUrl = obj["kyc_verification_url"].toString();
                   cb(true, {}, token, user);
               } else {
                   auto detail = doc.object()["detail"].toString("Request failed");
                   cb(false, detail, {}, {});
               }
           });
}

void ApiClient::login(const QString& username,
                      const QString& password,
                      AuthCb callback)
{
    doPost("/auth/login",
           {{"username", username}, {"password", password}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if (status == 200 && doc.isObject()) {
                   auto obj   = doc.object();
                   auto token = obj["access_token"].toString();
                   auto user  = parseUser(obj["user"].toObject());
                   cb(true, {}, token, user);
               } else {
                   auto detail = doc.object()["detail"].toString("Invalid credentials");
                   cb(false, detail, {}, {});
               }
           });
}

// ── Guilds ────────────────────────────────────────────────────────────────────

void ApiClient::getGuilds(GuildsCb callback)
{
    doGet("/guilds", [cb = std::move(callback)](int status, QByteArray data) {
        auto doc = QJsonDocument::fromJson(data);
        if (status == 200 && doc.isArray()) {
            QList<GuildInfo> guilds;
            for (const auto& v : doc.array())
                guilds.append(parseGuild(v.toObject()));
            cb(true, {}, guilds);
        } else {
            cb(false, doc.object()["detail"].toString("Failed to load guilds"), {});
        }
    });
}

void ApiClient::createGuild(const QString& name, GuildCb callback)
{
    doPost("/guilds", {{"name", name}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseGuild(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to create guild"), {});
               }
           });
}

void ApiClient::joinGuild(const QString& inviteCode, GuildCb callback)
{
    doPost("/guilds/join", {{"invite_code", inviteCode}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseGuild(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to join guild"), {});
               }
           });
}

void ApiClient::leaveGuild(int guildId, SimpleCb callback)
{
    doDelete(QString("/guilds/%1/leave").arg(guildId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) {
                     cb(true, {});
                 } else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to leave guild"));
                 }
             });
}

// ── Channels ──────────────────────────────────────────────────────────────────

void ApiClient::getChannels(int guildId, ChannelsCb callback)
{
    doGet(QString("/guilds/%1/channels").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<ChannelInfo> channels;
                  for (const auto& v : doc.array())
                      channels.append(parseChannel(v.toObject()));
                  cb(true, {}, channels);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load channels"), {});
              }
          });
}

void ApiClient::createChannel(int guildId, const QString& name, ChannelCb callback,
                               const QString& type)
{
    QJsonObject body;
    body["name"] = name;
    body["channel_type"] = type;
    doPost(QString("/guilds/%1/channels").arg(guildId), body,
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseChannel(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to create channel"), {});
               }
           });
}

void ApiClient::getVoiceState(int channelId, VoiceParticipantsCb callback)
{
    doGet(QString("/channels/%1/voice").arg(channelId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<VoiceParticipant> list;
                  for (const auto& v : doc.array()) {
                      const auto o = v.toObject();
                      VoiceParticipant p;
                      p.userId   = o["user_id"].toInt();
                      p.username = o["username"].toString();
                      list.append(p);
                  }
                  cb(true, {}, list);
              } else {
                  cb(false, "Failed to get voice state", {});
              }
          });
}

void ApiClient::joinVoice(int channelId, SimpleCb callback)
{
    doPost(QString("/channels/%1/voice/join").arg(channelId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204 || status == 200) {
                   cb(true, {});
               } else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to join voice"));
               }
           });
}

void ApiClient::leaveVoice(int channelId, SimpleCb callback)
{
    doPost(QString("/channels/%1/voice/leave").arg(channelId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204 || status == 200) {
                   cb(true, {});
               } else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to leave voice"));
               }
           });
}

// ── Messages ──────────────────────────────────────────────────────────────────

void ApiClient::getMessages(int channelId, int limit, MessagesCb callback)
{
    doGet(QString("/channels/%1/messages?limit=%2").arg(channelId).arg(limit),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<MessageInfo> msgs;
                  for (const auto& v : doc.array())
                      msgs.append(parseMessage(v.toObject()));
                  cb(true, {}, msgs);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load messages"), {});
              }
          });
}

void ApiClient::sendMessage(int channelId, const QString& content, MessageCb callback,
                             int replyToId)
{
    QJsonObject body{{"content", content}};
    if (replyToId > 0) body["reply_to_id"] = replyToId;
    doPost(QString("/channels/%1/messages").arg(channelId),
           body,
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseMessage(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to send message"), {});
               }
           });
}

// ── Members ───────────────────────────────────────────────────────────────────

void ApiClient::getMembers(int guildId, MembersCb callback)
{
    doGet(QString("/guilds/%1/members").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<MemberInfo> members;
                  for (const auto& v : doc.array())
                      members.append(parseMember(v.toObject()));
                  cb(true, {}, members);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load members"), {});
              }
          });
}

// ── User profile ──────────────────────────────────────────────────────────────

void ApiClient::updateProfile(const QString& displayName, const QString& bio, UserCb callback)
{
    QJsonObject body;
    body["display_name"] = displayName;
    body["bio"]          = bio;
    doPatch("/users/me", body, [cb = std::move(callback)](int status, QByteArray data) {
        auto doc = QJsonDocument::fromJson(data);
        if (status == 200 && doc.isObject()) {
            cb(true, {}, parseUser(doc.object()));
        } else {
            cb(false, doc.object()["detail"].toString("Failed to update profile"), {});
        }
    });
}

void ApiClient::uploadAvatar(const QByteArray& data, const QString& mimeType, SimpleCb callback)
{
    QString ext = "png";
    if      (mimeType == "image/jpeg") ext = "jpg";
    else if (mimeType == "image/gif")  ext = "gif";
    else if (mimeType == "image/webp") ext = "webp";

    doMultipart("/users/me/avatar", data, mimeType, "file",
                QStringLiteral("avatar.%1").arg(ext),
                [cb = std::move(callback)](int status, QByteArray raw) {
                    if (status == 204 || status == 200) {
                        cb(true, {});
                    } else {
                        auto doc = QJsonDocument::fromJson(raw);
                        cb(false, doc.object()["detail"].toString("Upload failed"));
                    }
                });
}

void ApiClient::getAvatar(int userId, std::function<void(bool, const QByteArray&)> callback)
{
    // Raw bytes endpoint — do not force JSON content-type on the request
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/users/%1/avatar").arg(userId)));
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(callback)]() {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QByteArray data = reply->readAll();
                reply->deleteLater();
                if (status == 200)
                    cb(true, data);
                else
                    cb(false, {});
            });
}

void ApiClient::changePassword(const QString& current, const QString& newPw, SimpleCb callback)
{
    doPost("/users/me/password",
           {{"current_password", current}, {"new_password", newPw}},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204 || status == 200) {
                   cb(true, {});
               } else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Password change failed"));
               }
           });
}

void ApiClient::setStatus(const QString& userStatus, SimpleCb callback)
{
    doPost("/users/me/status", {{"status", userStatus}},
           [cb = std::move(callback)](int code, QByteArray data) {
               if (code == 204) {
                   cb(true, {});
               } else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to update status"));
               }
           });
}

void ApiClient::searchUsers(const QString& query,
                             std::function<void(bool, const QString&, const QList<UserInfo>&)> callback)
{
    doGet(QStringLiteral("/users/search?q=%1").arg(QString::fromUtf8(
              QUrl::toPercentEncoding(query))),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<UserInfo> users;
                  for (const auto& v : doc.array())
                      users.append(parseUser(v.toObject()));
                  cb(true, {}, users);
              } else {
                  cb(false, doc.object()["detail"].toString("Search failed"), {});
              }
          });
}

// ── Slash-command invocation ──────────────────────────────────────────────────

void ApiClient::invokeCommand(int channelId, const QString& name,
                               const QString& args, MessageCb callback)
{
    doPost(QStringLiteral("/channels/%1/invoke").arg(channelId),
           {{"name", name}, {"args", args}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseMessage(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Command failed"), {});
               }
           });
}

void ApiClient::getCommands(int guildId, CommandsCb callback)
{
    doGet(QStringLiteral("/guilds/%1/commands").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<CommandInfo> cmds;
                  for (const auto& v : doc.array())
                      cmds.append(parseCommand(v.toObject()));
                  cb(true, {}, cmds);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load commands"), {});
              }
          });
}

// ── Direct Messages ───────────────────────────────────────────────────────────

void ApiClient::getDms(DmsCb callback)
{
    doGet("/dms", [cb = std::move(callback)](int status, QByteArray data) {
        auto doc = QJsonDocument::fromJson(data);
        if (status == 200 && doc.isArray()) {
            QList<DmConversationInfo> convs;
            for (const auto& v : doc.array())
                convs.append(parseDmConv(v.toObject()));
            cb(true, {}, convs);
        } else {
            cb(false, doc.object()["detail"].toString("Failed to load DMs"), {});
        }
    });
}

void ApiClient::openDm(int userId, DmCb callback)
{
    doPost("/dms", {{"user_id", userId}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseDmConv(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to open DM"), {});
               }
           });
}

void ApiClient::getDmMessages(int dmId, int limit, DmMsgsCb callback)
{
    doGet(QStringLiteral("/dms/%1/messages?limit=%2").arg(dmId).arg(limit),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<DmMessageInfo> msgs;
                  for (const auto& v : doc.array())
                      msgs.append(parseDmMsg(v.toObject()));
                  cb(true, {}, msgs);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load messages"), {});
              }
          });
}

void ApiClient::sendDm(int dmId, const QString& content, DmMsgCb callback, int replyToId)
{
    QJsonObject body{{"content", content}};
    if (replyToId > 0) body["reply_to_id"] = replyToId;
    doPost(QStringLiteral("/dms/%1/messages").arg(dmId),
           body,
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseDmMsg(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to send DM"), {});
               }
           });
}

// ── Bots ──────────────────────────────────────────────────────────────────────

void ApiClient::getBots(BotsCb callback)
{
    doGet("/bots", [cb = std::move(callback)](int status, QByteArray data) {
        auto doc = QJsonDocument::fromJson(data);
        if (status == 200 && doc.isArray()) {
            QList<BotInfo> bots;
            for (const auto& v : doc.array())
                bots.append(parseBot(v.toObject()));
            cb(true, {}, bots);
        } else {
            cb(false, doc.object()["detail"].toString("Failed to load bots"), {});
        }
    });
}

void ApiClient::createBot(const QString& name, BotCb callback)
{
    doPost("/bots", {{"name", name}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseBot(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to create bot"), {});
               }
           });
}

void ApiClient::deleteBot(int botId, SimpleCb callback)
{
    doDelete(QStringLiteral("/bots/%1").arg(botId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) {
                     cb(true, {});
                 } else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to delete bot"));
                 }
             });
}

void ApiClient::resetBotToken(int botId, BotCb callback)
{
    doPost(QStringLiteral("/bots/%1/reset-token").arg(botId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if (status == 200 && doc.isObject()) {
                   cb(true, {}, parseBot(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to reset token"), {});
               }
           });
}

// ── Webhooks ──────────────────────────────────────────────────────────────────

void ApiClient::getWebhooks(int channelId, WebhooksCb callback)
{
    doGet(QStringLiteral("/channels/%1/webhooks").arg(channelId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<WebhookInfo> hooks;
                  for (const auto& v : doc.array())
                      hooks.append(parseWebhook(v.toObject()));
                  cb(true, {}, hooks);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to load webhooks"), {});
              }
          });
}

void ApiClient::createWebhook(int channelId, const QString& name, WebhookCb callback)
{
    doPost(QStringLiteral("/channels/%1/webhooks").arg(channelId),
           {{"name", name}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if ((status == 200 || status == 201) && doc.isObject()) {
                   cb(true, {}, parseWebhook(doc.object()));
               } else {
                   cb(false, doc.object()["detail"].toString("Failed to create webhook"), {});
               }
           });
}

void ApiClient::deleteWebhook(int webhookId, SimpleCb callback)
{
    doDelete(QStringLiteral("/webhooks/%1").arg(webhookId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) {
                     cb(true, {});
                 } else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to delete webhook"));
                 }
             });
}

// ── Friends ───────────────────────────────────────────────────────────────────

void ApiClient::getFriends(FriendsCb callback)
{
    doGet("/friends",
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200) {
                  QList<FriendInfo> list;
                  for (auto v : doc.array())
                      list << parseFriend(v.toObject());
                  cb(true, {}, list);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to fetch friends"), {});
              }
          });
}

void ApiClient::getFriendRequests(FriendReqsCb callback)
{
    doGet("/friends/requests",
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200) {
                  QList<FriendRequestInfo> list;
                  for (auto v : doc.array())
                      list << parseFriendRequest(v.toObject());
                  cb(true, {}, list);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to fetch requests"), {});
              }
          });
}

void ApiClient::sendFriendRequest(int userId, SimpleCb callback)
{
    doPost(QStringLiteral("/friends/request/%1").arg(userId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to send request"));
               }
           });
}

void ApiClient::acceptFriendRequest(int userId, SimpleCb callback)
{
    doPost(QStringLiteral("/friends/accept/%1").arg(userId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to accept request"));
               }
           });
}

void ApiClient::declineFriendRequest(int userId, SimpleCb callback)
{
    doDelete(QStringLiteral("/friends/%1").arg(userId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to decline"));
                 }
             });
}

void ApiClient::removeFriend(int userId, SimpleCb callback)
{
    doDelete(QStringLiteral("/friends/%1").arg(userId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to remove friend"));
                 }
             });
}

void ApiClient::inviteUserToGuild(int guildId, int userId, SimpleCb callback)
{
    doPost(QStringLiteral("/guilds/%1/invite-user/%2").arg(guildId).arg(userId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to invite user"));
               }
           });
}

// ── Native KYC ───────────────────────────────────────────────────────────────

void ApiClient::initiateKyc(std::function<void(bool, const QString&, const QString&)> callback)
{
    doPost("/auth/kyc/initiate", {},
           [cb = std::move(callback)](int httpStatus, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if (httpStatus == 200) {
                   cb(true, doc.object()["verification_url"].toString(), {});
               } else {
                   cb(false, {}, doc.object()["detail"].toString("Could not start verification."));
               }
           });
}

void ApiClient::getKycStatus(std::function<void(bool, const QString&)> callback)
{
    doGet("/auth/kyc/status",
          [cb = std::move(callback)](int httpStatus, QByteArray data) {
              if (httpStatus == 200) {
                  auto doc = QJsonDocument::fromJson(data);
                  cb(true, doc.object()["kyc_status"].toString("unverified"));
              } else {
                  cb(false, {});
              }
          });
}

void ApiClient::submitKyc(const QByteArray& frontImage, const QString& frontMime,
                           const QByteArray& backImage,  const QString& backMime,
                           const QByteArray& selfieImage, const QString& selfieMime,
                           std::function<void(bool, const QString&)> callback)
{
    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addImagePart = [mp](const QString& field,
                              const QByteArray& data,
                              const QString& mime) {
        // guess extension for the filename
        QString ext = mime.endsWith("png") ? "png" : "jpg";
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"; filename=\"%1.%2\"")
                           .arg(field, ext));
        part.setBody(data);
        mp->append(part);
    };

    addImagePart("front_image",  frontImage,  frontMime);
    if (!backImage.isEmpty())
        addImagePart("back_image", backImage, backMime);
    addImagePart("selfie_image", selfieImage, selfieMime);

    QNetworkRequest req = makeRequest("/auth/kyc/verify");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + mp->boundary());

    QNetworkReply* reply = m_nam->post(req, mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(callback)]() {
                const int httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = reply->readAll();
                reply->deleteLater();

                auto doc = QJsonDocument::fromJson(body);
                const QString st = doc.object()["status"].toString();
                if (httpStatus == 200 && st == "approved") {
                    cb(true, {});
                } else {
                    QString msg = doc.object()["message"].toString();
                    if (msg.isEmpty())
                        msg = doc.object()["detail"].toString("Verification failed. Please try again.");
                    cb(false, msg);
                }
            });
}



void ApiClient::updateGuild(int guildId, const QString& name, GuildCb callback)
{
    doPatch(QStringLiteral("/guilds/%1").arg(guildId),
            QJsonObject{{"name", name}},
            [cb = std::move(callback)](int status, QByteArray data) {
                auto doc = QJsonDocument::fromJson(data);
                if (status == 200)
                    cb(true, {}, parseGuild(doc.object()));
                else
                    cb(false, doc.object()["detail"].toString("Update failed"), {});
            });
}

void ApiClient::deleteGuild(int guildId, SimpleCb callback)
{
    doDelete(QStringLiteral("/guilds/%1").arg(guildId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Delete failed"));
                 }
             });
}

void ApiClient::uploadGuildIcon(int guildId, const QByteArray& data,
                                 const QString& mime, SimpleCb callback)
{
    doMultipart(QStringLiteral("/guilds/%1/icon").arg(guildId),
                data, mime, "file", "icon.png",
                [cb = std::move(callback)](int status, QByteArray resp) {
                    if (status == 204) cb(true, {});
                    else {
                        auto doc = QJsonDocument::fromJson(resp);
                        cb(false, doc.object()["detail"].toString("Upload failed"));
                    }
                });
}

void ApiClient::getGuildIcon(int guildId, std::function<void(bool, const QByteArray&)> callback)
{
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/guilds/%1/icon").arg(guildId)));
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(callback)]() {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QByteArray data = reply->readAll();
                reply->deleteLater();
                if (status == 200) cb(true, data);
                else cb(false, {});
            });
}

// ── Roles ─────────────────────────────────────────────────────────────────────

void ApiClient::getRoles(int guildId, RolesCb callback)
{
    doGet(QStringLiteral("/guilds/%1/roles").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<RoleInfo> roles;
                  for (const auto& v : doc.array())
                      roles.append(parseRole(v.toObject()));
                  cb(true, {}, roles);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to get roles"), {});
              }
          });
}

void ApiClient::createRole(int guildId, const QString& name, const QString& color,
                            int permissions, bool hoist, RoleCb callback)
{
    doPost(QStringLiteral("/guilds/%1/roles").arg(guildId),
           QJsonObject{{"name", name}, {"color", color},
                       {"permissions", permissions}, {"hoist", hoist}},
           [cb = std::move(callback)](int status, QByteArray data) {
               auto doc = QJsonDocument::fromJson(data);
               if (status == 201) cb(true, {}, parseRole(doc.object()));
               else cb(false, doc.object()["detail"].toString("Failed to create role"), {});
           });
}

void ApiClient::updateRole(int guildId, int roleId, const QString& name,
                            const QString& color, int permissions, bool hoist, RoleCb callback)
{
    doPatch(QStringLiteral("/guilds/%1/roles/%2").arg(guildId).arg(roleId),
            QJsonObject{{"name", name}, {"color", color},
                        {"permissions", permissions}, {"hoist", hoist}},
            [cb = std::move(callback)](int status, QByteArray data) {
                auto doc = QJsonDocument::fromJson(data);
                if (status == 200) cb(true, {}, parseRole(doc.object()));
                else cb(false, doc.object()["detail"].toString("Failed to update role"), {});
            });
}

void ApiClient::deleteRole(int guildId, int roleId, SimpleCb callback)
{
    doDelete(QStringLiteral("/guilds/%1/roles/%2").arg(guildId).arg(roleId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to delete role"));
                 }
             });
}

void ApiClient::assignRole(int guildId, int userId, int roleId, SimpleCb callback)
{
    doPost(QStringLiteral("/guilds/%1/members/%2/roles/%3").arg(guildId).arg(userId).arg(roleId),
           {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Failed to assign role"));
               }
           });
}

void ApiClient::removeRole(int guildId, int userId, int roleId, SimpleCb callback)
{
    doDelete(QStringLiteral("/guilds/%1/members/%2/roles/%3").arg(guildId).arg(userId).arg(roleId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Failed to remove role"));
                 }
             });
}

// ── Moderation ────────────────────────────────────────────────────────────────

void ApiClient::kickMember(int guildId, int userId, SimpleCb callback)
{
    doPost(QStringLiteral("/guilds/%1/members/%2/kick").arg(guildId).arg(userId), {},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Kick failed"));
               }
           });
}

void ApiClient::banMember(int guildId, int userId, const QString& reason, SimpleCb callback)
{
    doPost(QStringLiteral("/guilds/%1/members/%2/ban").arg(guildId).arg(userId),
           QJsonObject{{"reason", reason}},
           [cb = std::move(callback)](int status, QByteArray data) {
               if (status == 204) cb(true, {});
               else {
                   auto doc = QJsonDocument::fromJson(data);
                   cb(false, doc.object()["detail"].toString("Ban failed"));
               }
           });
}

void ApiClient::getBans(int guildId, BansCb callback)
{
    doGet(QStringLiteral("/guilds/%1/bans").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200 && doc.isArray()) {
                  QList<BanInfo> bans;
                  for (const auto& v : doc.array())
                      bans.append(parseBan(v.toObject()));
                  cb(true, {}, bans);
              } else {
                  cb(false, doc.object()["detail"].toString("Failed to get bans"), {});
              }
          });
}

void ApiClient::unbanMember(int guildId, int userId, SimpleCb callback)
{
    doDelete(QStringLiteral("/guilds/%1/bans/%2").arg(guildId).arg(userId),
             [cb = std::move(callback)](int status, QByteArray data) {
                 if (status == 204) cb(true, {});
                 else {
                     auto doc = QJsonDocument::fromJson(data);
                     cb(false, doc.object()["detail"].toString("Unban failed"));
                 }
             });
}

// ── Server profile ────────────────────────────────────────────────────────────

void ApiClient::getServerProfile(int guildId, ServerProfileCb callback)
{
    doGet(QStringLiteral("/guilds/%1/server-profile").arg(guildId),
          [cb = std::move(callback)](int status, QByteArray data) {
              auto doc = QJsonDocument::fromJson(data);
              if (status == 200) cb(true, {}, parseServerProfile(doc.object()));
              else cb(false, doc.object()["detail"].toString("Failed to get profile"), {});
          });
}

void ApiClient::updateServerProfile(int guildId, const QString& nickname, ServerProfileCb callback)
{
    // Server uses PUT; we reuse the doPost/doPatch pattern by sending PUT manually
    QNetworkRequest req = makeRequest(QStringLiteral("/guilds/%1/server-profile").arg(guildId));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body{{"nickname", nickname}};
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_nam->put(req, payload);
    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(callback)]() {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QByteArray data = reply->readAll();
                reply->deleteLater();
                auto doc = QJsonDocument::fromJson(data);
                if (status == 200) cb(true, {}, parseServerProfile(doc.object()));
                else cb(false, doc.object()["detail"].toString("Failed to update profile"), {});
            });
}

void ApiClient::uploadServerAvatar(int guildId, const QByteArray& data,
                                    const QString& mime, SimpleCb callback)
{
    doMultipart(QStringLiteral("/guilds/%1/server-profile/avatar").arg(guildId),
                data, mime, "file", "avatar.png",
                [cb = std::move(callback)](int status, QByteArray resp) {
                    if (status == 204) cb(true, {});
                    else {
                        auto doc = QJsonDocument::fromJson(resp);
                        cb(false, doc.object()["detail"].toString("Upload failed"));
                    }
                });
}

void ApiClient::getMemberAvatar(int guildId, int userId,
                                 std::function<void(bool, const QByteArray&)> callback)
{
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/guilds/%1/members/%2/avatar")
                                            .arg(guildId).arg(userId)));
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(callback)]() {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QByteArray data = reply->readAll();
                reply->deleteLater();
                if (status == 200) cb(true, data);
                else cb(false, {});
            });
}
