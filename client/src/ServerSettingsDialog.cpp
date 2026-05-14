#include "ServerSettingsDialog.h"
#include "AvatarCropDialog.h"
#include "ForjPrompt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QScrollArea>
#include <QColorDialog>
#include <QSplitter>

// Bitmask constants (must match server Perms)
static constexpr int P_SEND_MESSAGES   = 1 << 0;
static constexpr int P_MANAGE_MESSAGES = 1 << 1;
static constexpr int P_MANAGE_CHANNELS = 1 << 2;
static constexpr int P_KICK            = 1 << 3;
static constexpr int P_BAN             = 1 << 4;
static constexpr int P_MANAGE_ROLES    = 1 << 5;
static constexpr int P_MANAGE_GUILD    = 1 << 6;
static constexpr int P_ADMINISTRATOR   = 1 << 7;
static constexpr int P_WEBHOOKS        = 1 << 8;
static constexpr int P_BOTS            = 1 << 9;

ServerSettingsDialog::ServerSettingsDialog(ApiClient* api,
                                             const GuildInfo& guild,
                                             int currentUserId,
                                             const QString& currentUserRole,
                                             QWidget* parent)
    : ForjDialog(QStringLiteral("Server Settings \u2014 %1").arg(guild.name), parent)
    , m_api(api)
    , m_guild(guild)
    , m_currentUserId(currentUserId)
    , m_currentUserRole(currentUserRole)
{
    resize(760, 610);

    m_tabs = new QTabWidget(body());
    buildOverviewTab();
    buildRolesTab();
    buildMembersTab();
    buildBansTab();
    buildMyProfileTab();
    buildBotsTab();

    auto* root = new QVBoxLayout(body());
    root->addWidget(m_tabs);

    // Load data
    loadRoles();
    loadMembers();
    loadBans();
    loadMyProfile();
    refreshBotList();

    // Load server icon if exists
    if (m_guild.hasIcon) {
        m_api->getGuildIcon(m_guild.id, [this](bool ok, const QByteArray& data) {
            if (ok && !data.isEmpty()) {
                QPixmap pix;
                pix.loadFromData(data);
                m_iconPreview->setPixmap(avatarToCircle(pix, 80));
                m_iconPreview->setText({});
            }
        });
    }
}

// ── Overview Tab ──────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildOverviewTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // Icon row
    auto* iconRow = new QHBoxLayout;
    m_iconPreview = new QLabel;
    m_iconPreview->setFixedSize(80, 80);
    m_iconPreview->setStyleSheet("background:transparent;");
    m_iconPreview->setAlignment(Qt::AlignCenter);
    m_iconPreview->setText(m_guild.name.left(1).toUpper());

    auto* pickIconBtn = new QPushButton("Upload Icon");
    pickIconBtn->setFixedWidth(120);
    iconRow->addWidget(m_iconPreview);
    iconRow->addSpacing(12);
    iconRow->addWidget(pickIconBtn);
    iconRow->addStretch();
    layout->addLayout(iconRow);

    // Name
    auto* form = new QFormLayout;
    m_guildNameEdit = new QLineEdit(m_guild.name);
    m_guildNameEdit->setMaxLength(100);
    form->addRow("Server Name:", m_guildNameEdit);
    layout->addLayout(form);

    // Invite link
    m_inviteLabel = new QLabel;
    m_inviteLabel->setStyleSheet("color:#7289DA;");
    updateInviteLabel();
    auto* inviteRow = new QHBoxLayout;
    inviteRow->addWidget(new QLabel("Invite Code:"));
    inviteRow->addWidget(m_inviteLabel);
    auto* copyBtn = new QPushButton("Copy");
    copyBtn->setFixedWidth(60);
    inviteRow->addWidget(copyBtn);
    inviteRow->addStretch();
    layout->addLayout(inviteRow);

    auto* saveBtn = new QPushButton("Save Changes");
    layout->addWidget(saveBtn);
    layout->addStretch();

    m_tabs->addTab(page, "Overview");

    connect(pickIconBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onPickIcon);
    connect(saveBtn,     &QPushButton::clicked, this, &ServerSettingsDialog::onSaveOverview);
    connect(copyBtn,     &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_guild.inviteCode);
    });
}

