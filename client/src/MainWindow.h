#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QCompleter>
#include <QStringListModel>
#include <QHash>
#include <QSet>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QAction>
#include <functional>
#include "ApiClient.h"
#include "WebSocketClient.h"
#include "ServerSettingsDialog.h"
#include "FriendsDialog.h"
#include "TitleBar.h"

class VoiceEngine;


// ── Notification record ────────────────────────────────────────────────────────
struct NotificationItem {
    enum class Type { Mention, DM, FriendRequest };
    Type    type{Type::Mention};
    QString title;
    QString body;
    QString timestamp;
    int     guildId{0};
    int     channelId{0};
    int     dmId{0};
    int     fromUserId{0};
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ApiClient* api, WebSocketClient* ws,
                        const UserInfo& me, QWidget* parent = nullptr);

private slots:
    // Navigation
    void onGuildSelected(int row);
    void onChannelSelected(int row);
    void onDmSelected(int row);

    // Actions
    void onSendMessage();
    void onCreateGuildClicked();
    void onJoinGuildClicked();
    void onCreateChannelClicked();
    void onOpenDmClicked();
    void onSettingsClicked();
    void onNotifyClicked();
    void onFriendsClicked();
    void onFriendRequest(int fromId, const QString& fromUsername, const QString& fromDisplayName);
    void onFriendAccepted(int byId, const QString& byUsername, const QString& byDisplayName);
    void onGuildInviteReceived(int guildId, const QString& guildName,
                               const QString& inviteCode, const QString& inviterUsername);
    void onInviteToGuildRequested(int userId, const QString& username);
    void onChannelContextMenu(const QPoint& pos);
    void onGuildContextMenu(const QPoint& pos);

    // Voice
    void onVoiceJoin(int channelId, int userId, const QString& username);
    void onVoiceLeave(int channelId, int userId, const QString& username);
    void onLeaveVoiceClicked();
    void onVoiceSpeaking(int channelId, int userId, bool speaking);

    // WebSocket events
    void onNewMessage(const MessageInfo& msg);
    void onNewDmMessage(const DmMessageInfo& msg);
    void onUserOnline(int userId, const QString& username);
    void onUserOffline(int userId, const QString& username);
    void onUserStatusChanged(int userId, const QString& username, const QString& status);
    void onMentioned(int guildId, int channelId, const QString& channelName,
                     const QString& author, const QString& content);

    // Slash autocomplete
    void onInputChanged(const QString& text);

