#include "SettingsDialog.h"
#include "AvatarCropDialog.h"
#include "ForjPrompt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QGroupBox>
#include <QScrollArea>
#include <QSettings>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmsystem.h>
#endif

SettingsDialog::SettingsDialog(ApiClient* api, const UserInfo& me, QWidget* parent)
    : ForjDialog("Forj \u2014 User Settings", parent)
    , m_api(api)
    , m_me(me)
{
    resize(520, 580);

    m_tabs = new QTabWidget(body());
    buildProfileTab();
    buildAccountTab();
    buildBotsTab();
    buildVoiceVideoTab();

    auto* root = new QVBoxLayout(body());
    root->addWidget(m_tabs);

    // Pre-populate
    m_displayName->setText(me.displayName.isEmpty() ? me.username : me.displayName);
    m_bio->setPlainText(me.bio);

    // Load existing avatar if the user has one
    if (me.hasAvatar) {
        m_api->getAvatar(me.id, [this](bool ok, const QByteArray& data) {
            if (ok && !data.isEmpty()) {
                QPixmap pix;
                pix.loadFromData(data);
                m_avatarPreview->setPixmap(avatarToCircle(pix, 80));
                m_avatarPreview->setText({});
            }
        });
    }
}

// ── Profile Tab ───────────────────────────────────────────────────────────────

void SettingsDialog::buildProfileTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // Avatar row
    auto* avatarRow = new QHBoxLayout;
    m_avatarPreview = new QLabel;
    m_avatarPreview->setFixedSize(80, 80);
    m_avatarPreview->setStyleSheet("background: transparent;");
    m_avatarPreview->setAlignment(Qt::AlignCenter);
    m_avatarPreview->setText(m_me.username.left(1).toUpper());

    auto* pickAvatarBtn = new QPushButton("Upload Avatar");
    pickAvatarBtn->setFixedWidth(120);
    auto* avatarNote = new QLabel("<small>PNG, JPG, GIF or WEBP · max 512 KB</small>");
    avatarNote->setStyleSheet("color: #72767D;");

    auto* avatarCol = new QVBoxLayout;
    avatarCol->addWidget(pickAvatarBtn);
    avatarCol->addWidget(avatarNote);
    avatarCol->addStretch();

    avatarRow->addWidget(m_avatarPreview);
    avatarRow->addSpacing(12);
    avatarRow->addLayout(avatarCol);
    avatarRow->addStretch();
    layout->addLayout(avatarRow);

    // Form
    auto* form = new QFormLayout;
    m_displayName = new QLineEdit;
    m_displayName->setMaxLength(32);
    m_displayName->setPlaceholderText("How you appear to others");
    m_bio = new QTextEdit;
    m_bio->setPlaceholderText("Tell people a bit about yourself…");
    m_bio->setMaximumHeight(90);

    form->addRow("Display name", m_displayName);
    form->addRow("Bio",          m_bio);
    layout->addLayout(form);

    m_profileStatus = new QLabel;
    m_profileStatus->setWordWrap(true);
    layout->addWidget(m_profileStatus);

    m_saveProfileBtn = new QPushButton("Save Changes");
    layout->addWidget(m_saveProfileBtn);
    layout->addStretch();

    m_tabs->addTab(page, "Profile");

    connect(pickAvatarBtn,   &QPushButton::clicked, this, &SettingsDialog::onPickAvatar);
    connect(m_saveProfileBtn,&QPushButton::clicked, this, &SettingsDialog::onSaveProfile);
}

// ── Account Tab ───────────────────────────────────────────────────────────────