void ServerSettingsDialog::updateInviteLabel()
{
    m_inviteLabel->setText(m_guild.inviteCode);
}

// ── Roles Tab ─────────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildRolesTab()
{
    auto* page    = new QWidget;
    auto* hLayout = new QHBoxLayout(page);
    hLayout->setContentsMargins(10, 10, 10, 10);

    // Left: role list + create/delete
    auto* leftWidget = new QWidget;
    leftWidget->setFixedWidth(200);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0,0,0,0);

    m_roleList = new QListWidget;
    leftLayout->addWidget(m_roleList);

    auto* btnRow = new QHBoxLayout;
    auto* newRoleBtn = new QPushButton("+ New Role");
    m_deleteRoleBtn  = new QPushButton("Delete");
    m_deleteRoleBtn->setEnabled(false);
    btnRow->addWidget(newRoleBtn);
    btnRow->addWidget(m_deleteRoleBtn);
    leftLayout->addLayout(btnRow);

    // Right: edit panel
    auto* rightScroll = new QScrollArea;
    rightScroll->setWidgetResizable(true);
    auto* rightWidget = new QWidget;
    auto* rightLayout = new QFormLayout(rightWidget);
    rightLayout->setContentsMargins(10, 10, 10, 10);

    m_roleNameEdit  = new QLineEdit;
    m_roleNameEdit->setMaxLength(50);
    m_roleNameEdit->setPlaceholderText("Role name");

    auto* colorRow = new QHBoxLayout;
    m_roleColorEdit = new QLineEdit("#99AAB5");
    m_roleColorEdit->setMaxLength(7);
    auto* pickColorBtn = new QPushButton("Pick");
    pickColorBtn->setFixedWidth(50);
    colorRow->addWidget(m_roleColorEdit);
    colorRow->addWidget(pickColorBtn);

    m_roleHoist = new QCheckBox("Display separately in member list");

    rightLayout->addRow("Name:",  m_roleNameEdit);
    rightLayout->addRow("Color:", colorRow);
    rightLayout->addRow("",       m_roleHoist);

    auto* permBox = new QGroupBox("Permissions");
    auto* permLayout = new QVBoxLayout(permBox);
    m_permAdministrator  = new QCheckBox("Administrator (all permissions)");
    m_permManageGuild    = new QCheckBox("Manage Server");
    m_permManageChannels = new QCheckBox("Manage Channels");
    m_permManageMessages = new QCheckBox("Manage Messages");
    m_permManageRoles    = new QCheckBox("Manage Roles");
    m_permKick           = new QCheckBox("Kick Members");
    m_permBan            = new QCheckBox("Ban Members");
    m_permWebhooks       = new QCheckBox("Manage Webhooks");
    m_permBots           = new QCheckBox("Manage Bots");
    m_permSendMessages   = new QCheckBox("Send Messages");
    permLayout->addWidget(m_permAdministrator);
    permLayout->addWidget(m_permManageGuild);
    permLayout->addWidget(m_permManageChannels);
    permLayout->addWidget(m_permManageMessages);
    permLayout->addWidget(m_permManageRoles);
    permLayout->addWidget(m_permKick);
    permLayout->addWidget(m_permBan);
    permLayout->addWidget(m_permWebhooks);
    permLayout->addWidget(m_permBots);
    permLayout->addWidget(m_permSendMessages);
    rightLayout->addRow(permBox);

    m_saveRoleBtn = new QPushButton("Save Role");
    m_saveRoleBtn->setEnabled(false);
    rightLayout->addRow(m_saveRoleBtn);

    rightScroll->setWidget(rightWidget);
    hLayout->addWidget(leftWidget);
    hLayout->addWidget(rightScroll, 1);

    m_tabs->addTab(page, "Roles");

    connect(newRoleBtn,    &QPushButton::clicked, this, &ServerSettingsDialog::onCreateRole);
    connect(m_deleteRoleBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onDeleteRole);
    connect(m_saveRoleBtn,   &QPushButton::clicked, this, &ServerSettingsDialog::onSaveRole);
    connect(m_roleList, &QListWidget::currentRowChanged,
            this, &ServerSettingsDialog::onRoleSelected);
    connect(pickColorBtn, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(QColor(m_roleColorEdit->text()), this, "Pick Color");
        if (c.isValid()) m_roleColorEdit->setText(c.name());
    });
}

