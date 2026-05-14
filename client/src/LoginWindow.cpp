#include "LoginWindow.h"
#include "TitleBar.h"
#include "KycDialog.h"

#include <QPainter>
#include <QBitmap>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>

LoginWindow::LoginWindow(ApiClient* api, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_api(api)
{
    setWindowTitle("Forj — Login");
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(380, 492);   // +32px for custom titlebar
    setModal(true);

    m_stack = new QStackedWidget(this);

    buildLoginPage();
    buildRegisterPage();

    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_registerPage);
    m_stack->setCurrentIndex(0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new TitleBar(this);
    m_titleBar->setTitle(windowTitle());
    root->addWidget(m_titleBar);
    root->addWidget(m_stack);

    // ── Restore saved credentials ─────────────────────────────────────────────
    QSettings s;
    if (s.value("login/remember", false).toBool()) {
        m_loginUsername->setText(s.value("login/username").toString());
        m_loginPassword->setText(s.value("login/password").toString());
        m_rememberMe->setChecked(true);
    }
}

// ── Page builders ─────────────────────────────────────────────────────────────

void LoginWindow::buildLoginPage()
{
    m_loginPage = new QWidget;
    auto* layout = new QVBoxLayout(m_loginPage);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    auto* title = new QLabel("<h2>Welcome back!</h2>");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel("Sign in to continue to Forj.");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);
    layout->addSpacing(10);

    auto* form = new QFormLayout;
    m_loginUsername = new QLineEdit;
    m_loginUsername->setPlaceholderText("Your username");
    m_loginPassword = new QLineEdit;
    m_loginPassword->setPlaceholderText("Your password");
    m_loginPassword->setEchoMode(QLineEdit::Password);
    form->addRow("Username", m_loginUsername);
    form->addRow("Password", m_loginPassword);
    layout->addLayout(form);

    m_rememberMe = new QCheckBox("Remember me");
    layout->addWidget(m_rememberMe);

    m_loginStatus = new QLabel;
    m_loginStatus->setWordWrap(true);
    m_loginStatus->setAlignment(Qt::AlignCenter);
    m_loginStatus->setStyleSheet("color: #ED4245;");
    layout->addWidget(m_loginStatus);

    m_loginBtn = new QPushButton("Login");
    m_loginBtn->setFixedHeight(38);
    m_loginBtn->setDefault(true);
    layout->addWidget(m_loginBtn);

    layout->addStretch();

    auto* switchRow = new QHBoxLayout;
    switchRow->addStretch();
    auto* switchLbl = new QLabel("Need an account?");
    auto* switchBtn = new QPushButton("Register");
    switchBtn->setFlat(true);
    switchRow->addWidget(switchLbl);
    switchRow->addWidget(switchBtn);
    switchRow->addStretch();
    layout->addLayout(switchRow);

    connect(m_loginBtn,     &QPushButton::clicked, this, &LoginWindow::doLogin);
    connect(m_loginPassword, &QLineEdit::returnPressed, this, &LoginWindow::doLogin);
    connect(switchBtn,      &QPushButton::clicked, this, &LoginWindow::showRegisterPage);
}

