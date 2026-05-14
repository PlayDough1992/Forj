#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include "ApiClient.h"   // MessageInfo, UserInfo


class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketClient(QObject* parent = nullptr);
    ~WebSocketClient() override;

    void connectToServer(const QString& wsUrl, const QString& token);
    void disconnectFromServer();
    [[nodiscard]] bool isConnected() const;

    void sendAudioFrame(int channelId, const QByteArray& pcm);
    void sendVoiceSpeaking(int channelId, bool speaking);

signals:
    void connected();
    void disconnected();
    void authenticationFailed(const QString& reason);

    // Server-pushed events
    void newMessage(const MessageInfo& msg);
    void newDmMessage(const DmMessageInfo& msg);
    void userOnline(int userId, const QString& username);
    void userOffline(int userId, const QString& username);
    void userStatusChanged(int userId, const QString& username, const QString& status);
    void mentioned(int guildId, int channelId, const QString& channelName,
                   const QString& authorUsername, const QString& content);
    void friendRequest(int fromId, const QString& fromUsername, const QString& fromDisplayName);
    void friendAccepted(int byId, const QString& byUsername, const QString& byDisplayName);
    void guildInviteReceived(int guildId, const QString& guildName,
                             const QString& inviteCode, const QString& inviterUsername);
    void voiceJoin(int channelId, int userId, const QString& username);
    void voiceLeave(int channelId, int userId, const QString& username);
    void audioData(int channelId, int userId, const QByteArray& pcm);
    void voiceSpeaking(int channelId, int userId, bool speaking);
    // KYC / safety events
    void kycApproved();                                      // user's identity was verified
    void safetyFreeze(int conversationId, const QString& message); // conversation frozen

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& text);
    void onError(QAbstractSocket::SocketError error);
    void onPingTimer();

private:
    QWebSocket* m_socket{nullptr};
    QTimer*     m_pingTimer{nullptr};
    QString     m_token;
    QString     m_wsUrl;
};