// ── Members Tab ───────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildMembersTab()
{
    auto* page    = new QWidget;
    auto* hLayout = new QHBoxLayout(page);
    hLayout->setContentsMargins(10, 10, 10, 10);

    m_memberListWidget = new QListWidget;
    hLayout->addWidget(m_memberListWidget, 2);

    auto* rightWidget = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(10, 0, 0, 0);

    m_memberInfoLabel = new QLabel("Select a member");
    m_memberInfoLabel->setWordWrap(true);
    m_memberInfoLabel->setStyleSheet("color:#B9BBBE;");
    rightLayout->addWidget(m_memberInfoLabel);

    auto* rolesBox    = new QGroupBox("Roles");
    auto* rolesLayout = new QVBoxLayout(rolesBox);
    auto* assignRow   = new QHBoxLayout;
    m_assignRoleCombo = new QComboBox;
    m_assignRoleBtn   = new QPushButton("Assign");
    m_removeRoleBtn   = new QPushButton("Remove");
    assignRow->addWidget(m_assignRoleCombo, 1);
    assignRow->addWidget(m_assignRoleBtn);
    assignRow->addWidget(m_removeRoleBtn);
    rolesLayout->addLayout(assignRow);
    rightLayout->addWidget(rolesBox);

    auto* modBox    = new QGroupBox("Moderation");
    auto* modLayout = new QVBoxLayout(modBox);
    m_kickBtn = new QPushButton("Kick Member");
    m_banBtn  = new QPushButton("Ban Member");
    m_kickBtn->setStyleSheet("color:#FAA61A;");
    m_banBtn->setStyleSheet("color:#F04747;");
    modLayout->addWidget(m_kickBtn);
    modLayout->addWidget(m_banBtn);
    rightLayout->addWidget(modBox);
    rightLayout->addStretch();

    hLayout->addWidget(rightWidget, 1);

    // Only show moderation to privileged users
    if (!isPrivileged()) {
        modBox->setVisible(false);
        rolesBox->setVisible(false);
    }

    m_tabs->addTab(page, "Members");

    connect(m_memberListWidget, &QListWidget::currentRowChanged,
            this, &ServerSettingsDialog::onMemberSelected);
    connect(m_kickBtn,       &QPushButton::clicked, this, &ServerSettingsDialog::onKickMember);
    connect(m_banBtn,        &QPushButton::clicked, this, &ServerSettingsDialog::onBanMember);
    connect(m_assignRoleBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onAssignRole);
    connect(m_removeRoleBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onRemoveRoleFromMember);
}

// ── Bans Tab ──────────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildBansTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);

    m_banList = new QListWidget;
    layout->addWidget(m_banList);

    m_unbanBtn = new QPushButton("Unban Selected");
    m_unbanBtn->setEnabled(false);
    layout->addWidget(m_unbanBtn);

    m_tabs->addTab(page, "Bans");

    connect(m_banList, &QListWidget::currentRowChanged,
            this, [this](int row) { m_unbanBtn->setEnabled(row >= 0); });
    connect(m_unbanBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onUnban);
}

