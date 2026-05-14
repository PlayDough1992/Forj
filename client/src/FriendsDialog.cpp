#include "FriendsDialog.h"
#include "ForjPrompt.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QScrollArea>
#include <QListWidgetItem>
#include <QToolButton>
#include <QMenu>
#include <QFont>
#include <QPainter>
#include <QPixmap>

// ── Helpers ───────────────────────────────────────────────────────────────────

QLabel* FriendsDialog::makeStatusDot(const QString& status)
{
    static const QMap<QString, QColor> kColors = {
        {"online",    QColor(0x23, 0xa5, 0x59)},
        {"idle",      QColor(0xf0, 0xa5, 0x00)},
        {"dnd",       QColor(0xed, 0x43, 0x37)},
        {"offline",   QColor(0x74, 0x78, 0x7c)},
    };
    constexpr int sz = 12;
    QPixmap pix(sz, sz);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QColor col = kColors.value(status, kColors["offline"]);
    p.setBrush(col);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, sz, sz);
    auto* lbl = new QLabel;
    lbl->setPixmap(pix);
    lbl->setFixedSize(sz, sz);
    return lbl;
}

// ── Constructor ───────────────────────────────────────────────────────────────

FriendsDialog::FriendsDialog(ApiClient* api, const UserInfo& me, QWidget* parent)
    : ForjDialog("Friends", parent), m_api(api), m_me(me)
{
    setMinimumSize(520, 510);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* mainLayout = new QVBoxLayout(body());

    m_tabs = new QTabWidget(this);
    mainLayout->addWidget(m_tabs);

    // ── Tab 1: Friends ────────────────────────────────────────────────────────
    auto* friendsTab = new QWidget;
    auto* friendsLayout = new QVBoxLayout(friendsTab);
    m_friendsList = new QListWidget(friendsTab);
    m_friendsList->setSpacing(2);
    m_friendsList->setSelectionMode(QAbstractItemView::NoSelection);
    friendsLayout->addWidget(m_friendsList);
    m_tabs->addTab(friendsTab, "Friends");

    // ── Tab 2: Requests ───────────────────────────────────────────────────────
    auto* reqTab = new QWidget;
    auto* reqLayout = new QVBoxLayout(reqTab);
    m_reqList = new QListWidget(reqTab);
    m_reqList->setSpacing(2);
    m_reqList->setSelectionMode(QAbstractItemView::NoSelection);
    reqLayout->addWidget(m_reqList);
    m_tabs->addTab(reqTab, "Requests");

    // ── Tab 3: Find People ────────────────────────────────────────────────────
    auto* findTab = new QWidget;
    auto* findLayout = new QVBoxLayout(findTab);
    auto* searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit(findTab);
    m_searchEdit->setPlaceholderText("Enter username…");
    m_searchBtn  = new QPushButton("Search", findTab);
    searchRow->addWidget(m_searchEdit);
    searchRow->addWidget(m_searchBtn);
    m_resultList = new QListWidget(findTab);
    m_resultList->setSpacing(2);
    m_resultList->setSelectionMode(QAbstractItemView::NoSelection);
    findLayout->addLayout(searchRow);
    findLayout->addWidget(m_resultList);
    m_tabs->addTab(findTab, "Find People");

    connect(m_searchBtn, &QPushButton::clicked, this, &FriendsDialog::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &FriendsDialog::onSearchClicked);

    // ── Tab-change refresh ────────────────────────────────────────────────────
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 0) refreshFriends();
        else if (idx == 1) refreshRequests();
    });

    refreshFriends();
}

// ── Row builders ──────────────────────────────────────────────────────────────

void FriendsDialog::buildFriendRow(QListWidgetItem* item, const FriendInfo& f)
{
    auto* w     = new QWidget;
    auto* hbox  = new QHBoxLayout(w);
    hbox->setContentsMargins(4, 2, 4, 2);
    hbox->setSpacing(8);

    hbox->addWidget(makeStatusDot(f.status));

    auto* nameLabel = new QLabel(f.displayName.isEmpty() ? f.username
                                                          : QStringLiteral("%1 (%2)").arg(f.displayName, f.username));
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hbox->addWidget(nameLabel);

    auto* msgBtn = new QPushButton("Message");
    msgBtn->setFixedHeight(24);
    connect(msgBtn, &QPushButton::clicked, this, [this, f] {
        emit openDmRequested(f.userId, f.username);
    });
    hbox->addWidget(msgBtn);

    // Invite ▾ button with guild-picker menu
    auto* invBtn = new QToolButton;
    invBtn->setText("Invite ▾");
    invBtn->setFixedHeight(24);
    invBtn->setPopupMode(QToolButton::InstantPopup);
    int capturedUserId   = f.userId;
    QString capturedName = f.username;
    connect(invBtn, &QToolButton::clicked, this, [this, capturedUserId, capturedName] {
        emit inviteToGuildRequested(capturedUserId, capturedName);
    });
    hbox->addWidget(invBtn);

    auto* removeBtn = new QPushButton("✕");
    removeBtn->setFixedSize(24, 24);
    removeBtn->setToolTip("Remove Friend");
    connect(removeBtn, &QPushButton::clicked, this, [this, f, item] {
        m_api->removeFriend(f.userId, [this](bool ok, const QString& err) {
            if (!ok) {
                ForjPrompt::warning(this, "Error", err);
                return;
            }
            refreshFriends();
        });
    });
    hbox->addWidget(removeBtn);

    item->setSizeHint(w->sizeHint());
    m_friendsList->setItemWidget(item, w);
}