void SettingsDialog::buildAccountTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* box = new QGroupBox("Change Password");
    auto* form = new QFormLayout(box);
    m_currentPw = new QLineEdit; m_currentPw->setEchoMode(QLineEdit::Password);
    m_newPw     = new QLineEdit; m_newPw->setEchoMode(QLineEdit::Password);
    m_confirmPw = new QLineEdit; m_confirmPw->setEchoMode(QLineEdit::Password);
    form->addRow("Current",  m_currentPw);
    form->addRow("New",      m_newPw);
    form->addRow("Confirm",  m_confirmPw);
    layout->addWidget(box);

    m_pwStatus = new QLabel;
    m_pwStatus->setWordWrap(true);
    layout->addWidget(m_pwStatus);

    m_changePwBtn = new QPushButton("Update Password");
    layout->addWidget(m_changePwBtn);
    layout->addStretch();

    m_tabs->addTab(page, "Account");

    connect(m_changePwBtn, &QPushButton::clicked, this, &SettingsDialog::onChangePassword);
}

// ── Bots Tab ──────────────────────────────────────────────────────────────────

void SettingsDialog::buildBotsTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(
        "<b>Your Bots</b><br>"
        "<small style='color:#72767D;'>Bots can join servers, post messages, and register slash commands via the API.</small>"
    ));

    m_botList = new QListWidget;
    layout->addWidget(m_botList, 1);

    auto* btnRow = new QHBoxLayout;
    m_createBotBtn = new QPushButton("+ New Bot");
    m_deleteBotBtn = new QPushButton("Delete");
    m_copyTokenBtn = new QPushButton("Copy Token");
    m_deleteBotBtn->setEnabled(false);
    m_copyTokenBtn->setEnabled(false);
    btnRow->addWidget(m_createBotBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_copyTokenBtn);
    btnRow->addWidget(m_deleteBotBtn);
    layout->addLayout(btnRow);

    m_botStatus = new QLabel;
    m_botStatus->setWordWrap(true);
    layout->addWidget(m_botStatus);

    m_tabs->addTab(page, "Bots");

    connect(m_botList,      &QListWidget::currentRowChanged, this, [this](int row) {
        bool ok = row >= 0 && row < m_bots.size();
        m_deleteBotBtn->setEnabled(ok);
        m_copyTokenBtn->setEnabled(ok && !m_bots[row].token.isEmpty());
    });
    connect(m_createBotBtn, &QPushButton::clicked, this, &SettingsDialog::onCreateBot);
    connect(m_deleteBotBtn, &QPushButton::clicked, this, &SettingsDialog::onDeleteBot);
    connect(m_copyTokenBtn, &QPushButton::clicked, this, &SettingsDialog::onCopyToken);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void SettingsDialog::setStatus(QLabel* lbl, const QString& msg, bool isError)
{
    lbl->setStyleSheet(isError ? "color: #ED4245;" : "color: #57F287;");
    lbl->setText(msg);
}

// ── Voice & Video Tab ─────────────────────────────────────────────────────────

void SettingsDialog::buildVoiceVideoTab()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    layout->addWidget(new QLabel(
        "<b>Voice &amp; Video</b><br>"
        "<small style='color:#72767D;'>Configure your input and output audio devices.</small>"
    ));

    auto* form = new QFormLayout;
    form->setSpacing(8);

    m_inputDeviceCombo  = new QComboBox;
    m_outputDeviceCombo = new QComboBox;