// ── My Profile Tab ────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildMyProfileTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* avatarRow = new QHBoxLayout;
    m_serverAvatarPreview = new QLabel;
    m_serverAvatarPreview->setFixedSize(80, 80);
    m_serverAvatarPreview->setStyleSheet("background: transparent;");
    m_serverAvatarPreview->setAlignment(Qt::AlignCenter);
    m_serverAvatarPreview->setText("?");

    auto* pickAvatarBtn = new QPushButton("Upload Avatar");
    pickAvatarBtn->setFixedWidth(130);
    auto* avatarNote = new QLabel("<small>Overrides global avatar in this server</small>");
    avatarNote->setStyleSheet("color:#72767D;");

    auto* avatarCol = new QVBoxLayout;
    avatarCol->addWidget(pickAvatarBtn);
    avatarCol->addWidget(avatarNote);
    avatarCol->addStretch();

    avatarRow->addWidget(m_serverAvatarPreview);
    avatarRow->addSpacing(12);
    avatarRow->addLayout(avatarCol);
    avatarRow->addStretch();
    layout->addLayout(avatarRow);

    auto* form = new QFormLayout;
    m_nicknameEdit = new QLineEdit;
    m_nicknameEdit->setMaxLength(32);
    m_nicknameEdit->setPlaceholderText("Your nickname in this server (optional)");
    form->addRow("Nickname:", m_nicknameEdit);
    layout->addLayout(form);

    auto* saveBtn = new QPushButton("Save Profile");
    layout->addWidget(saveBtn);
    layout->addStretch();

    m_tabs->addTab(page, "My Profile");

    connect(pickAvatarBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onPickServerAvatar);
    connect(saveBtn,       &QPushButton::clicked, this, &ServerSettingsDialog::onSaveMyProfile);
}

// ── Bots Tab ──────────────────────────────────────────────────────────────────

void ServerSettingsDialog::buildBotsTab()
{
    auto* page    = new QWidget;
    auto* hLayout = new QHBoxLayout(page);
    hLayout->setContentsMargins(10, 10, 10, 10);

    // Left: bot list
    auto* leftWidget = new QWidget;
    leftWidget->setFixedWidth(200);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->addWidget(new QLabel("<b>Bots</b>"));

    m_botList = new QListWidget;
    leftLayout->addWidget(m_botList);

    auto* botBtnRow = new QHBoxLayout;
    auto* createBotBtn = new QPushButton("+ Create Bot");
    auto* deleteBotBtn = new QPushButton("Delete");
    auto* resetTokenBtn= new QPushButton("Reset Token");
    botBtnRow->addWidget(createBotBtn);
    botBtnRow->addWidget(deleteBotBtn);
    leftLayout->addLayout(botBtnRow);
    leftLayout->addWidget(resetTokenBtn);

    m_botTokenLabel = new QLabel;
    m_botTokenLabel->setWordWrap(true);
    m_botTokenLabel->setStyleSheet("color:#43B581;font-family:monospace;font-size:10px;");
    leftLayout->addWidget(m_botTokenLabel);

    // Right: commands for selected bot
    auto* rightWidget = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(10,0,0,0);
    rightLayout->addWidget(new QLabel("<b>Slash Commands in this Server</b>"));

    m_commandList = new QListWidget;
    rightLayout->addWidget(m_commandList);

    auto* cmdForm = new QFormLayout;
    m_cmdBotCombo = new QComboBox;
    m_cmdNameEdit = new QLineEdit;
    m_cmdNameEdit->setMaxLength(32);
    m_cmdNameEdit->setPlaceholderText("command-name");
    m_cmdDescEdit = new QLineEdit;
    m_cmdDescEdit->setMaxLength(200);
    m_cmdDescEdit->setPlaceholderText("What it does");
    cmdForm->addRow("Bot:",         m_cmdBotCombo);
    cmdForm->addRow("Name:",        m_cmdNameEdit);
    cmdForm->addRow("Description:", m_cmdDescEdit);
    rightLayout->addLayout(cmdForm);

    auto* cmdBtnRow = new QHBoxLayout;
    auto* registerCmdBtn = new QPushButton("Register Command");
    auto* deleteCmdBtn   = new QPushButton("Delete Command");
    cmdBtnRow->addWidget(registerCmdBtn);
    cmdBtnRow->addWidget(deleteCmdBtn);
    rightLayout->addLayout(cmdBtnRow);

    hLayout->addWidget(leftWidget);
    hLayout->addWidget(rightWidget, 1);

    m_tabs->addTab(page, "Bots");

    connect(createBotBtn,  &QPushButton::clicked, this, &ServerSettingsDialog::onCreateBot);
    connect(deleteBotBtn,  &QPushButton::clicked, this, &ServerSettingsDialog::onDeleteBot);
    connect(resetTokenBtn, &QPushButton::clicked, this, &ServerSettingsDialog::onResetBotToken);
    connect(registerCmdBtn,&QPushButton::clicked, this, &ServerSettingsDialog::onRegisterCommand);
    connect(deleteCmdBtn,  &QPushButton::clicked, this, &ServerSettingsDialog::onDeleteCommand);
}