private:
    void buildUi();
    void buildVoiceBar();
    void buildVoiceRoomPane();
    void refreshVoiceRoomTiles();
    void changeEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    void loadGuilds();
    void loadChannels(int guildId);
    void loadVoiceParticipants(int channelId);
    void updateVoiceChannelItem(int channelId);
    QString channelItemText(const ChannelInfo& c) const;
    void loadMessages(int channelId);
    void loadMembers(int guildId);
    void loadDms();
    void loadDmMessages(int dmId);
    void appendGuildMessage(const MessageInfo& msg);
    void appendDmMessage(const DmMessageInfo& msg);
    void appendChatLine(const QString& author, int authorId,
                        const QString& content, const QString& timestamp,
                        bool isBot, const QString& avatarCell,
                        const QString& roleColor = {},
                        const QString& roleName  = {},
                        int msgId = 0,
                        const QString& replyToAuthor  = {},
                        const QString& replyToContent = {});
    void fetchAvatarThen(int userId, bool hasAvatar,
                         std::function<void(const QString& avatarCell)> cb);
    void refreshMemberOnlineStatus(int userId, bool online);
    void updateMyStatusDot();
    void updateCommands();
    static QString colorForId(int id);
    void openDmWithUser(const UserInfo& user);
    void showUserContextMenu(int userId, const QPoint& globalPos);
    bool eventFilter(QObject* obj, QEvent* e) override;
    void onMemberContextMenu(const QPoint& pos);
    void onDmContextMenu(const QPoint& pos);

    ApiClient*       m_api{nullptr};
    WebSocketClient* m_ws{nullptr};
    UserInfo         m_me;

    // Avatar cache: userId -> HTML table-cell snippet with either <img> or colored square
    QHash<int, QString> m_avatarCache;
    QSet<int>           m_avatarFetching;
    QHash<int, QList<std::function<void(const QString&)>>> m_avatarPending;

    // Per-guild member avatar cache: "guildId:userId" -> HTML snippet
    QHash<QString, QString> m_memberAvatarCache;
    QSet<QString>           m_memberAvatarFetching;
    QHash<QString, QList<std::function<void(const QString&)>>> m_memberAvatarPending;
    void fetchMemberAvatarThen(int guildId, int userId,
                               std::function<void(const QString&)> cb);

    // Per-guild member icon cache for the member list
    QHash<QString, QIcon>   m_memberIconCache;
    QSet<QString>           m_memberIconFetching;
    QHash<QString, QList<std::function<void(const QIcon&)>>> m_memberIconPending;
    void fetchMemberIconThen(int guildId, int userId,
                             std::function<void(const QIcon&)> cb);

    // Guild icon cache: guildId -> QIcon
    QHash<int, QIcon>   m_guildIconCache;
    QSet<int>           m_guildIconFetching;
    void fetchGuildIconThen(int guildId, bool hasIcon,
                            std::function<void(const QIcon&)> cb);

    // Data
    QList<GuildInfo>           m_guilds;
    QList<ChannelInfo>         m_channels;
    QList<MemberInfo>          m_members;
    QList<DmConversationInfo>  m_dms;
    QList<CommandInfo>         m_commands;
    int m_currentGuildId{0};
    int m_currentChannelId{0};
    int m_currentDmId{0};
    bool m_inDmMode{false};

    // channelId → guildId map (built as user navigates; used for cross-guild unread)
    QHash<int, int>  m_channelGuildMap;

    // Unread badges
    QHash<int, int>  m_unreadChannels;   // channelId → count
    QHash<int, int>  m_unreadDms;        // dmId      → count
    QSet<int>        m_mentionChannels;  // channels with pending @mention
    QSet<int>        m_unreadGuilds;     // guildIds with any unread

    // Voice state
    int  m_currentVoiceChannelId{0};
    bool m_voiceMuted{false};
    bool m_voiceDeafened{false};
    // channelId -> list of participants
    QHash<int, QList<VoiceParticipant>> m_voiceParticipants;
    // userId -> speaking state (for ring updates without full tile rebuild)
    QHash<int, bool>    m_voiceSpeakingState;
    // userId -> VoiceAvatarWidget* (cast to QWidget* for header independence)
    QHash<int, QWidget*> m_voiceTiles;
    // Audio engine (non-null while connected to a voice channel)
    VoiceEngine*        m_voiceEngine{nullptr};

    // Reply state
    int     m_replyToMsgId{0};
    QString m_replyToAuthor;
    QString m_replyToContent;
    // msg-id → {displayAuthor, contentSnippet} for reply-button lookups
    QHash<int, QPair<QString,QString>> m_msgReplyCache;

    // Tray icon for mention / DM notifications
    QSystemTrayIcon* m_trayIcon{nullptr};

    // Notification inbox
    QList<NotificationItem> m_notifications;
    int                     m_unreadNotifications{0};
    QAction*                m_notifyAction{nullptr};

    // Custom title bar
    TitleBar*               m_titleBar{nullptr};

    // Friends dialog (non-modal, kept alive while open)
    FriendsDialog*          m_friendsDialog{nullptr};

    // My own status
    QString  m_myStatus{"online"};
    QLabel*  m_myStatusDotLabel{nullptr};

    // UI
    QSplitter*    m_splitter{nullptr};

    // Voice status bar (shown at bottom of channel panel when in voice)
    QWidget*      m_voiceBar{nullptr};
    QLabel*       m_voiceBarLabel{nullptr};
    QPushButton*  m_voiceLeaveBtn{nullptr};

    // Voice room view (replaces chat area when in a voice channel)
    QStackedWidget* m_chatStack{nullptr};          // index 0=chat, 1=voice room
    QWidget*        m_voiceRoomPane{nullptr};
    QLabel*         m_voiceRoomChannelLabel{nullptr};
    QWidget*        m_participantContainer{nullptr};
    QGridLayout*    m_participantGrid{nullptr};
    QPushButton*    m_voiceMuteBtn{nullptr};
    QPushButton*    m_voiceDeafenBtn{nullptr};

    // Left pane (servers + DMs stacked/combined)
    QListWidget*  m_guildList{nullptr};
    QListWidget*  m_dmList{nullptr};

    // Center-left: channel / DM info
    QLabel*       m_guildNameLabel{nullptr};
    QListWidget*  m_channelList{nullptr};
    QPushButton*  m_addChannelBtn{nullptr};

    // Chat
    QTextBrowser* m_messageView{nullptr};
    QWidget*      m_replyBar{nullptr};       // shown while replying
    QLabel*       m_replyLabel{nullptr};
    QPushButton*  m_replyCancelBtn{nullptr};
    QLineEdit*    m_messageInput{nullptr};
    QPushButton*  m_sendBtn{nullptr};

    // Members (right pane)
    QListWidget*  m_memberList{nullptr};

    // Slash autocomplete
    QCompleter*        m_completer{nullptr};
    QStringListModel*  m_completerModel{nullptr};
};
