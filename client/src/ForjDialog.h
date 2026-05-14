#pragma once
#include <QDialog>
#include "TitleBar.h"

// ── ForjDialog ────────────────────────────────────────────────────────────────
// Base class for all Forj dialogs: frameless, rounded corners, custom titlebar.
// Subclasses build their content into body() instead of `this`.
class ForjDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ForjDialog(const QString& title, QWidget* parent = nullptr,
                        Qt::WindowFlags extraFlags = Qt::Window);

    // Content container — subclasses add their layout here.
    QWidget* body() const { return m_body; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;

private:
    TitleBar* m_titleBar{nullptr};
    QWidget*  m_body{nullptr};
};