// ── Data loaders ──────────────────────────────────────────────────────────────

void ServerSettingsDialog::loadRoles()
{
    m_api->getRoles(m_guild.id, [this](bool ok, const QString&, const QList<RoleInfo>& roles) {
        m_roles = roles;
        m_roleList->clear();
        m_assignRoleCombo->clear();
        for (const auto& r : roles) {
            auto* item = new QListWidgetItem(r.name);
            item->setForeground(QColor(r.color));
            m_roleList->addItem(item);
            m_assignRoleCombo->addItem(r.name, r.id);
        }
    });
}

void ServerSettingsDialog::loadMembers()
{
    m_api->getMembers(m_guild.id, [this](bool ok, const QString&, const QList<MemberInfo>& members) {
        m_members = members;
        m_memberListWidget->clear();
        for (const auto& m : members) {
            const QString display = m.nickname.isEmpty()
                ? (m.displayName.isEmpty() ? m.username : m.displayName)
                : m.nickname;
            QString roleStr;
            for (const auto& r : m.roles)
                roleStr += " [" + r.name + "]";
            m_memberListWidget->addItem(display + roleStr);
        }
    });
}

void ServerSettingsDialog::loadBans()
{
    if (!isPrivileged()) return;
    m_api->getBans(m_guild.id, [this](bool ok, const QString&, const QList<BanInfo>& bans) {
        m_bans = bans;
        m_banList->clear();
        for (const auto& b : bans) {
            const QString reason = b.reason.isEmpty() ? "No reason" : b.reason;
            m_banList->addItem(QStringLiteral("%1 — %2").arg(b.username, reason));
        }
    });
}

void ServerSettingsDialog::loadMyProfile()
{
    m_api->getServerProfile(m_guild.id, [this](bool ok, const QString&, const ServerProfileInfo& sp) {
        if (!ok) return;
        m_nicknameEdit->setText(sp.nickname);
        if (sp.hasAvatar) {
            m_api->getMemberAvatar(m_guild.id, m_currentUserId,
                                   [this](bool ok2, const QByteArray& data) {
                if (ok2 && !data.isEmpty()) {
                    QPixmap pix;
                    pix.loadFromData(data);
                    m_serverAvatarPreview->setPixmap(avatarToCircle(pix, 80));
                    m_serverAvatarPreview->setText({});
                }
            });
        }
    });
}

// ── Slots: Overview ───────────────────────────────────────────────────────────

void ServerSettingsDialog::onPickIcon()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Select Icon", {}, "Images (*.png *.jpg *.jpeg *.gif *.webp)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray raw = f.readAll();

    QPixmap src;
    if (!src.loadFromData(raw) || src.isNull()) return;

    AvatarCropDialog dlg(src, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_pendingIconData = dlg.croppedPng();
    m_pendingIconMime = "image/png";

    QPixmap pix;
    pix.loadFromData(m_pendingIconData);
    m_iconPreview->setPixmap(avatarToCircle(pix, 80));
    m_iconPreview->setText({});
}

void ServerSettingsDialog::onSaveOverview()
{
    const QString name = m_guildNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    auto finish = [this, name](bool ok, const QString& err, const GuildInfo& g) {
        if (ok) {
            m_guild = g;
            setWindowTitle(QStringLiteral("Server Settings — %1").arg(g.name));
        } else {
            ForjPrompt::warning(this, "Error", err);
        }
    };

    if (!m_pendingIconData.isEmpty()) {
        m_api->uploadGuildIcon(m_guild.id, m_pendingIconData, m_pendingIconMime,
                               [this, name, finish](bool ok, const QString& err) {
            if (!ok) { ForjPrompt::warning(this, "Icon upload failed", err); }
            m_pendingIconData.clear();
            m_api->updateGuild(m_guild.id, name, finish);
        });
    } else {
        m_api->updateGuild(m_guild.id, name, finish);
    }
}

