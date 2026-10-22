// SPDX-License-Identifier: GPL-3.0-only

#include "ChangeSignaturePopup.h"

#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Item data role: the parameter's index in the old signature, -1 for a
// new one.
constexpr int oldIndexRole = Qt::UserRole;
constexpr int newParameter = -1;

constexpr int declarationColumn = 0;
constexpr int valueColumn = 1;

constexpr QSizeF windowShare(0.55, 0.55);
constexpr QSize minimumPanelSize(640, 400);
constexpr int marginPx = 16;
constexpr int spacingPx = 8;

constexpr const char *popupRules = R"(
QTableWidget { background: @base; border: 1px solid @line; gridline-color: @line; }
QHeaderView::section { background: @panel; border: none; border-bottom: 1px solid @line; padding: 4px 6px; }
QLabel#signaturePreview { font-family: monospace; }
)";

} // namespace

ChangeSignaturePopup::ChangeSignaturePopup(QString languageId,
                                           ChangeSignature::Signature signature,
                                           Accept accept,
                                           QWidget *parent)
    : OverlayPopup(QStringLiteral("changeSignature"), windowShare, minimumPanelSize, parent)
    , languageId_(std::move(languageId))
    , signature_(std::move(signature))
    , accept_(std::move(accept))
    , header_(new PopupHeader(QStringLiteral("code"), tr("Change Signature"), this))
    , table_(new QTableWidget(0, 2, this))
    , preview_(new QLabel(this))
    , error_(new QLabel(this))
    , refactor_(new QPushButton(tr("Refactor"), this))
{
    header_->setSubtitle(signature_.name);
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);
    connect(this, &OverlayPopup::dismissed, this, &QObject::deleteLater);

    table_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value at the calls")});
    table_->horizontalHeader()->setSectionResizeMode(declarationColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(valueColumn, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int i = 0; i < signature_.parameters.size(); ++i)
        addRow(i, {.oldIndex = i}, signature_.parameters[i]);
    connect(table_, &QTableWidget::itemChanged, this, &ChangeSignaturePopup::updateSignature);

    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(spacingPx);
    const QList<std::pair<QString, void (ChangeSignaturePopup::*)()>> commands = {
        {tr("&Add"), &ChangeSignaturePopup::addParameter},
        {tr("&Remove"), &ChangeSignaturePopup::removeParameter},
        {tr("Move &Up"), &ChangeSignaturePopup::moveUp},
        {tr("Move &Down"), &ChangeSignaturePopup::moveDown},
    };
    for (const auto &[text, command] : commands) {
        auto *button = new QPushButton(text, this);
        connect(button, &QPushButton::clicked, this, command);
        buttons->addWidget(button);
    }
    buttons->addStretch();

    auto *columns = new QHBoxLayout;
    columns->setSpacing(spacingPx);
    columns->addWidget(table_, 1);
    columns->addLayout(buttons);
    preview_->setObjectName(QStringLiteral("signaturePreview"));
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setObjectName(QStringLiteral("popupError"));
    error_->hide();
    auto *body = new QVBoxLayout;
    body->setContentsMargins(marginPx, spacingPx + 4, marginPx, spacingPx + 4);
    body->setSpacing(spacingPx);
    body->addLayout(columns, 1);
    body->addWidget(preview_);
    body->addWidget(error_);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint =
        new QLabel(tr("A new parameter's value is passed by every call; empty, the calls leave it out."), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    hint->setWordWrap(true);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    refactor_->setDefault(true);
    connect(refactor_, &QPushButton::clicked, this, &ChangeSignaturePopup::accept);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(marginPx, spacingPx + 2, marginPx, spacingPx + 2);
    footerLayout->setSpacing(spacingPx);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(refactor_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    const Theme &theme = ThemeStyle::applied();
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(popupRules)) +
                  QStringLiteral("QLabel#popupError { color: %1; }").arg(theme.error.name()));
    header_->applyTheme(theme);
    updateSignature();
}

