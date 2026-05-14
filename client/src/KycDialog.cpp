#include "KycDialog.h"
#include "ApiClient.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Page indices
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int PAGE_FETCH  = 0;   // Fetching session URL from server
static constexpr int PAGE_VERIFY = 1;   // Open browser + poll for approval
static constexpr int PAGE_DONE   = 2;   // Success

// ─────────────────────────────────────────────────────────────────────────────
// Style constants
// ─────────────────────────────────────────────────────────────────────────────
static const char* kPrimaryBtn =
    "QPushButton {"
    "  background-color: #5865F2;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-size: 13px;"
    "}"
    "QPushButton:hover   { background-color: #4752C4; }"
    "QPushButton:pressed { background-color: #3C45A5; }"
    "QPushButton:disabled{ background-color: #3a3d5c; color: #72767d; }";

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
KycDialog::KycDialog(ApiClient* api, const QString& verificationUrl, QWidget* parent)
    : ForjDialog("Identity Verification — Forj", parent)
    , m_api(api)
    , m_verifyUrl(verificationUrl)
{
    setMinimumWidth(480);
    setMinimumHeight(300);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(3000);
    connect(m_pollTimer, &QTimer::timeout, this, &KycDialog::checkStatus);

    m_stack = new QStackedWidget;
    buildPages();

    auto* lay = new QVBoxLayout(body());
    lay->setContentsMargins(24, 24, 24, 24);
    lay->addWidget(m_stack, 1);

    if (m_verifyUrl.isEmpty()) {
        // Need to fetch a session URL first
        m_stack->setCurrentIndex(PAGE_FETCH);
        m_api->initiateKyc([this](bool ok, const QString& url, const QString& err) {
            if (ok && !url.isEmpty()) {
                m_verifyUrl = url;
                showVerifyPage(url);
            } else {
                showError(err.isEmpty() ? "Could not start verification. Please try again later." : err);
            }
        });
    } else {
        showVerifyPage(m_verifyUrl);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build pages
// ─────────────────────────────────────────────────────────────────────────────
void KycDialog::buildPages()
{
    // ── PAGE_FETCH: loading ───────────────────────────────────────────────────
    {
        auto* pg  = new QWidget;
        auto* lay = new QVBoxLayout(pg);
        lay->setAlignment(Qt::AlignCenter);

        m_fetchStatus = new QLabel("Getting your verification link…", pg);
        m_fetchStatus->setStyleSheet("color: #b9bbbe; font-size: 13px;");
        m_fetchStatus->setAlignment(Qt::AlignCenter);
        lay->addWidget(m_fetchStatus);

        m_stack->addWidget(pg);   // index 0
    }

    // ── PAGE_VERIFY: open browser + waiting ──────────────────────────────────
    {
        auto* pg  = new QWidget;
        auto* lay = new QVBoxLayout(pg);
        lay->setSpacing(14);

        auto* title = new QLabel("Verify your identity", pg);
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
        lay->addWidget(title);

        auto* instr = new QLabel(
            "Click the button below to open the Didit verification page in your browser. "
            "Complete the steps there, then return here — this dialog will update automatically.",
            pg);
        instr->setStyleSheet("color: #b9bbbe; font-size: 13px;");
        instr->setWordWrap(true);
        lay->addWidget(instr);

        auto* hint = new QLabel(
            "You will need:\u2003\u2022 A government-issued photo ID"
            "\u2003\u2022 A selfie or camera access",
            pg);
        hint->setStyleSheet("color: #72767d; font-size: 11px;");
        hint->setWordWrap(true);
        lay->addWidget(hint);

        m_openBtn = new QPushButton("Open Verification Page", pg);
        m_openBtn->setStyleSheet(kPrimaryBtn);
        m_openBtn->setFixedHeight(38);
        connect(m_openBtn, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(QUrl(m_verifyUrl));
        });
        lay->addWidget(m_openBtn);

        m_pollStatus = new QLabel("Waiting for verification to complete…", pg);
        m_pollStatus->setStyleSheet("color: #72767d; font-size: 12px; font-style: italic;");
        m_pollStatus->setAlignment(Qt::AlignCenter);
        m_pollStatus->setVisible(false);
        lay->addWidget(m_pollStatus);

        lay->addStretch();
        m_stack->addWidget(pg);   // index 1
    }

    // ── PAGE_DONE: success ────────────────────────────────────────────────────
    {
        auto* pg  = new QWidget;
        auto* lay = new QVBoxLayout(pg);
        lay->setAlignment(Qt::AlignCenter);
        lay->setSpacing(16);

        m_doneLabel = new QLabel({}, pg);
        m_doneLabel->setStyleSheet(
            "font-size: 15px; font-weight: bold; color: #43b581;");
        m_doneLabel->setAlignment(Qt::AlignCenter);
        m_doneLabel->setWordWrap(true);
        lay->addWidget(m_doneLabel);

        m_closeBtn = new QPushButton("Continue to Forj", pg);
        m_closeBtn->setStyleSheet(kPrimaryBtn);
        m_closeBtn->setFixedHeight(38);
        connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        lay->addWidget(m_closeBtn, 0, Qt::AlignCenter);

        m_stack->addWidget(pg);   // index 2
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation helpers
// ─────────────────────────────────────────────────────────────────────────────
void KycDialog::showVerifyPage(const QString& url)
{
    m_verifyUrl = url;
    m_stack->setCurrentIndex(PAGE_VERIFY);
    startPolling();
}

void KycDialog::showError(const QString& msg)
{
    stopPolling();
    m_fetchStatus->setStyleSheet("color: #ed4245; font-size: 13px;");
    m_fetchStatus->setText(msg);
    m_stack->setCurrentIndex(PAGE_FETCH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Polling
// ─────────────────────────────────────────────────────────────────────────────
void KycDialog::startPolling()
{
    m_pollStatus->setVisible(true);
    m_pollTimer->start();
}

void KycDialog::stopPolling()
{
    m_pollTimer->stop();
}

void KycDialog::checkStatus()
{
    m_api->getKycStatus([this](bool ok, const QString& kycStatus) {
        if (!ok) return;   // network hiccup — try again next tick
        if (kycStatus == "approved") {
            stopPolling();
            m_doneLabel->setText(
                "\u2714\u2006 Identity verified!\n\n"
                "Welcome to Forj.");
            m_stack->setCurrentIndex(PAGE_DONE);
        } else if (kycStatus == "declined") {
            stopPolling();
            showError(
                "Your identity verification was declined.\n"
                "Please contact support or try again with a clearer document.");
        }
        // "unverified" / "pending" \u2014 keep polling
    });
}
