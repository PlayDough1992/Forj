#include "TitleBar.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWindow>
#include <QScreen>
#include <QApplication>

static constexpr int kBarHeight = 30;

// Consistent button factory
static QPushButton* makeWinButton(const QString& text,
                                  const QString& hoverBg,
                                  const QString& hoverFg = "#ffffff")
{
    auto* btn = new QPushButton(text);
    btn->setFixedSize(46, kBarHeight);
    btn->setFlat(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::ArrowCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { color:#8e9297; background:transparent; border:none; font-size:13px; }"
        "QPushButton:hover { background:%1; color:%2; }")
        .arg(hoverBg, hoverFg));
    return btn;
}

TitleBar::TitleBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Slightly darker than main window (#36393F), matching Discord's titlebar
    setStyleSheet("background:#1e1f22;");
    setCursor(Qt::ArrowCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(0);

    // App brand: icon + name
    auto* logoLabel = new QLabel;
    QPixmap iconPix(":/Icon/Forj.png");
    if (!iconPix.isNull())
        logoLabel->setPixmap(iconPix.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        logoLabel->setText("\u2692");  // fallback hammer
    logoLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    logoLabel->setFixedWidth(24);
    logoLabel->setStyleSheet("background:transparent;");
    layout->addWidget(logoLabel);

    layout->addSpacing(8);

    m_titleLabel = new QLabel;
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_titleLabel->setStyleSheet(
        "color:#8e9297; font-size:11px; background:transparent;");
    layout->addWidget(m_titleLabel);

    layout->addStretch();

    // Window control buttons — standard Windows ordering
    m_minBtn   = makeWinButton(QStringLiteral("\u2013"), "#4f545c");          // en-dash (─)
    m_maxBtn   = makeWinButton(QStringLiteral("\u25a1"), "#4f545c");          // □
    m_closeBtn = makeWinButton(QStringLiteral("\u00d7"), "#ed4337", "#ffffff"); // ×

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    connect(m_minBtn, &QPushButton::clicked, this, [this] {
        window()->showMinimized();
    });
    connect(m_maxBtn, &QPushButton::clicked, this, [this] {
        if (window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
        updateMaxBtn();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        window()->close();
    });
}

void TitleBar::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void TitleBar::updateMaxBtn()
{
    // □ when normal, ⧉ (overlapping squares) when maximized
    m_maxBtn->setText(window()->isMaximized()
                      ? QStringLiteral("\u29c9")   // ⧉
                      : QStringLiteral("\u25a1")); // □
}

// ── Mouse drag (move window) ──────────────────────────────────────────────────

void TitleBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        // Check if click is on a button; if so, let the button handle it
        QWidget* childAtPos = childAt(e->pos());
        if (childAtPos != m_minBtn && childAtPos != m_maxBtn && childAtPos != m_closeBtn) {
            // Native move is required on many Linux/Wayland setups.
            if (window() && window()->windowHandle() && window()->windowHandle()->startSystemMove()) {
                e->accept();
                return;
            }
            m_dragging = true;
            m_dragStart = e->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            e->accept();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        if (window()->isMaximized()) {
            // Restore first, then start dragging from a sensible offset
            window()->showNormal();
            updateMaxBtn();
            m_dragStart = QPoint(qMin(m_dragStart.x(), window()->width() - 50),
                                 kBarHeight / 2);
        }
        window()->move(e->globalPosition().toPoint() - m_dragStart);
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_dragging) {
        m_dragging = false;
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
        updateMaxBtn();
    }
    QWidget::mouseDoubleClickEvent(e);
}

bool TitleBar::event(QEvent* e)
{
    // Keep max button icon in sync when window state changes externally
    if (e->type() == QEvent::WindowStateChange)
        updateMaxBtn();
    return QWidget::event(e);
}