void ChangeSignaturePopup::popup()
{
    showCentered();
    table_->setFocus();
    if (table_->rowCount() > 0)
        table_->selectRow(0);
}

void ChangeSignaturePopup::addRow(int row, const ChangeSignature::Parameter &parameter, const QString &declaration)
{
    const QSignalBlocker blocker(table_);
    table_->insertRow(row);
    const bool isNew = !parameter.oldIndex;
    auto *declared = new QTableWidgetItem(declaration);
    declared->setData(oldIndexRole, isNew ? newParameter : *parameter.oldIndex);
    auto *value = new QTableWidgetItem(isNew ? parameter.valueAtCalls : QStringLiteral("—"));
    if (!isNew) {
        // A kept parameter: its declaration and its calls' values stay.
        declared->setFlags(declared->flags() & ~Qt::ItemIsEditable);
        value->setFlags(value->flags() & ~(Qt::ItemIsEditable | Qt::ItemIsEnabled));
    }
    table_->setItem(row, declarationColumn, declared);
    table_->setItem(row, valueColumn, value);
}

void ChangeSignaturePopup::addParameter()
{
    const int row = table_->currentRow() < 0 ? table_->rowCount() : table_->currentRow() + 1;
    addRow(row, {}, {});
    table_->selectRow(row);
    table_->editItem(table_->item(row, declarationColumn));
    updateSignature();
}

void ChangeSignaturePopup::removeParameter()
{
    const int row = table_->currentRow();
    if (row < 0)
        return;
    table_->removeRow(row);
    table_->selectRow(std::min(row, table_->rowCount() - 1));
    updateSignature();
}

void ChangeSignaturePopup::moveBy(int rows)
{
    const int row = table_->currentRow();
    const int target = row + rows;
    if (row < 0 || target < 0 || target >= table_->rowCount())
        return;
    const QSignalBlocker blocker(table_);
    for (const int column : {declarationColumn, valueColumn}) {
        QTableWidgetItem *moved = table_->takeItem(row, column);
        table_->setItem(row, column, table_->takeItem(target, column));
        table_->setItem(target, column, moved);
    }
    table_->selectRow(target);
    updateSignature();
}

void ChangeSignaturePopup::moveUp()
{
    moveBy(-1);
}

void ChangeSignaturePopup::moveDown()
{
    moveBy(1);
}

QList<ChangeSignature::Parameter> ChangeSignaturePopup::parameters() const
{
    QList<ChangeSignature::Parameter> parameters;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem *declared = table_->item(row, declarationColumn);
        const int oldIndex = declared->data(oldIndexRole).toInt();
        if (oldIndex != newParameter) {
            parameters.append({.oldIndex = oldIndex});
            continue;
        }
        const QString declaration = declared->text().trimmed();
        parameters.append({.declaration = declaration,
                           .name = ChangeSignature::nameOf(languageId_, declaration),
                           .valueAtCalls = table_->item(row, valueColumn)->text().trimmed()});
    }
    return parameters;
}

void ChangeSignaturePopup::updateSignature()
{
    QStringList declarations;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString declaration = table_->item(row, declarationColumn)->text().trimmed();
        declarations.append(declaration.isEmpty() ? QStringLiteral("…") : declaration); // being typed
    }
    Paragraph::setText(preview_,
                       QStringLiteral("%1(%2)").arg(signature_.name, declarations.join(QStringLiteral(", "))));
    error_->hide();
}

void ChangeSignaturePopup::accept()
{
    const QList<ChangeSignature::Parameter> chosen = parameters();
    const bool isIncomplete = std::ranges::any_of(
        chosen, [](const ChangeSignature::Parameter &p) { return !p.oldIndex && p.declaration.isEmpty(); });
    if (isIncomplete) {
        error_->setText(tr("Give each new parameter its declaration"));
        error_->show();
        return;
    }
    // The popup is deleted once dismissed: what accept_ needs is copied.
    const Accept accept = accept_;
    dismiss();
    accept(chosen);
}

void ChangeSignaturePopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
