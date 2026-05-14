#include <QApplication>
#include <QPalette>
#include <QColor>

#include "ApiClient.h"
#include "WebSocketClient.h"
#include "LoginWindow.h"
#include "MainWindow.h"

static void applyDarkPalette(QApplication& app)
{
    app.setStyle("Fusion");

    QPalette p;
    // Main surfaces
    p.setColor(QPalette::Window,          QColor(0x36, 0x39, 0x3F));
    p.setColor(QPalette::WindowText,      QColor(0xDC, 0xDD, 0xDE));
    p.setColor(QPalette::Base,            QColor(0x2F, 0x31, 0x36));
    p.setColor(QPalette::AlternateBase,   QColor(0x40, 0x44, 0x4B));
    // Text
    p.setColor(QPalette::Text,            QColor(0xDC, 0xDD, 0xDE));
    p.setColor(QPalette::PlaceholderText, QColor(0x72, 0x76, 0x7D));
    // Buttons
    p.setColor(QPalette::Button,          QColor(0x4F, 0x54, 0x5C));
    p.setColor(QPalette::ButtonText,      QColor(0xDC, 0xDD, 0xDE));
    // Selection
    p.setColor(QPalette::Highlight,       QColor(0x5B, 0x6E, 0xAF));
    p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    // Links
    p.setColor(QPalette::Link,            QColor(0x00, 0xAF, 0xFF));
    p.setColor(QPalette::LinkVisited,     QColor(0x80, 0x6F, 0xCF));
    // Disabled
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x72, 0x76, 0x7D));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x72, 0x76, 0x7D));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x72, 0x76, 0x7D));
    p.setColor(QPalette::Disabled, QPalette::Highlight,  QColor(0x40, 0x44, 0x4B));

    app.setPalette(p);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Forj");
    app.setOrganizationName("Forj");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/Icon/Forj.png"));
    applyDarkPalette(app);

    // Shared services (owned by QApplication, live for the whole session)
    auto* api = new ApiClient(&app);
    api->setBaseUrl(QStringLiteral("http://209.126.5.36:8000"));

    auto* ws = new WebSocketClient(&app);

    // Login — show dialog; on success open the main window
    auto* loginWin = new LoginWindow(api);
    QObject::connect(
        loginWin, &LoginWindow::loginSuccess,
        [api, ws, loginWin](const QString& token, const UserInfo& user) {
            loginWin->deleteLater();

            auto* mw = new MainWindow(api, ws, user);
            mw->setAttribute(Qt::WA_DeleteOnClose);
            mw->show();

            ws->connectToServer(QStringLiteral("ws://209.126.5.36:8000/ws"), token);
        });
    loginWin->show();

    return app.exec();
}
