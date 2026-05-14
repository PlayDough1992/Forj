#pragma once

#include <QTabWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QTreeWidget>
#include <QPixmap>
#include "ApiClient.h"
#include "ForjDialog.h"

class ServerSettingsDialog : public ForjDialog
{
    Q_OBJECT

public:
    explicit ServerSettingsDialog(ApiClient* api,
                                   const GuildInfo& guild,
                                   int currentUserId,
                                   const QString& currentUserRole,
                                   QWidget* parent = nullptr);

    // Emitted when the guild name or icon changes so MainWindow can refresh
    // the server list item.
    GuildInfo updatedGuild() const { return m_guild; }

private slots:
    void onSaveOverview();
    void onPickIcon();
    void onCreateRole();
    void onDeleteRole();
    void onRoleSelected(int row);
    void onSaveRole();
    void onMemberSelected(int row);
    void onKickMember();
    void onBanMember();
    void onAssignRole();
    void onRemoveRoleFromMember();
    void onUnban();
    void onSaveMyProfile();
    void onPickServerAvatar();
    void onCreateBot();
    void onDeleteBot();
    void onResetBotToken();
    void onRegisterCommand();
    void onDeleteCommand();
    void refreshBotList();
    void refreshCommandList();

private:
    void buildOverviewTab();
    void buildRolesTab();
    void buildMembersTab();
    void buildBansTab();
    void buildMyProfileTab();
    void buildBotsTab();

    void loadMembers();
    void loadRoles();
    void loadBans();
    void loadMyProfile();

    bool isPrivileged() const
    { return m_currentUserRole == "owner" || m_currentUserRole == "admin"; }
    bool isOwner() const { return m_currentUserRole == "owner"; }
    void updateInviteLabel();

    ApiClient*   m_api;
    GuildInfo    m_guild;
    int          m_currentUserId;
    QString      m_currentUserRole;

    QTabWidget* m_tabs{nullptr};

    // Overview tab
    QLabel*      m_iconPreview{nullptr};
    QLineEdit*   m_guildNameEdit{nullptr};
    QLabel*      m_inviteLabel{nullptr};
    QByteArray   m_pendingIconData;
    QString      m_pendingIconMime;

    // Roles tab
    QListWidget* m_roleList{nullptr};
    QLineEdit*   m_roleNameEdit{nullptr};
    QLineEdit*   m_roleColorEdit{nullptr};
    QCheckBox*   m_roleHoist{nullptr};
    // Permission checkboxes
    QCheckBox*   m_permSendMessages{nullptr};
    QCheckBox*   m_permManageMessages{nullptr};
    QCheckBox*   m_permManageChannels{nullptr};
    QCheckBox*   m_permKick{nullptr};
    QCheckBox*   m_permBan{nullptr};
    QCheckBox*   m_permManageRoles{nullptr};
    QCheckBox*   m_permManageGuild{nullptr};
    QCheckBox*   m_permAdministrator{nullptr};
    QCheckBox*   m_permWebhooks{nullptr};
    QCheckBox*   m_permBots{nullptr};
    QPushButton* m_saveRoleBtn{nullptr};
    QPushButton* m_deleteRoleBtn{nullptr};
    QList<RoleInfo> m_roles;
    int m_editingRoleId{0};

    // Members tab
    QListWidget* m_memberListWidget{nullptr};
    QLabel*      m_memberInfoLabel{nullptr};
    QComboBox*   m_assignRoleCombo{nullptr};
    QPushButton* m_assignRoleBtn{nullptr};
    QPushButton* m_removeRoleBtn{nullptr};
    QPushButton* m_kickBtn{nullptr};
    QPushButton* m_banBtn{nullptr};
    QList<MemberInfo> m_members;

    // Bans tab
    QListWidget* m_banList{nullptr};
    QPushButton* m_unbanBtn{nullptr};
    QList<BanInfo> m_bans;

    // My Profile tab
    QLabel*      m_serverAvatarPreview{nullptr};
    QLineEdit*   m_nicknameEdit{nullptr};
    QByteArray   m_pendingServerAvatarData;
    QString      m_pendingServerAvatarMime;

    // Bots tab
    QListWidget* m_botList{nullptr};
    QListWidget* m_commandList{nullptr};
    QLabel*      m_botTokenLabel{nullptr};
    QComboBox*   m_cmdBotCombo{nullptr};
    QLineEdit*   m_cmdNameEdit{nullptr};
    QLineEdit*   m_cmdDescEdit{nullptr};
    QList<BotInfo>     m_bots;
    QList<CommandInfo> m_commands;
};