void LoginWindow::buildRegisterPage()
{
    m_registerPage = new QWidget;
    auto* layout = new QVBoxLayout(m_registerPage);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    auto* title = new QLabel("<h2>Create an account</h2>");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    layout->addSpacing(6);

    auto* form = new QFormLayout;
    m_regUsername = new QLineEdit;
    m_regUsername->setPlaceholderText("2–32 alphanumeric chars / underscore");
    m_regEmail    = new QLineEdit;
    m_regEmail->setPlaceholderText("you@example.com");
    m_regPassword = new QLineEdit;
    m_regPassword->setEchoMode(QLineEdit::Password);
    m_regConfirm  = new QLineEdit;
    m_regConfirm->setEchoMode(QLineEdit::Password);
    form->addRow("Username", m_regUsername);
    form->addRow("Email",    m_regEmail);
    form->addRow("Password", m_regPassword);
    form->addRow("Confirm",  m_regConfirm);
    layout->addLayout(form);

    m_regStatus = new QLabel;
    m_regStatus->setWordWrap(true);
    m_regStatus->setAlignment(Qt::AlignCenter);
    m_regStatus->setStyleSheet("color: #ED4245;");
    layout->addWidget(m_regStatus);

    m_registerBtn = new QPushButton("Create Account");
    m_registerBtn->setFixedHeight(38);
    m_registerBtn->setDefault(true);
    layout->addWidget(m_registerBtn);

    layout->addStretch();

    auto* switchRow = new QHBoxLayout;
    switchRow->addStretch();
    auto* switchLbl = new QLabel("Already have an account?");
    auto* switchBtn = new QPushButton("Login");
    switchBtn->setFlat(true);
    switchRow->addWidget(switchLbl);
    switchRow->addWidget(switchBtn);
    switchRow->addStretch();
    layout->addLayout(switchRow);

    connect(m_registerBtn,  &QPushButton::clicked,        this, &LoginWindow::doRegister);
    connect(m_regConfirm,   &QLineEdit::returnPressed,    this, &LoginWindow::doRegister);
    connect(switchBtn,      &QPushButton::clicked,        this, &LoginWindow::showLoginPage);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void LoginWindow::showLoginPage()
{
    m_loginStatus->clear();
    m_stack->setCurrentIndex(0);
}

void LoginWindow::showRegisterPage()
{
    m_regStatus->clear();
    m_stack->setCurrentIndex(1);
}

void LoginWindow::setStatus(const QString& msg, bool isError)
{
    QLabel* lbl = (m_stack->currentIndex() == 0) ? m_loginStatus : m_regStatus;
    lbl->setStyleSheet(isError ? "color: #ED4245;" : "color: #57F287;");
    lbl->setText(msg);
}

void LoginWindow::doLogin()
{
    const QString username = m_loginUsername->text().trimmed();
    const QString password = m_loginPassword->text();

    if (username.isEmpty() || password.isEmpty()) {
        setStatus("Please fill in all fields.");
        return;
    }

    m_loginBtn->setEnabled(false);
    setStatus("Logging in…", false);

    m_api->login(username, password,
                 [this, username, password](bool ok, const QString& error, const QString& token, const UserInfo& user) {
                     m_loginBtn->setEnabled(true);
                     if (ok) {
                         // Save or clear credentials
                         QSettings s;
                         if (m_rememberMe->isChecked()) {
                             s.setValue("login/remember",  true);
                             s.setValue("login/username",  username);
                             s.setValue("login/password",  password);
                         } else {
                             s.remove("login/remember");
                             s.remove("login/username");
                             s.remove("login/password");
                         }
                         m_api->setToken(token);
                         emit loginSuccess(token, user);
                         accept();
                     } else {
                         setStatus(error);
                     }
                 });
}

void LoginWindow::doRegister()
{
    const QString username = m_regUsername->text().trimmed();
    const QString email    = m_regEmail->text().trimmed();
    const QString password = m_regPassword->text();
    const QString confirm  = m_regConfirm->text();

    if (username.isEmpty() || email.isEmpty() || password.isEmpty() || confirm.isEmpty()) {
        setStatus("Please fill in all fields.");
        return;
    }
    if (password != confirm) {
        setStatus("Passwords do not match.");
        return;
    }
    if (password.length() < 8) {
        setStatus("Password must be at least 8 characters.");
        return;
    }

    m_registerBtn->setEnabled(false);
    setStatus("Creating account…", false);

    m_api->registerUser(username, email, password,
                        [this](bool ok, const QString& error, const QString& token, const UserInfo& user) {
                            m_registerBtn->setEnabled(true);
                            if (ok) {
                                m_api->setToken(token);

                                // If KYC is required, launch the native verification wizard.
                                if (user.kycStatus != "approved") {
                                    auto* kyc = new KycDialog(m_api, user.kycVerificationUrl, this);
                                    kyc->setAttribute(Qt::WA_DeleteOnClose);
                                    kyc->exec();
                                    // The wizard stays open until the user is approved
                                    // or closes the dialog; either way the WebSocket
                                    // kyc_update event will refresh MainWindow once live.
                                }

                                emit loginSuccess(token, user);
                                accept();
                            } else {
                                setStatus(error);
                            }
                        });
}

void LoginWindow::paintEvent(QPaintEvent*)
{
    static constexpr int kRadius = 8;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x36, 0x39, 0x3F));
    p.drawRoundedRect(rect(), kRadius, kRadius);
}

void LoginWindow::resizeEvent(QResizeEvent* e)
{
    static constexpr int kRadius = 8;
    QBitmap bm(size());
    bm.fill(Qt::color0);
    QPainter p(&bm);
    p.setBrush(Qt::color1);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), kRadius, kRadius);
    setMask(bm);
    QDialog::resizeEvent(e);
}
