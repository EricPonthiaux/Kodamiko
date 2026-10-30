// SPDX-License-Identifier: GPL-3.0-only

#include "MergeReadyPanel.h"

#include "ui/AppFonts.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>

MergeReadyPanel::MergeReadyPanel(QWidget *parent)
    : QWidget(parent)
    , icon_(new QLabel(this))
    , title_(new QLabel(this))
    , description_(new QLabel(this))
    , files_(new QLabel(this))
    , messageLabel_(new QLabel(tr("Merge commit message"), this))
    , message_(new QPlainTextEdit(this))
    , hint_(new QLabel(this))
    , progress_(new QProgressBar(this))
{
    setObjectName(QStringLiteral("mergeReadyPanel"));
    AppFonts::setInterfaceFont(title_, 1.55, QFont::DemiBold);
    for (QLabel *label : {title_, description_, files_, hint_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    description_->setObjectName(QStringLiteral("pageDescription"));
    hint_->setObjectName(QStringLiteral("pageDescription"));
    files_->setObjectName(QStringLiteral("mergeReadyFiles"));
    files_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_->setObjectName(QStringLiteral("mergeCommitMessage"));
    message_->setAccessibleName(tr("Merge commit message"));
    message_->setMaximumHeight(UiMetrics::scaled(100));
    messageLabel_->setBuddy(message_);
    connect(message_, &QPlainTextEdit::textChanged, this, &MergeReadyPanel::messageChanged);
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(UiMetrics::scaled(4));
    progress_->hide();

    auto *content = new QWidget(this);
    content->setMaximumWidth(UiMetrics::scaled(640));
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(24, 24, 24, 24);
    body->setSpacing(16);
    body->addStretch();
    body->addWidget(icon_);
    body->addWidget(title_);
    body->addWidget(description_);
    body->addWidget(files_);
    body->addWidget(messageLabel_);
    body->addWidget(message_);
    body->addWidget(hint_);
    body->addWidget(progress_);
    body->addStretch();
    auto *center = new QWidget(this);
    auto *row = new QHBoxLayout(center);
    row->addStretch();
    row->addWidget(content, 1);
    row->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(center);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

void MergeReadyPanel::showSummary(const QString &operation, const QStringList &paths, const QString &message)
{
    operation_ = operation;
    files_->setText(paths.join(QLatin1Char('\n')));
    const bool isMerge = operation == tr("merge");
    messageLabel_->setVisible(isMerge);
    message_->setVisible(isMerge);
    if (isMerge && !messageLoaded_) {
        message_->setPlainText(message);
        messageLoaded_ = true;
    }
    setFinalizing(false);
}

void MergeReadyPanel::setFinalizing(bool finalizing)
{
    title_->setText(finalizing ? tr("Finishing %1…").arg(operation_) : tr("Ready to finish the %1").arg(operation_));
    description_->setText(finalizing ? tr("Git is recording the result. Please wait.")
                                     : tr("All conflicts are resolved and saved. Review the result, then continue."));
    hint_->setText(tr("This window closes when the operation succeeds.\nA confirmation will appear in the editor."));
    message_->setEnabled(!finalizing);
    progress_->setVisible(finalizing);
}

void MergeReadyPanel::applyTheme(const Theme &theme)
{
    const int size = UiMetrics::scaled(40);
    icon_->setPixmap(UiIcons::icon(QStringLiteral("check"), theme.accent).pixmap(size, size));
    message_->setFont(theme.font);
}

QString MergeReadyPanel::message() const
{
    return message_->toPlainText();
}
