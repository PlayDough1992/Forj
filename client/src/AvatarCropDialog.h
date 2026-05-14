#pragma once

#include <QPixmap>
#include "ForjDialog.h"

class QSlider;

// Shared utility: cover-scale src, center-crop to size×size circle.
QPixmap avatarToCircle(const QPixmap& src, int size);
// Draggable / zoomable crop viewport (no Q_OBJECT — uses slider pointer for sync).
// Minimum zoom is automatically set so the circle is always filled.
class CropWidget : public QWidget
{
public:
    explicit CropWidget(const QPixmap& src, QWidget* parent = nullptr);

    void   setZoom(double z);
    double minZoom() const { return m_minZoom; }
    double maxZoom() const { return m_minZoom * 4.0; }

    // Attach slider so wheel events keep it in sync
    void setSlider(QSlider* s) { m_slider = s; }

    // Returns a square PNG crop (outSize×outSize, no circle clipping applied)
    QByteArray croppedPng(int outSize = 256) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void clampOffset();
    void syncSlider();

    QPixmap  m_src;
    double   m_zoom{1.0};
    double   m_minZoom{1.0};
    double   m_cx{0.0}, m_cy{0.0};   // center in source-image pixel coords
    QPoint   m_lastMouse;
    bool     m_dragging{false};
    QSlider* m_slider{nullptr};

    static constexpr int kD = 280;    // circle / viewport diameter (px)
};

// ─────────────────────────────────────────────────────────────────────────────

class AvatarCropDialog : public ForjDialog
{
    Q_OBJECT
public:
    explicit AvatarCropDialog(const QPixmap& src, QWidget* parent = nullptr);

    // Call after accept() — returns 256×256 PNG bytes ready for upload
    QByteArray croppedPng() const;

private:
    CropWidget* m_crop;
    QSlider*    m_slider;
};
