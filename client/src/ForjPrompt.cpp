#include "ForjPrompt.h"
#include "ForjDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

// ── shared helpers ────────────────────────────────────────────────────────────

static void addButtonRow(QVBoxLayout* lay, QDialog* dlg,
                         const QString& acceptLabel, const QString& rejectLabel = "Cancel")
{
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* acceptBtn = new QPushButton(acceptLabel);
    acceptBtn->setFixedWidth(80);
    acceptBtn->setDefault(true);
    row->addStretch();
    row->addWidget(acceptBtn);
    if (!rejectLabel.isEmpty()) {
        auto* rejectBtn = new QPushButton(rejectLabel);
        rejectBtn->setFixedWidth(80);
        row->addWidget(rejectBtn);
        QObject::connect(rejectBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    }
    QObject::connect(acceptBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    lay->addLayout(row);
}

// ── getText ───────────────────────────────────────────────────────────────────

QString ForjPrompt::getText(QWidget* parent, const QString& title,
                            const QString& label, const QString& defaultText)
{
    ForjDialog dlg(title, parent);
    dlg.setFixedWidth(360);

    auto* lay = new QVBoxLayout(dlg.body());
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(10);

    auto* lbl = new QLabel(label);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    auto* edit = new QLineEdit(defaultText);
    lay->addWidget(edit);

    lay->addSpacing(6);
    addButtonRow(lay, &dlg, "OK");

    QObject::connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return {};
    return edit->text();
}

// ── getItem ───────────────────────────────────────────────────────────────────

QString ForjPrompt::getItem(QWidget* parent, const QString& title,
                            const QString& label, const QStringList& items,
                            int currentIndex)
{
    ForjDialog dlg(title, parent);
    dlg.setFixedWidth(360);

    auto* lay = new QVBoxLayout(dlg.body());
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(10);

    auto* lbl = new QLabel(label);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    auto* combo = new QComboBox;
    combo->addItems(items);
    combo->setCurrentIndex(currentIndex);
    lay->addWidget(combo);

    lay->addSpacing(6);
    addButtonRow(lay, &dlg, "OK");

    if (dlg.exec() != QDialog::Accepted) return {};
    return combo->currentText();
}

// ── warning ───────────────────────────────────────────────────────────────────

void ForjPrompt::warning(QWidget* parent, const QString& title, const QString& text)
{
    ForjDialog dlg(title, parent);
    dlg.setFixedWidth(380);

    auto* lay = new QVBoxLayout(dlg.body());
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(16);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    addButtonRow(lay, &dlg, "OK", {});
    dlg.exec();
}

// ── information ───────────────────────────────────────────────────────────────

void ForjPrompt::information(QWidget* parent, const QString& title, const QString& text)
{
    ForjPrompt::warning(parent, title, text);   // same layout, different title
}

// ── question ──────────────────────────────────────────────────────────────────

bool ForjPrompt::question(QWidget* parent, const QString& title, const QString& text)
{
    ForjDialog dlg(title, parent);
    dlg.setFixedWidth(380);

    auto* lay = new QVBoxLayout(dlg.body());
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(16);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    addButtonRow(lay, &dlg, "Yes", "No");
    return dlg.exec() == QDialog::Accepted;
}
