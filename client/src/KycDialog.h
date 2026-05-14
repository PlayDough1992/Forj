#pragma once

#include "ForjDialog.h"

#include <QByteArray>
#include <QString>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class ApiClient;

// ─────────────────────────────────────────────────────────────────────────────
// KycDialog — identity-verification wizard using Didit session flow
//
// Flow:
//   If a verificationUrl is supplied: jump straight to PAGE_VERIFY.
//   Otherwise show PAGE_FETCH while requesting a new session URL.
//   PAGE_VERIFY: user clicks “Open Verification Page” → browser opens.
//               Dialog polls /auth/kyc/status every 3 s.
//   PAGE_DONE:  auto-shown when server reports kyc_status == "approved".
// ─────────────────────────────────────────────────────────────────────────────
class KycDialog : public ForjDialog
{
    Q_OBJECT

public:
    explicit KycDialog(ApiClient* api,
                       const QString& verificationUrl = {},
                       QWidget* parent = nullptr);

private:
    void buildPages();
    void showVerifyPage(const QString& url);
    void showError(const QString& msg);
    void startPolling();
    void stopPolling();
    void checkStatus();

    // ── Widgets ─────────────────────────────────────────────────────────────
    ApiClient*      m_api{nullptr};
    QStackedWidget* m_stack{nullptr};
    QLabel*         m_fetchStatus{nullptr};  // PAGE_FETCH spinner text
    QLabel*         m_pollStatus{nullptr};   // PAGE_VERIFY "Waiting…"
    QPushButton*    m_openBtn{nullptr};      // "Open Verification Page"
    QLabel*         m_doneLabel{nullptr};    // PAGE_DONE message
    QPushButton*    m_closeBtn{nullptr};
    QTimer*         m_pollTimer{nullptr};
    QString         m_verifyUrl;
};