// ── Slots: Roles ──────────────────────────────────────────────────────────────

void ServerSettingsDialog::onRoleSelected(int row)
{
    if (row < 0 || row >= m_roles.size()) {
        m_saveRoleBtn->setEnabled(false);
        m_deleteRoleBtn->setEnabled(false);
        m_editingRoleId = 0;
        return;
    }
    const auto& r = m_roles[row];
    m_editingRoleId = r.id;
    m_roleNameEdit->setText(r.name);
    m_roleColorEdit->setText(r.color);
    m_roleHoist->setChecked(r.hoist);
    m_permAdministrator ->setChecked(r.permissions & P_ADMINISTRATOR);
    m_permManageGuild   ->setChecked(r.permissions & P_MANAGE_GUILD);
    m_permManageChannels->setChecked(r.permissions & P_MANAGE_CHANNELS);
    m_permManageMessages->setChecked(r.permissions & P_MANAGE_MESSAGES);
    m_permManageRoles   ->setChecked(r.permissions & P_MANAGE_ROLES);
    m_permKick          ->setChecked(r.permissions & P_KICK);
    m_permBan           ->setChecked(r.permissions & P_BAN);
    m_permWebhooks      ->setChecked(r.permissions & P_WEBHOOKS);
    m_permBots          ->setChecked(r.permissions & P_BOTS);
    m_permSendMessages  ->setChecked(r.permissions & P_SEND_MESSAGES);
    m_saveRoleBtn->setEnabled(true);
    m_deleteRoleBtn->setEnabled(true);
}

