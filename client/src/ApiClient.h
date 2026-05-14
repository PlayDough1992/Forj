#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <optional>

// ── Plain data structs ─────────────────────────────────────────────────────────

struct UserInfo {
    int     id{0};
    QString username;
    QString email;
    QString displayName;
    QString bio;
    bool    hasAvatar{false};
    bool    isBot{false};
    QString createdAt;
    QString status{"online"};
    QString kycStatus{"unverified"};   // unverified|pending|approved|declined
    QString kycVerificationUrl;        // non-empty immediately after register
};

struct GuildInfo {
    int     id{0};
    QString name;
    int     ownerId{0};
    QString inviteCode;
    bool    hasIcon{false};
};

struct ChannelInfo {
    int     id{0};
    int     guildId{0};
    QString name;
    QString type;
};

struct MessageInfo {
    int     id{0};
    int     channelId{0};
    int     authorId{0};
    QString authorUsername;
    bool    authorHasAvatar{false};
    bool    authorIsBot{false};
    QString content;
    QString createdAt;
    int     replyToId{0};
    QString replyToAuthor;
    QString replyToContent;
};

struct RoleInfo {
    int     id{0};
    int     guildId{0};
    QString name;
    QString color{"#99AAB5"};
    int     permissions{0};
    int     position{0};
    bool    hoist{false};
};

struct MemberInfo {
    int     userId{0};
    QString username;
    QString displayName;
    QString nickname;
    QString role;
    bool    isOnline{false};
    bool    hasAvatar{false};
    bool    isBot{false};
    QList<RoleInfo> roles;
    QString status{"offline"};
};

struct ServerProfileInfo {
    int     guildId{0};
    int     userId{0};
    QString nickname;
    bool    hasAvatar{false};
};

struct BanInfo {
    int     guildId{0};
    int     userId{0};
    QString username;
    QString reason;
};

struct DmConversationInfo {
    int     id{0};
    int     otherUserId{0};
    QString otherUsername;
    QString otherDisplayName;
    bool    otherHasAvatar{false};
};

struct DmMessageInfo {
    int     id{0};
    int     conversationId{0};
    int     authorId{0};
    QString authorUsername;
    bool    authorHasAvatar{false};
    bool    authorIsBot{false};
    QString content;
    QString createdAt;
    int     replyToId{0};
    QString replyToAuthor;
    QString replyToContent;
};

struct BotInfo {
    int     id{0};
    int     userId{0};
    QString username;
    QString name;
    QString token;   // only set at creation / token reset
};

struct CommandInfo {
    int     id{0};
    int     guildId{0};
    QString name;
    QString description;
    QString botUsername;
};

struct WebhookInfo {
    int     id{0};
    int     channelId{0};
    QString name;
    QString token;
    QString url;
};

struct FriendInfo {
    int     userId{0};
    QString username;
    QString displayName;
    bool    hasAvatar{false};
    QString status{"offline"};
};

struct FriendRequestInfo {
    int     fromUserId{0};
    QString fromUsername;
    QString fromDisplayName;
    bool    fromHasAvatar{false};
};

struct VoiceParticipant {
    int     userId{0};
    QString username;
};

// ── Callback aliases ──────────────────────────────────────────────────────────

