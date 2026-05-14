#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>

// ── TitleBar ──────────────────────────────────────────────────────────────────
// Frameless window title bar: logo + title text + min/max/close buttons.
// Place via QMainWindow::setMenuWidget() or as the first item in a QVBoxLayout.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    // Keep in sync with window title (can still call setWindowTitle too)
    void setTitle(const QString& title);

protected:
    void mousePressEvent(QMouseEvent* e)       override;
    void mouseMoveEvent(QMouseEvent* e)        override;
    void mouseReleaseEvent(QMouseEvent* e)     override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool event(QEvent* e)                      override;

private:
    void updateMaxBtn();

    QLabel*      m_titleLabel{nullptr};
    QPushButton* m_minBtn{nullptr};
    QPushButton* m_maxBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};

    QPoint m_dragStart;
    bool   m_dragging{false};
};
