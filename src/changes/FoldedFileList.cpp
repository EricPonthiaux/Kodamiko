// SPDX-License-Identifier: GPL-3.0-only

#include "FoldedFileList.h"

#include "ui/UiIcons.h"

#include <QToolButton>
#include <QVBoxLayout>

FoldedFileList::FoldedFileList(QWidget *parent)
    : QFrame(parent)
    , expandButton_(new QToolButton(this))
{
    setObjectName(QStringLiteral("foldedFileList"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    expandButton_->setAutoRaise(true);
    expandButton_->setFocusPolicy(Qt::NoFocus);
    expandButton_->setToolTip(tr("Show the file list"));
    connect(expandButton_, &QToolButton::clicked, this, &FoldedFileList::expandRequested);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(expandButton_);
    layout->addStretch();
}

void FoldedFileList::applyTheme(const Theme &theme)
{
    expandButton_->setIcon(UiIcons::icon(QStringLiteral("file-list"), theme.foreground));
}
