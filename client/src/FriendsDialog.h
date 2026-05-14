#pragma once
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include "ApiClient.h"
#include "ForjDialog.h"

class FriendsDialog : public ForjDialog
{
    Q_OBJECT
public:
    explicit FriendsDialog(ApiClient* api, const UserInfo& me, QWidget* parent = nullptr);

    // Called from MainWindow when a WS event arrives so the dialog stays fresh.
    void notifyFriendRequest(int fromId, const QString& fromUsername, const QString& fromDisplayName);
    void notifyFriendAccepted(int byId, const QString& byUsername);

signals:
    void openDmRequested(int userId, const QString& username);
    void inviteToGuildRequested(int userId, const QString& username);

private slots:
    void refreshFriends();
    void refreshRequests();
    void onSearchClicked();

private:
    // helpers
    void buildFriendRow   (QListWidgetItem* item, const FriendInfo& f);
    void buildRequestRow  (QListWidgetItem* item, const FriendRequestInfo& r);
    void buildResultRow   (QListWidgetItem* item, int userId, const QString& username,
                           const QString& displayName);

    static QLabel* makeStatusDot(const QString& status);

    ApiClient*   m_api;
    UserInfo     m_me;

    QTabWidget*  m_tabs{nullptr};

    // Friends tab
    QListWidget* m_friendsList{nullptr};

    // Requests tab
    QListWidget* m_reqList{nullptr};

    // Find People tab
    QLineEdit*   m_searchEdit{nullptr};
    QPushButton* m_searchBtn{nullptr};
    QListWidget* m_resultList{nullptr};
};