using AuthCb      = std::function<void(bool, const QString&, const QString&, const UserInfo&)>;
using UserCb      = std::function<void(bool, const QString&, const UserInfo&)>;
using SimpleCb    = std::function<void(bool, const QString&)>;
using GuildCb     = std::function<void(bool, const QString&, const GuildInfo&)>;
using GuildsCb    = std::function<void(bool, const QString&, const QList<GuildInfo>&)>;
using ChannelCb   = std::function<void(bool, const QString&, const ChannelInfo&)>;
using ChannelsCb  = std::function<void(bool, const QString&, const QList<ChannelInfo>&)>;
using MessageCb   = std::function<void(bool, const QString&, const MessageInfo&)>;
using MessagesCb  = std::function<void(bool, const QString&, const QList<MessageInfo>&)>;
using MembersCb   = std::function<void(bool, const QString&, const QList<MemberInfo>&)>;
using DmsCb       = std::function<void(bool, const QString&, const QList<DmConversationInfo>&)>;
using DmCb        = std::function<void(bool, const QString&, const DmConversationInfo&)>;
using DmMsgCb     = std::function<void(bool, const QString&, const DmMessageInfo&)>;
using DmMsgsCb    = std::function<void(bool, const QString&, const QList<DmMessageInfo>&)>;
using BotCb       = std::function<void(bool, const QString&, const BotInfo&)>;
using BotsCb      = std::function<void(bool, const QString&, const QList<BotInfo>&)>;
using CommandsCb  = std::function<void(bool, const QString&, const QList<CommandInfo>&)>;
using WebhooksCb  = std::function<void(bool, const QString&, const QList<WebhookInfo>&)>;
using WebhookCb   = std::function<void(bool, const QString&, const WebhookInfo&)>;
using RoleCb      = std::function<void(bool, const QString&, const RoleInfo&)>;
using RolesCb     = std::function<void(bool, const QString&, const QList<RoleInfo>&)>;
using BansCb      = std::function<void(bool, const QString&, const QList<BanInfo>&)>;
using ServerProfileCb = std::function<void(bool, const QString&, const ServerProfileInfo&)>;
using FriendsCb    = std::function<void(bool, const QString&, const QList<FriendInfo>&)>;
using FriendReqsCb = std::function<void(bool, const QString&, const QList<FriendRequestInfo>&)>;
using VoiceParticipantsCb = std::function<void(bool, const QString&, const QList<VoiceParticipant>&)>;


// ── ApiClient ─────────────────────────────────────────────────────────────────

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url);
    void setToken(const QString& token);
    [[nodiscard]] bool hasToken() const;

    // Auth
    void registerUser(const QString& username,
                      const QString& email,
                      const QString& password,
                      AuthCb callback);

    void login(const QString& username,
               const QString& password,
               AuthCb callback);

    // User profile
    void updateProfile(const QString& displayName, const QString& bio, UserCb callback);
    void uploadAvatar(const QByteArray& data, const QString& mimeType, SimpleCb callback);
    void getAvatar(int userId, std::function<void(bool, const QByteArray&)> callback);
    void changePassword(const QString& current, const QString& newPw, SimpleCb callback);
    void setStatus(const QString& userStatus, SimpleCb callback);
    void searchUsers(const QString& query,
                     std::function<void(bool, const QString&, const QList<UserInfo>&)> callback);

    // Guilds
    void getGuilds(GuildsCb callback);
    void createGuild(const QString& name, GuildCb callback);
    void joinGuild(const QString& inviteCode, GuildCb callback);
    void leaveGuild(int guildId, SimpleCb callback);
    void updateGuild(int guildId, const QString& name, GuildCb callback);
    void deleteGuild(int guildId, SimpleCb callback);
    void uploadGuildIcon(int guildId, const QByteArray& data, const QString& mime, SimpleCb callback);
    void getGuildIcon(int guildId, std::function<void(bool, const QByteArray&)> callback);

    // Channels
    void getChannels(int guildId, ChannelsCb callback);
    void createChannel(int guildId, const QString& name, ChannelCb callback,
                       const QString& type = QStringLiteral("text"));

    // Voice
    void getVoiceState(int channelId, VoiceParticipantsCb callback);
    void joinVoice(int channelId, SimpleCb callback);
    void leaveVoice(int channelId, SimpleCb callback);

    // Messages
    void getMessages(int channelId, int limit, MessagesCb callback);
    void sendMessage(int channelId, const QString& content, MessageCb callback,
                     int replyToId = 0);

    // Slash-command invocation
    void invokeCommand(int channelId, const QString& name, const QString& args,
                       MessageCb callback);
    void getCommands(int guildId, CommandsCb callback);

    // Members
    void getMembers(int guildId, MembersCb callback);

    // Roles
    void getRoles(int guildId, RolesCb callback);
    void createRole(int guildId, const QString& name, const QString& color,
                    int permissions, bool hoist, RoleCb callback);
    void updateRole(int guildId, int roleId, const QString& name, const QString& color,
                    int permissions, bool hoist, RoleCb callback);
    void deleteRole(int guildId, int roleId, SimpleCb callback);
    void assignRole(int guildId, int userId, int roleId, SimpleCb callback);
    void removeRole(int guildId, int userId, int roleId, SimpleCb callback);

    // Moderation
    void kickMember(int guildId, int userId, SimpleCb callback);
    void banMember(int guildId, int userId, const QString& reason, SimpleCb callback);
    void getBans(int guildId, BansCb callback);
    void unbanMember(int guildId, int userId, SimpleCb callback);

    // Server profile
    void getServerProfile(int guildId, ServerProfileCb callback);
    void updateServerProfile(int guildId, const QString& nickname, ServerProfileCb callback);
    void uploadServerAvatar(int guildId, const QByteArray& data, const QString& mime, SimpleCb callback);
    void getMemberAvatar(int guildId, int userId, std::function<void(bool, const QByteArray&)> callback);

    // Direct Messages
    void getDms(DmsCb callback);
    void openDm(int userId, DmCb callback);
    void getDmMessages(int dmId, int limit, DmMsgsCb callback);
    void sendDm(int dmId, const QString& content, DmMsgCb callback,
                int replyToId = 0);

    // Bots
    void getBots(BotsCb callback);
    void createBot(const QString& name, BotCb callback);
    void deleteBot(int botId, SimpleCb callback);
    void resetBotToken(int botId, BotCb callback);

    // Webhooks
    void getWebhooks(int channelId, WebhooksCb callback);
    void createWebhook(int channelId, const QString& name, WebhookCb callback);
    void deleteWebhook(int webhookId, SimpleCb callback);

    // Friends
    void getFriends(FriendsCb callback);
    void getFriendRequests(FriendReqsCb callback);
    void sendFriendRequest(int userId, SimpleCb callback);
    void acceptFriendRequest(int userId, SimpleCb callback);
    void declineFriendRequest(int userId, SimpleCb callback);
    void removeFriend(int userId, SimpleCb callback);
    void inviteUserToGuild(int guildId, int userId, SimpleCb callback);

    // KYC — native verification wizard
    // Initiate a new KYC session; callback(ok, verificationUrl, errorMsg)
    void initiateKyc(std::function<void(bool, const QString&, const QString&)> callback);
    // Poll current KYC status; callback(ok, status)  status = "unverified"|"approved"|"declined"
    void getKycStatus(std::function<void(bool, const QString&)> callback);
    // Direct-upload proxy (kept for future use when credits are available)
    void submitKyc(const QByteArray& frontImage, const QString& frontMime,
                   const QByteArray& backImage,  const QString& backMime,
                   const QByteArray& selfieImage, const QString& selfieMime,
                   std::function<void(bool, const QString&)> callback);

