#pragma once
#include <QString>
#include <QStringList>

class QWidget;

// ── ForjPrompt ─────────────────────────────────────────────────────────────────
// Themed replacements for QInputDialog and QMessageBox.
// All methods are modal and return when the user dismisses the dialog.
namespace ForjPrompt
{
    // Returns the entered text, or {} if cancelled.
    QString getText(QWidget* parent, const QString& title,
                    const QString& label, const QString& defaultText = {});

    // Returns the selected item text, or {} if cancelled.
    QString getItem(QWidget* parent, const QString& title,
                    const QString& label, const QStringList& items,
                    int currentIndex = 0);

    void    warning    (QWidget* parent, const QString& title, const QString& text);
    void    information(QWidget* parent, const QString& title, const QString& text);
    bool    question   (QWidget* parent, const QString& title, const QString& text);
}