#ifdef Q_OS_WIN
    // Enumerate audio input devices using WinMM
    const int numIn = static_cast<int>(waveInGetNumDevs());
    m_inputDeviceCombo->addItem("Default");
    for (int i = 0; i < numIn; ++i) {
        WAVEINCAPS caps{};
        if (waveInGetDevCaps(static_cast<UINT>(i), &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            m_inputDeviceCombo->addItem(QString::fromWCharArray(caps.szPname));
    }
    // Enumerate audio output devices
    const int numOut = static_cast<int>(waveOutGetNumDevs());
    m_outputDeviceCombo->addItem("Default");
    for (int i = 0; i < numOut; ++i) {
        WAVEOUTCAPS caps{};
        if (waveOutGetDevCaps(static_cast<UINT>(i), &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            m_outputDeviceCombo->addItem(QString::fromWCharArray(caps.szPname));
    }
#else
    m_inputDeviceCombo->addItem("Default (not configurable on this platform)");
    m_outputDeviceCombo->addItem("Default (not configurable on this platform)");
#endif

    form->addRow("Input Device:", m_inputDeviceCombo);

    m_inputVolumeSlider = new QSlider(Qt::Horizontal);
    m_inputVolumeSlider->setRange(0, 200);
    m_inputVolumeSlider->setValue(100);
    m_inputVolumeSlider->setToolTip("Input volume (100 = unity gain)");
    form->addRow("Input Volume:", m_inputVolumeSlider);

    form->addRow("Output Device:", m_outputDeviceCombo);

    m_outputVolumeSlider = new QSlider(Qt::Horizontal);
    m_outputVolumeSlider->setRange(0, 200);
    m_outputVolumeSlider->setValue(100);
    m_outputVolumeSlider->setToolTip("Output volume (100 = unity gain)");
    form->addRow("Output Volume:", m_outputVolumeSlider);

    layout->addLayout(form);

    // Load saved settings
    QSettings s;
    m_inputDeviceCombo->setCurrentText(s.value("voice/inputDevice", "Default").toString());
    m_outputDeviceCombo->setCurrentText(s.value("voice/outputDevice", "Default").toString());
    m_inputVolumeSlider->setValue(s.value("voice/inputVolume", 100).toInt());
    m_outputVolumeSlider->setValue(s.value("voice/outputVolume", 100).toInt());

    // Test Mic section
    auto* micTestBox = new QGroupBox("Microphone Test");
    auto* micTestLayout = new QVBoxLayout(micTestBox);

    auto* micTestRow = new QHBoxLayout;
    m_testMicBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x8E\x99") + "  Test Mic");
    m_testMicBtn->setCheckable(true);
    m_testMicBtn->setFixedWidth(120);
    micTestRow->addWidget(m_testMicBtn);
    micTestRow->addSpacing(8);
    m_micLevelBar = new QProgressBar;
    m_micLevelBar->setRange(0, 100);
    m_micLevelBar->setValue(0);
    m_micLevelBar->setTextVisible(false);
    m_micLevelBar->setFixedHeight(18);
    m_micLevelBar->setStyleSheet(
        "QProgressBar{border:1px solid #72767D;border-radius:4px;background:#2f3136;}"
        "QProgressBar::chunk{background:#57F287;border-radius:3px;}"
    );
    micTestRow->addWidget(m_micLevelBar, 1);
    micTestLayout->addLayout(micTestRow);
    auto* micNote = new QLabel("<small style='color:#72767D;'>Input level — speaks when you see green bars</small>");
    micTestLayout->addWidget(micNote);
    layout->addWidget(micTestBox);

    // Test Speaker section
    auto* spkTestBox = new QGroupBox("Speaker Test");
    auto* spkTestLayout = new QHBoxLayout(spkTestBox);
    m_testSpeakerBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x8A") + "  Play Test Sound");
    m_testSpeakerBtn->setFixedWidth(160);
    auto* spkNote = new QLabel("<small style='color:#72767D;'>Plays a 440 Hz tone through the selected output device</small>");
    spkNote->setWordWrap(true);
    spkTestLayout->addWidget(m_testSpeakerBtn);
    spkTestLayout->addWidget(spkNote, 1);
    layout->addWidget(spkTestBox);
    layout->addStretch();

    auto* saveBtn = new QPushButton("Save");
    saveBtn->setFixedWidth(80);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSaveVoiceSettings);
    connect(m_testMicBtn,     &QPushButton::clicked, this, &SettingsDialog::onTestMicToggled);
    connect(m_testSpeakerBtn, &QPushButton::clicked, this, &SettingsDialog::onTestSpeakerClicked);

    m_voiceSettingsStatus = new QLabel;
    m_voiceSettingsStatus->setWordWrap(true);

    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(saveBtn);
    layout->addLayout(row);
    layout->addWidget(m_voiceSettingsStatus);

    m_tabs->addTab(page, "Voice & Video");
}

void SettingsDialog::onSaveVoiceSettings()
{
    QSettings s;
    s.setValue("voice/inputDevice",  m_inputDeviceCombo->currentText());
    s.setValue("voice/outputDevice", m_outputDeviceCombo->currentText());
    s.setValue("voice/inputVolume",  m_inputVolumeSlider->value());
    s.setValue("voice/outputVolume", m_outputVolumeSlider->value());
    setStatus(m_voiceSettingsStatus, "Settings saved.", false);
}

void SettingsDialog::onTestMicToggled()
{
    if (m_testEngine) {
        // Stop mic test
        m_testEngine->stop();
        delete m_testEngine;
        m_testEngine = nullptr;
        m_testMicBtn->setText(QString::fromUtf8("\xF0\x9F\x8E\x99") + "  Test Mic");
        m_testMicBtn->setChecked(false);
        m_micLevelBar->setValue(0);
        return;
    }

    // Start mic test using selected input device
    int inIdx = m_inputDeviceCombo->currentIndex();  // 0 = Default
    m_testEngine = new VoiceEngine(this);
    connect(m_testEngine, &VoiceEngine::inputLevelChanged, this, [this](float v) {
        m_micLevelBar->setValue(static_cast<int>(v * 100.0f));
    });
    if (!m_testEngine->start(inIdx, -1)) {
        delete m_testEngine;
        m_testEngine = nullptr;
        setStatus(m_voiceSettingsStatus, "Could not open microphone.", true);
        m_testMicBtn->setChecked(false);
        return;
    }
    m_testMicBtn->setText(QString::fromUtf8("\u23F9") + "  Stop");
}

void SettingsDialog::onTestSpeakerClicked()
{
    // Play a 440 Hz test tone through default output.
    // VoiceEngine::playTestTone() opens waveOut internally (WAVE_MAPPER)
    // when no active voice session is running.
    auto* eng = new VoiceEngine(this);
    eng->playTestTone();
    // Clean up after the tone has finished (~600 ms tone + buffer drain)
    QTimer::singleShot(700, eng, &QObject::deleteLater);
    m_testSpeakerBtn->setEnabled(false);
    QTimer::singleShot(700, this, [this] { m_testSpeakerBtn->setEnabled(true); });
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SettingsDialog::onPickAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose Avatar",
        {},
        "Images (*.png *.jpg *.jpeg *.gif *.webp)"
    );
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(m_profileStatus, "Could not open file.");
        return;
    }
    QByteArray raw = f.readAll();

    QPixmap src;
    if (!src.loadFromData(raw) || src.isNull()) {
        setStatus(m_profileStatus, "Could not read image.");
        return;
    }

    AvatarCropDialog dlg(src, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_pendingAvatarData = dlg.croppedPng();
    m_pendingAvatarMime = "image/png";

    QPixmap pix;
    pix.loadFromData(m_pendingAvatarData);
    m_avatarPreview->setPixmap(avatarToCircle(pix, 80));
    m_avatarPreview->setText({});
    setStatus(m_profileStatus, "Avatar ready — click Save Changes to upload.", false);
}

void SettingsDialog::onSaveProfile()
{
    m_saveProfileBtn->setEnabled(false);
    setStatus(m_profileStatus, "Saving…", false);

    const QString displayName = m_displayName->text().trimmed();
    const QString bio         = m_bio->toPlainText().trimmed();

    // Upload avatar first if one was selected
    auto doProfileUpdate = [this, displayName, bio]() {
        m_api->updateProfile(displayName, bio, [this](bool ok, const QString& err, const UserInfo& user) {
            m_saveProfileBtn->setEnabled(true);
            if (ok) {
                m_me = user;
                setStatus(m_profileStatus, "Saved!", false);
                emit profileUpdated(user);
            } else {
                setStatus(m_profileStatus, err);
            }
        });
    };

    if (!m_pendingAvatarData.isEmpty()) {
        m_api->uploadAvatar(m_pendingAvatarData, m_pendingAvatarMime,
                            [this, doProfileUpdate](bool ok, const QString& err) {
                                if (ok) {
                                    m_pendingAvatarData.clear();
                                    doProfileUpdate();
                                } else {
                                    m_saveProfileBtn->setEnabled(true);
                                    setStatus(m_profileStatus, "Avatar upload failed: " + err);
                                }
                            });
    } else {
        doProfileUpdate();
    }
}

void SettingsDialog::onChangePassword()
{
    const QString cur  = m_currentPw->text();
    const QString nw   = m_newPw->text();
    const QString conf = m_confirmPw->text();

    if (cur.isEmpty() || nw.isEmpty() || conf.isEmpty()) {
        setStatus(m_pwStatus, "Fill in all fields.");
        return;
    }
    if (nw != conf) {
        setStatus(m_pwStatus, "New passwords do not match.");
        return;
    }
    if (nw.length() < 8) {
        setStatus(m_pwStatus, "New password must be at least 8 characters.");
        return;
    }

    m_changePwBtn->setEnabled(false);
    setStatus(m_pwStatus, "Updating…", false);

    m_api->changePassword(cur, nw, [this](bool ok, const QString& err) {
        m_changePwBtn->setEnabled(true);
        if (ok) {
            m_currentPw->clear();
            m_newPw->clear();
            m_confirmPw->clear();
            setStatus(m_pwStatus, "Password updated!", false);
        } else {
            setStatus(m_pwStatus, err);
        }
    });
}

void SettingsDialog::loadBots()
{
    m_api->getBots([this](bool ok, const QString&, const QList<BotInfo>& bots) {
        m_bots.clear();
        m_botList->clear();
        if (!ok) return;
        for (const auto& b : bots) {
            m_bots.append({b.id, b.userId, b.username, b.name, {}});
            m_botList->addItem(QStringLiteral("🤖 %1  (@%2)").arg(b.name, b.username));
        }
    });
}

void SettingsDialog::onCreateBot()
{
    const QString name = ForjPrompt::getText(this, "New Bot", "Bot display name:");
    if (name.trimmed().isEmpty())
        return;

    m_api->createBot(name.trimmed(), [this](bool ok, const QString& err, const BotInfo& bot) {
        if (ok) {
            // token is available only at creation time - save in our in-memory list
            BotEntry entry{bot.id, bot.userId, bot.username, bot.name, bot.token};
            m_bots.append(entry);
            m_botList->addItem(QStringLiteral("🤖 %1  (@%2)").arg(bot.name, bot.username));
            m_botList->setCurrentRow(m_bots.size() - 1);
            setStatus(m_botStatus, "Bot created! Token is ready to copy.", false);
        } else {
            setStatus(m_botStatus, "Failed: " + err);
        }
    });
}

void SettingsDialog::onDeleteBot()
{
    int row = m_botList->currentRow();
    if (row < 0 || row >= m_bots.size()) return;

    if (!ForjPrompt::question(this, "Delete Bot",
            QStringLiteral("Delete bot '%1'? This cannot be undone.").arg(m_bots[row].name)))
        return;

    int botId = m_bots[row].id;
    m_api->deleteBot(botId, [this, row](bool ok, const QString& err) {
        if (ok) {
            m_bots.removeAt(row);
            delete m_botList->takeItem(row);
            setStatus(m_botStatus, "Bot deleted.", false);
        } else {
            setStatus(m_botStatus, "Failed: " + err);
        }
    });
}

void SettingsDialog::onCopyToken()
{
    int row = m_botList->currentRow();
    if (row < 0 || row >= m_bots.size()) return;

    if (m_bots[row].token.isEmpty()) {
        // Reset to get a fresh token
        m_api->resetBotToken(m_bots[row].id, [this, row](bool ok, const QString& err, const BotInfo& bot) {
            if (ok) {
                m_bots[row].token = bot.token;
                QApplication::clipboard()->setText(bot.token);
                setStatus(m_botStatus, "New token copied to clipboard!", false);
            } else {
                setStatus(m_botStatus, "Failed: " + err);
            }
        });
    } else {
        QApplication::clipboard()->setText(m_bots[row].token);
        setStatus(m_botStatus, "Token copied to clipboard!", false);
    }
}