void FriendsDialog::buildRequestRow(QListWidgetItem* item, const FriendRequestInfo& r)
{
    auto* w    = new QWidget;
    auto* hbox = new QHBoxLayout(w);
    hbox->setContentsMargins(4, 2, 4, 2);
    hbox->setSpacing(8);

    auto* nameLabel = new QLabel(r.fromDisplayName.isEmpty()
                                 ? r.fromUsername
                                 : QStringLiteral("%1 (%2)").arg(r.fromDisplayName, r.fromUsername));
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hbox->addWidget(nameLabel);

    auto* acceptBtn = new QPushButton("✔ Accept");
    acceptBtn->setFixedHeight(24);
    connect(acceptBtn, &QPushButton::clicked, this, [this, r] {
        m_api->acceptFriendRequest(r.fromUserId, [this](bool ok, const QString& err) {
            if (!ok) { ForjPrompt::warning(this, "Error", err); return; }
            refreshRequests();
            refreshFriends();
        });
    });
    hbox->addWidget(acceptBtn);

    auto* declineBtn = new QPushButton("✖ Decline");
    declineBtn->setFixedHeight(24);
    connect(declineBtn, &QPushButton::clicked, this, [this, r] {
        m_api->declineFriendRequest(r.fromUserId, [this](bool ok, const QString& err) {
            if (!ok) { ForjPrompt::warning(this, "Error", err); return; }
            refreshRequests();
        });
    });
    hbox->addWidget(declineBtn);

    item->setSizeHint(w->sizeHint());
    m_reqList->setItemWidget(item, w);
}

void FriendsDialog::buildResultRow(QListWidgetItem* item, int userId,
                                   const QString& username, const QString& displayName)
{
    auto* w    = new QWidget;
    auto* hbox = new QHBoxLayout(w);
    hbox->setContentsMargins(4, 2, 4, 2);
    hbox->setSpacing(8);

    auto* nameLabel = new QLabel(displayName.isEmpty()
                                 ? username
                                 : QStringLiteral("%1 (%2)").arg(displayName, username));
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hbox->addWidget(nameLabel);

    auto* addBtn = new QPushButton("Add Friend");
    addBtn->setFixedHeight(24);
    connect(addBtn, &QPushButton::clicked, this, [this, userId, username, addBtn] {
        addBtn->setEnabled(false);
        m_api->sendFriendRequest(userId, [this, addBtn, username](bool ok, const QString& err) {
            if (!ok) {
                ForjPrompt::warning(this, "Error", err);
                addBtn->setEnabled(true);
            } else {
                addBtn->setText("Sent!");
            }
        });
    });
    hbox->addWidget(addBtn);

    item->setSizeHint(w->sizeHint());
    m_resultList->setItemWidget(item, w);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void FriendsDialog::refreshFriends()
{
    m_friendsList->clear();
    m_api->getFriends([this](bool ok, const QString& err, const QList<FriendInfo>& friends) {
        if (!ok) {
            auto* item = new QListWidgetItem(QStringLiteral("Error: %1").arg(err));
            m_friendsList->addItem(item);
            return;
        }
        if (friends.isEmpty()) {
            m_friendsList->addItem("No friends yet. Find some people!");
            return;
        }
        for (const auto& f : friends) {
            auto* item = new QListWidgetItem(m_friendsList);
            buildFriendRow(item, f);
        }
    });
}

void FriendsDialog::refreshRequests()
{
    m_reqList->clear();
    m_api->getFriendRequests([this](bool ok, const QString& err, const QList<FriendRequestInfo>& reqs) {
        if (!ok) {
            m_reqList->addItem(QStringLiteral("Error: %1").arg(err));
            return;
        }
        if (reqs.isEmpty()) {
            m_reqList->addItem("No incoming friend requests.");
            return;
        }
        for (const auto& r : reqs) {
            auto* item = new QListWidgetItem(m_reqList);
            buildRequestRow(item, r);
        }
    });
}

void FriendsDialog::onSearchClicked()
{
    QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    m_resultList->clear();
    m_api->searchUsers(query, [this](bool ok, const QString& err, const QList<UserInfo>& users) {
        if (!ok) {
            m_resultList->addItem(QStringLiteral("Error: %1").arg(err));
            return;
        }
        if (users.isEmpty()) {
            m_resultList->addItem("No users found.");
            return;
        }
        for (const auto& u : users) {
            auto* item = new QListWidgetItem(m_resultList);
            buildResultRow(item, u.id, u.username, u.displayName);
        }
    });
}

// ── Notification helpers (called from MainWindow) ─────────────────────────────

void FriendsDialog::notifyFriendRequest(int /*fromId*/, const QString& /*fromUsername*/,
                                        const QString& /*fromDisplayName*/)
{
    if (m_tabs->currentIndex() == 1)
        refreshRequests();
    // Badge on "Requests" tab is handled at MainWindow level via notification bell.
}

void FriendsDialog::notifyFriendAccepted(int /*byId*/, const QString& /*byUsername*/)
{
    if (m_tabs->currentIndex() == 0)
        refreshFriends();
}
