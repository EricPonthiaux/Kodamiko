// SPDX-License-Identifier: GPL-3.0-only

#include "RefactoringPreviewPopup.h"

#include "EditPreview.h"
#include "changes/DiffView.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

constexpr const char *previewRules = R"(
QListWidget#refactoredFiles { background: transparent; border: none; border-right: 1px solid @line; outline: 0; }
QListWidget#refactoredFiles::item { padding: 4px 8px; border-radius: 2px; }
QListWidget#refactoredFiles::item:hover { background: @hover; }
QListWidget#refactoredFiles::item:selected { background: @selection; color: @accentText; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

constexpr QSizeF windowShare(0.75, 0.8);
constexpr QSize minimumPanelSize(860, 540);
constexpr int marginPx = 16;
constexpr int spacingPx = 12;
constexpr int listStretch = 3;
constexpr int diffStretch = 8;

} // namespace

RefactoringPreviewPopup::RefactoringPreviewPopup(Preview preview, Apply apply, QWidget *parent)
    : OverlayPopup(QStringLiteral("refactoringPreview"), windowShare, minimumPanelSize, parent)
    , preview_(std::move(preview))
    , apply_(std::move(apply))
    , diffFiles_(EditPreview::diffFiles(preview_.files, preview_.root))
    , header_(new PopupHeader(QStringLiteral("files"), tr("Refactoring Preview"), this))
    , list_(new QListWidget(this))
    , diff_(new DiffView(ThemeStyle::applied(), this))
    , refactor_(new QPushButton(tr("Do Refactor"), this))
{
    header_->setSubtitle(preview_.title);
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);
    connect(this, &OverlayPopup::dismissed, this, &QObject::deleteLater);

    list_->setObjectName(QStringLiteral("refactoredFiles"));
    list_->setTextElideMode(Qt::ElideLeft);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (qsizetype i = 0; i < preview_.files.size(); ++i) {
        auto *item = new QListWidgetItem(diffFiles_[i].path, list_);
        item->setToolTip(diffFiles_[i].status);
        item->setCheckState(Qt::Checked);
        if (!preview_.files[i].hasTextEditsOnly)
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    }
    connect(list_, &QListWidget::currentRowChanged, this, &RefactoringPreviewPopup::showSelected);
    connect(list_, &QListWidget::itemChanged, this, &RefactoringPreviewPopup::updateButton);
    auto *columns = new QWidget(this);
    auto *columnsLayout = new QHBoxLayout(columns);
    columnsLayout->setContentsMargins(marginPx, spacingPx, marginPx, spacingPx);
    columnsLayout->setSpacing(spacingPx);
    columnsLayout->addWidget(list_, listStretch);
    columnsLayout->addWidget(diff_, diffStretch);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Uncheck a file to leave it as it is."), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    auto *cancel = new QPushButton(tr("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    refactor_->setDefault(true);
    connect(refactor_, &QPushButton::clicked, this, &RefactoringPreviewPopup::accept);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(marginPx, spacingPx - 2, marginPx, spacingPx - 2);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(refactor_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(columns, 1);
    layout->addWidget(footer);

    const Theme &theme = ThemeStyle::applied();
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(previewRules)));
    header_->applyTheme(theme);
    diff_->applyTheme(theme);
}

void RefactoringPreviewPopup::popup()
{
    list_->setCurrentRow(0);
    showSelected();
    showCentered();
    refactor_->setFocus();
}

void RefactoringPreviewPopup::accept()
{
    QSet<QString> excluded;
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->checkState() == Qt::Unchecked)
            excluded.insert(preview_.files[row].path);
    }
    const LspProtocol::WorkspaceEdit edit = EditPreview::without(preview_.edit, excluded);
    // The popup is deleted once dismissed: what apply_ needs is copied.
    const Apply apply = apply_;
    dismiss();
    if (!edit.isEmpty())
        apply(edit);
}

void RefactoringPreviewPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}

void RefactoringPreviewPopup::showSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= diffFiles_.size())
        return;
    diff_->setFiles({diffFiles_[row]});
}

void RefactoringPreviewPopup::updateButton()
{
    bool anyChecked = false;
    for (int row = 0; row < list_->count(); ++row)
        anyChecked = anyChecked || list_->item(row)->checkState() == Qt::Checked;
    refactor_->setEnabled(anyChecked);
}
