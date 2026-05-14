#include "AvatarCropDialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QBuffer>

// ── Shared avatar helper ──────────────────────────────────────────────────────

QPixmap avatarToCircle(const QPixmap& src, int size)
{
    if (src.isNull()) return {};
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    const int ox = (scaled.width()  - size) / 2;
    const int oy = (scaled.height() - size) / 2;
    QPixmap circ(size, size);
    circ.fill(Qt::transparent);
    QPainter p(&circ);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled, ox, oy, size, size);
    return circ;
}


CropWidget::CropWidget(const QPixmap& src, QWidget* parent)
    : QWidget(parent)
    , m_src(src)
{
    setFixedSize(kD, kD);
    setCursor(Qt::OpenHandCursor);

    m_cx = src.width()  / 2.0;
    m_cy = src.height() / 2.0;

    // Minimum zoom: image must cover the full kD×kD circle bounding box
    m_minZoom = qMax((double)kD / src.width(), (double)kD / src.height());
    m_zoom    = m_minZoom;
}

void CropWidget::setZoom(double z)
{
    m_zoom = qBound(m_minZoom, z, maxZoom());
    clampOffset();
    update();
}

void CropWidget::clampOffset()
{
    // Visible half-size in image-space pixels
    const double hw = kD / 2.0 / m_zoom;
    m_cx = qBound(hw, m_cx, (double)m_src.width()  - hw);
    m_cy = qBound(hw, m_cy, (double)m_src.height() - hw);
}

void CropWidget::syncSlider()
{
    if (!m_slider) return;
    const int pct = qBound(0, (int)(
        (m_zoom - m_minZoom) / (maxZoom() - m_minZoom) * 100.0), 100);
    m_slider->blockSignals(true);
    m_slider->setValue(pct);
    m_slider->blockSignals(false);
}

void CropWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    p.fillRect(rect(), QColor(0x23, 0x27, 0x2A));

    // Draw the source image at current zoom / pan
    const double iw = m_src.width()  * m_zoom;
    const double ih = m_src.height() * m_zoom;
    const double ix = kD / 2.0 - m_cx * m_zoom;
    const double iy = kD / 2.0 - m_cy * m_zoom;
    p.drawPixmap(QRectF(ix, iy, iw, ih), m_src, QRectF(m_src.rect()));

    // Darken the area outside the circle
    QPainterPath outside;
    outside.addRect(QRectF(rect()));
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, kD, kD));
    p.fillPath(outside.subtracted(circle), QColor(0, 0, 0, 160));

    // Circle border
    p.setPen(QPen(QColor(0x5B, 0x65, 0xEA), 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(1, 1, kD - 2, kD - 2));
}

void CropWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging  = true;
        m_lastMouse = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CropWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) return;
    const QPoint delta = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();
    // Drag image: mouse right → center shifts left (image moves right)
    m_cx -= delta.x() / m_zoom;
    m_cy -= delta.y() / m_zoom;
    clampOffset();
    update();
}

void CropWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void CropWidget::wheelEvent(QWheelEvent* e)
{
    const double factor = 1.0 + e->angleDelta().y() / 1200.0;
    setZoom(m_zoom * factor);
    syncSlider();
    e->accept();
}

QByteArray CropWidget::croppedPng(int outSize) const
{
    const double hw = kD / 2.0 / m_zoom;   // half-size in image pixels
    const QRectF srcRect(m_cx - hw, m_cy - hw, hw * 2, hw * 2);

    QPixmap result(outSize, outSize);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(QRectF(0, 0, outSize, outSize), m_src, srcRect);
    p.end();

    QByteArray out;
    QBuffer    buf(&out);
    buf.open(QIODevice::WriteOnly);
    result.save(&buf, "PNG");
    return out;
}

// ── AvatarCropDialog ──────────────────────────────────────────────────────────

AvatarCropDialog::AvatarCropDialog(const QPixmap& src, QWidget* parent)
    : ForjDialog("Crop Avatar", parent)
{
    setFixedSize(320, 430);  // +30 for custom titlebar vs original 400

    auto* layout = new QVBoxLayout(body());
    layout->setContentsMargins(20, 12, 20, 16);
    layout->setSpacing(12);

    auto* hint = new QLabel("Drag to reposition  ·  Scroll or slide to zoom");
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color:#72767D;font-size:11px;");
    layout->addWidget(hint);

    m_crop = new CropWidget(src, this);
    layout->addWidget(m_crop, 0, Qt::AlignCenter);

    auto* zoomRow = new QHBoxLayout;
    auto* zoomLbl = new QLabel("Zoom");
    zoomLbl->setStyleSheet("color:#B9BBBE;");
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 100);
    m_slider->setValue(0);
    m_crop->setSlider(m_slider);
    zoomRow->addWidget(zoomLbl);
    zoomRow->addWidget(m_slider, 1);
    layout->addLayout(zoomRow);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* applyBtn  = new QPushButton("Apply");
    auto* cancelBtn = new QPushButton("Cancel");
    applyBtn->setFixedWidth(80);
    cancelBtn->setFixedWidth(80);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);

    connect(m_slider, &QSlider::valueChanged, this, [this](int val) {
        const double minZ = m_crop->minZoom();
        m_crop->setZoom(minZ + val / 100.0 * (m_crop->maxZoom() - minZ));
    });
    connect(applyBtn,  &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QByteArray AvatarCropDialog::croppedPng() const
{
    return m_crop->croppedPng(256);
}