void ServerSettingsDialog::onCreateRole()
{
    const QString name = ForjPrompt::getText(this, "New Role", "Role name:");
    if (name.trimmed().isEmpty()) return;
    m_api->createRole(m_guild.id, name.trimmed(), "#99AAB5", 0, false,
                      [this](bool ok, const QString& err, const RoleInfo&) {
        if (ok) loadRoles();
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onDeleteRole()
{
    if (m_editingRoleId == 0) return;
    if (!ForjPrompt::question(this, "Delete Role", "Delete this role?"))
        return;
    m_api->deleteRole(m_guild.id, m_editingRoleId,
                      [this](bool ok, const QString& err) {
        if (ok) { m_editingRoleId = 0; loadRoles(); }
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onSaveRole()
{
    if (m_editingRoleId == 0) return;
    int perms = 0;
    if (m_permAdministrator ->isChecked()) perms |= P_ADMINISTRATOR;
    if (m_permManageGuild   ->isChecked()) perms |= P_MANAGE_GUILD;
    if (m_permManageChannels->isChecked()) perms |= P_MANAGE_CHANNELS;
    if (m_permManageMessages->isChecked()) perms |= P_MANAGE_MESSAGES;
    if (m_permManageRoles   ->isChecked()) perms |= P_MANAGE_ROLES;
    if (m_permKick          ->isChecked()) perms |= P_KICK;
    if (m_permBan           ->isChecked()) perms |= P_BAN;
    if (m_permWebhooks      ->isChecked()) perms |= P_WEBHOOKS;
    if (m_permBots          ->isChecked()) perms |= P_BOTS;
    if (m_permSendMessages  ->isChecked()) perms |= P_SEND_MESSAGES;

    m_api->updateRole(m_guild.id, m_editingRoleId,
                      m_roleNameEdit->text().trimmed(),
                      m_roleColorEdit->text(),
                      perms,
                      m_roleHoist->isChecked(),
                      [this](bool ok, const QString& err, const RoleInfo&) {
        if (ok) loadRoles();
        else ForjPrompt::warning(this, "Error", err);
    });
}

// ── Slots: Members ────────────────────────────────────────────────────────────

void ServerSettingsDialog::onMemberSelected(int row)
{
    if (row < 0 || row >= m_members.size()) {
        m_memberInfoLabel->setText("Select a member");
        return;
    }
    const auto& m = m_members[row];
    QString info = QStringLiteral("<b>%1</b>").arg(m.username);
    if (!m.nickname.isEmpty())
        info += QStringLiteral(" (nick: %1)").arg(m.nickname);
    info += QStringLiteral("<br>Role: %1").arg(m.role);
    if (!m.roles.isEmpty()) {
        QStringList rnames;
        for (const auto& r : m.roles) rnames << r.name;
        info += "<br>Roles: " + rnames.join(", ");
    }
    m_memberInfoLabel->setText(info);
}

void ServerSettingsDialog::onKickMember()
{
    int row = m_memberListWidget->currentRow();
    if (row < 0 || row >= m_members.size()) return;
    const auto& target = m_members[row];
    if (target.userId == m_currentUserId) return;
    if (!ForjPrompt::question(this, "Kick",
            QStringLiteral("Kick %1?").arg(target.username)))
        return;
    m_api->kickMember(m_guild.id, target.userId,
                      [this](bool ok, const QString& err) {
        if (ok) loadMembers();
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onBanMember()
{
    int row = m_memberListWidget->currentRow();
    if (row < 0 || row >= m_members.size()) return;
    const auto& target = m_members[row];
    if (target.userId == m_currentUserId) return;
    const QString reason = ForjPrompt::getText(
        this, "Ban Member", QStringLiteral("Reason for banning %1:").arg(target.username));
    m_api->banMember(m_guild.id, target.userId, reason,
                     [this](bool ok, const QString& err) {
        if (ok) { loadMembers(); loadBans(); }
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onAssignRole()
{
    int memberRow = m_memberListWidget->currentRow();
    if (memberRow < 0 || memberRow >= m_members.size()) return;
    int roleId = m_assignRoleCombo->currentData().toInt();
    if (roleId == 0) return;
    const auto& target = m_members[memberRow];
    m_api->assignRole(m_guild.id, target.userId, roleId,
                      [this](bool ok, const QString& err) {
        if (ok) loadMembers();
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onRemoveRoleFromMember()
{
    int memberRow = m_memberListWidget->currentRow();
    if (memberRow < 0 || memberRow >= m_members.size()) return;
    int roleId = m_assignRoleCombo->currentData().toInt();
    if (roleId == 0) return;
    const auto& target = m_members[memberRow];
    m_api->removeRole(m_guild.id, target.userId, roleId,
                      [this](bool ok, const QString& err) {
        if (ok) loadMembers();
        else ForjPrompt::warning(this, "Error", err);
    });
}

// ── Slots: Bans ───────────────────────────────────────────────────────────────

void ServerSettingsDialog::onUnban()
{
    int row = m_banList->currentRow();
    if (row < 0 || row >= m_bans.size()) return;
    const auto& ban = m_bans[row];
    m_api->unbanMember(m_guild.id, ban.userId,
                       [this](bool ok, const QString& err) {
        if (ok) loadBans();
        else ForjPrompt::warning(this, "Error", err);
    });
}

// ── Slots: My Profile ─────────────────────────────────────────────────────────

void ServerSettingsDialog::onPickServerAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Select Avatar", {}, "Images (*.png *.jpg *.jpeg *.gif *.webp)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray raw = f.readAll();

    QPixmap src;
    if (!src.loadFromData(raw) || src.isNull()) return;

    AvatarCropDialog dlg(src, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_pendingServerAvatarData = dlg.croppedPng();
    m_pendingServerAvatarMime = "image/png";

    QPixmap preview;
    preview.loadFromData(m_pendingServerAvatarData);
    m_serverAvatarPreview->setPixmap(avatarToCircle(preview, 80));
    m_serverAvatarPreview->setText({});
}

void ServerSettingsDialog::onSaveMyProfile()
{
    const QString nickname = m_nicknameEdit->text().trimmed();
    auto doUpdate = [this, nickname] {
        m_api->updateServerProfile(m_guild.id, nickname,
                                   [this](bool ok, const QString& err, const ServerProfileInfo&) {
            if (!ok) ForjPrompt::warning(this, "Error", err);
        });
    };

    if (!m_pendingServerAvatarData.isEmpty()) {
        m_api->uploadServerAvatar(m_guild.id, m_pendingServerAvatarData,
                                  m_pendingServerAvatarMime,
                                  [this, doUpdate](bool ok, const QString& err) {
            if (!ok) ForjPrompt::warning(this, "Avatar Error", err);
            m_pendingServerAvatarData.clear();
            doUpdate();
        });
    } else {
        doUpdate();
    }
}

// ── Slots: Bots ───────────────────────────────────────────────────────────────

void ServerSettingsDialog::refreshBotList()
{
    m_api->getBots([this](bool ok, const QString&, const QList<BotInfo>& bots) {
        m_bots = bots;
        m_botList->clear();
        m_cmdBotCombo->clear();
        for (const auto& b : bots) {
            m_botList->addItem(b.name.isEmpty() ? b.username : b.name);
            m_cmdBotCombo->addItem(b.name.isEmpty() ? b.username : b.name, b.id);
        }
        refreshCommandList();
    });
}

void ServerSettingsDialog::refreshCommandList()
{
    m_api->getCommands(m_guild.id, [this](bool ok, const QString&, const QList<CommandInfo>& cmds) {
        m_commands = cmds;
        m_commandList->clear();
        for (const auto& c : cmds)
            m_commandList->addItem(QStringLiteral("/%1 — %2 [%3]")
                                   .arg(c.name, c.description, c.botUsername));
    });
}

void ServerSettingsDialog::onCreateBot()
{
    const QString name = ForjPrompt::getText(this, "Create Bot", "Bot name:");
    if (name.trimmed().isEmpty()) return;
    m_api->createBot(name.trimmed(), [this](bool ok, const QString& err, const BotInfo& bot) {
        if (!ok) { ForjPrompt::warning(this, "Error", err); return; }
        ForjPrompt::information(this, "Bot Created",
            QStringLiteral("Bot created!\nToken: %1\n\nSave this — it won't be shown again.")
                .arg(bot.token));
        refreshBotList();
    });
}

void ServerSettingsDialog::onDeleteBot()
{
    int row = m_botList->currentRow();
    if (row < 0 || row >= m_bots.size()) return;
    if (!ForjPrompt::question(this, "Delete Bot",
            QStringLiteral("Delete bot \"%1\"?").arg(m_bots[row].name)))
        return;
    m_api->deleteBot(m_bots[row].id, [this](bool ok, const QString& err) {
        if (ok) refreshBotList();
        else ForjPrompt::warning(this, "Error", err);
    });
}

void ServerSettingsDialog::onResetBotToken()
{
    int row = m_botList->currentRow();
    if (row < 0 || row >= m_bots.size()) return;
    m_api->resetBotToken(m_bots[row].id, [this](bool ok, const QString& err, const BotInfo& bot) {
        if (!ok) { ForjPrompt::warning(this, "Error", err); return; }
        m_botTokenLabel->setText(QStringLiteral("New token: %1").arg(bot.token));
    });
}

void ServerSettingsDialog::onRegisterCommand()
{
    const QString cmdName = m_cmdNameEdit->text().trimmed().toLower();
    const QString cmdDesc = m_cmdDescEdit->text().trimmed();
    int botId = m_cmdBotCombo->currentData().toInt();
    if (cmdName.isEmpty() || cmdDesc.isEmpty() || botId == 0) return;

    // Use the existing API for registering commands
    // POST /guilds/{id}/commands  with {bot_id, name, description}
    // This is done via a raw POST since ApiClient doesn't have a dedicated method yet
    // We'll wire it through a simple lambda using the internal doPost
    // For now show a placeholder
    ForjPrompt::information(this, "Slash Command",
        "Command registration coming soon — use the API directly for now.");
}

void ServerSettingsDialog::onDeleteCommand()
{
    int row = m_commandList->currentRow();
    if (row < 0 || row >= m_commands.size()) return;
    ForjPrompt::information(this, "Delete Command",
        "Command deletion coming soon — use the API directly.");
}
