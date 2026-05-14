#pragma once

#include <QTabWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QPixmap>
#include <QComboBox>
#include <QSlider>
#include <QProgressBar>
#include <QTimer>
#include "ApiClient.h"
#include "ForjDialog.h"
#include "VoiceEngine.h"


class SettingsDialog : public ForjDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(ApiClient* api, const UserInfo& me, QWidget* parent = nullptr);

signals:
    void profileUpdated(const UserInfo& user);

private slots:
    void onSaveProfile();
    void onChangePassword();
    void onPickAvatar();
    void onCreateBot();
    void onDeleteBot();
    void onCopyToken();
    void loadBots();
    void onSaveVoiceSettings();
    void onTestMicToggled();
    void onTestSpeakerClicked();

private:
    void buildProfileTab();
    void buildAccountTab();
    void buildBotsTab();
    void buildVoiceVideoTab();
    void setStatus(QLabel* lbl, const QString& msg, bool isError = true);

    ApiClient*  m_api{nullptr};
    UserInfo    m_me;

    QTabWidget* m_tabs{nullptr};

    // Profile tab
    QLabel*      m_avatarPreview{nullptr};
    QLineEdit*   m_displayName{nullptr};
    QTextEdit*   m_bio{nullptr};
    QPushButton* m_saveProfileBtn{nullptr};
    QLabel*      m_profileStatus{nullptr};
    QByteArray   m_pendingAvatarData;
    QString      m_pendingAvatarMime;

    // Account tab
    QLineEdit*   m_currentPw{nullptr};
    QLineEdit*   m_newPw{nullptr};
    QLineEdit*   m_confirmPw{nullptr};
    QPushButton* m_changePwBtn{nullptr};
    QLabel*      m_pwStatus{nullptr};

    // Bots tab
    QListWidget* m_botList{nullptr};
    QPushButton* m_createBotBtn{nullptr};
    QPushButton* m_deleteBotBtn{nullptr};
    QPushButton* m_copyTokenBtn{nullptr};
    QLabel*      m_botStatus{nullptr};

    struct BotEntry { int id; int userId; QString username; QString name; QString token; };
    QList<BotEntry> m_bots;

    // Voice & Video tab
    QComboBox*    m_inputDeviceCombo{nullptr};
    QComboBox*    m_outputDeviceCombo{nullptr};
    QSlider*      m_inputVolumeSlider{nullptr};
    QSlider*      m_outputVolumeSlider{nullptr};
    QLabel*       m_voiceSettingsStatus{nullptr};
    QPushButton*  m_testMicBtn{nullptr};
    QPushButton*  m_testSpeakerBtn{nullptr};
    QProgressBar* m_micLevelBar{nullptr};
    VoiceEngine*  m_testEngine{nullptr};
};
