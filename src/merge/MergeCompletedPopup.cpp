// SPDX-License-Identifier: GPL-3.0-only

#include "MergeCompletedPopup.h"

#include "ui/AppFonts.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

MergeCompletedPopup::MergeCompletedPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("mergeCompleted"), QSizeF(0.35, 0.46), QSize(500, 390), parent)
    , icon_(new QLabel(this))
    , title_(new QLabel(this))
    , description_(new QLabel(this))
    , branches_(new QLabel(this))
    , commit_(new QLabel(this))
    , resume_(new QPushButton(tr("OK"), this))
    , close_(new QToolButton(this))
{
    setObjectName(QStringLiteral("mergeCompletedPopup"));
    icon_->setObjectName(QStringLiteral("mergeSuccessIcon"));
    icon_->setAlignment(Qt::AlignCenter);
    icon_->setFixedSize(UiMetrics::scaled(44), UiMetrics::scaled(44));
    close_->setAutoRaise(true);
    close_->setAccessibleName(tr("Close confirmation"));
    close_->setToolTip(tr("Close confirmation"));
    connect(close_, &QToolButton::clicked, this, &OverlayPopup::dismiss);
    for (QLabel *label : {title_, description_, branches_, commit_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    AppFonts::setInterfaceFont(title_, 1.8, QFont::DemiBold);
    AppFonts::setInterfaceFont(description_, 1.15, QFont::Normal);
    branches_->setObjectName(QStringLiteral("mergeCompletedRoute"));
    commit_->setObjectName(QStringLiteral("pageDescription"));
    auto *body = new QVBoxLayout;
    body->setContentsMargins(28, 24, 28, 20);
    body->setSpacing(20);
    auto *top = new QHBoxLayout;
    top->addWidget(icon_);
    top->addStretch();
    top->addWidget(close_, 0, Qt::AlignTop);
    body->addLayout(top);
    body->addWidget(title_);
    body->addWidget(description_);
    body->addWidget(branches_);
    body->addWidget(commit_);
    body->addStretch();
    auto *footer = new PopupFooter(this);
    auto *viewCommit = new QPushButton(tr("View Commit"), this);
    viewCommit->setObjectName(QStringLiteral("viewMergeCommit"));
    connect(viewCommit, &QPushButton::clicked, this, [this] {
        dismiss();
        emit commitRequested(commitId_);
    });
    resume_->setDefault(true);
    connect(resume_, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    footer->addButton(viewCommit);
    footer->addButton(resume_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(body, 1);
    layout->addWidget(footer);
    applyTheme(theme);
}

void MergeCompletedPopup::popup(const Integration::Completion &completion)
{
    commitId_ = completion.commitId;
    title_->setText(completion.operation == tr("merge") ? tr("Merge completed")
                                                        : tr("%1 completed").arg(completion.operation));
    description_->setText(tr("The changes have been integrated successfully.\nNo conflicts remain."));
    branches_->setText(completion.operation == tr("merge")
                           ? tr("%1  →  %2").arg(completion.sides.incoming, completion.sides.current)
                           : tr("%1\n%2").arg(completion.sides.current, completion.sides.incoming));
    commit_->setText(tr("Commit %1").arg(completion.commitId));
    showCentered();
    resume_->setFocus();
}

void MergeCompletedPopup::applyTheme(const Theme &theme)
{
    const QColor success = theme.isDark ? QColor("#8fd18a") : QColor("#246f3b");
    const QColor background = ThemeStyle::mixed(theme.window, success, 0.15);
    const QString sheet =
        QStringLiteral("QLabel#mergeSuccessIcon { background: %1; border: 1px solid %2; border-radius: %3px; }"
                       "QLabel#mergeCompletedRoute { background: @base; border: 1px solid @line; border-radius: 4px; "
                       "padding: 14px; }")
            .arg(background.name(), success.name())
            .arg(UiMetrics::scaled(22));
    setStyleSheet(popupStyleSheet(theme, sheet));
    const int size = UiMetrics::scaled(25);
    icon_->setPixmap(UiIcons::icon(QStringLiteral("check"), success).pixmap(size, size));
    close_->setIcon(UiIcons::icon(QStringLiteral("close"), theme.mutedText));
}

void MergeCompletedPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (auto *button = qobject_cast<QPushButton *>(focusWidget()))
            button->click();
        else
            resume_->click();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
