#include "ForjDialog.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QBitmap>
#include <QEvent>

static constexpr int kRadius = 8;

ForjDialog::ForjDialog(const QString& title, QWidget* parent, Qt::WindowFlags extraFlags)
    : QDialog(parent, extraFlags | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(title);

    m_titleBar = new TitleBar(this);
    m_titleBar->setTitle(title);

    m_body = new QWidget(this);
    m_body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_titleBar);
    root->addWidget(m_body, 1);
}

void ForjDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x36, 0x39, 0x3F));
    p.drawRoundedRect(rect(), kRadius, kRadius);
}

void ForjDialog::resizeEvent(QResizeEvent* e)
{
    QBitmap bm(size());
    bm.fill(Qt::color0);
    QPainter p(&bm);
    p.setBrush(Qt::color1);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), kRadius, kRadius);
    setMask(bm);
    QDialog::resizeEvent(e);
}

void ForjDialog::changeEvent(QEvent* e)
{
    QDialog::changeEvent(e);
    if (e->type() == QEvent::WindowTitleChange && m_titleBar)
        m_titleBar->setTitle(windowTitle());
}
