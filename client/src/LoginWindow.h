#pragma once

#include <QDialog>
#include <QStackedWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include "ApiClient.h"
#include "TitleBar.h"


class LoginWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient* api, QWidget* parent = nullptr);

signals:
    void loginSuccess(const QString& token, const UserInfo& user);

private slots:
    void doLogin();
    void doRegister();
    void showLoginPage();
    void showRegisterPage();

private:
    void buildLoginPage();
    void buildRegisterPage();
    void setStatus(const QString& msg, bool isError = true);
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

    TitleBar* m_titleBar{nullptr};

    ApiClient*      m_api{nullptr};
    QStackedWidget* m_stack{nullptr};

    // Login page
    QWidget*     m_loginPage{nullptr};
    QLineEdit*   m_loginUsername{nullptr};
    QLineEdit*   m_loginPassword{nullptr};
    QCheckBox*   m_rememberMe{nullptr};
    QLabel*      m_loginStatus{nullptr};
    QPushButton* m_loginBtn{nullptr};

    // Register page
    QWidget*   m_registerPage{nullptr};
    QLineEdit* m_regUsername{nullptr};
    QLineEdit* m_regEmail{nullptr};
    QLineEdit* m_regPassword{nullptr};
    QLineEdit* m_regConfirm{nullptr};
    QLabel*    m_regStatus{nullptr};
    QPushButton* m_registerBtn{nullptr};
};