private:
    QNetworkRequest makeRequest(const QString& endpoint) const;
    void            doPost(const QString& endpoint,
                            const QJsonObject& body,
                            std::function<void(int, QByteArray)> handler);
    void            doGet(const QString& endpoint,
                           std::function<void(int, QByteArray)> handler);
    void            doDelete(const QString& endpoint,
                              std::function<void(int, QByteArray)> handler);
    void            doPatch(const QString& endpoint,
                             const QJsonObject& body,
                             std::function<void(int, QByteArray)> handler);
    void            doMultipart(const QString& endpoint,
                                 const QByteArray& data,
                                 const QString& mimeType,
                                 const QString& fieldName,
                                 const QString& fileName,
                                 std::function<void(int, QByteArray)> handler);

    static UserInfo           parseUser(const QJsonObject& o);
    static GuildInfo          parseGuild(const QJsonObject& o);
    static ChannelInfo        parseChannel(const QJsonObject& o);
    static MessageInfo        parseMessage(const QJsonObject& o);
    static MemberInfo         parseMember(const QJsonObject& o);
    static RoleInfo           parseRole(const QJsonObject& o);
    static ServerProfileInfo  parseServerProfile(const QJsonObject& o);
    static BanInfo            parseBan(const QJsonObject& o);
    static DmConversationInfo parseDmConv(const QJsonObject& o);
    static DmMessageInfo      parseDmMsg(const QJsonObject& o);
    static BotInfo            parseBot(const QJsonObject& o);
    static CommandInfo        parseCommand(const QJsonObject& o);
    static WebhookInfo        parseWebhook(const QJsonObject& o);
    static FriendInfo         parseFriend(const QJsonObject& o);
    static FriendRequestInfo  parseFriendRequest(const QJsonObject& o);

    QNetworkAccessManager* m_nam{nullptr};
    QString m_baseUrl;
    QString m_token;
};
