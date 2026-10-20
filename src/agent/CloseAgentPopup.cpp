// SPDX-License-Identifier: GPL-3.0-only

#include "CloseAgentPopup.h"

#include "ui/PopupWidgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *closeRules = R"(
QPushButton#deleteWorktree { color: #e06c75; }
)";

} // namespace

CloseAgentPopup::CloseAgentPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("closeAgent"), QSizeF(0.4, 0.26), QSize(600, 260), parent)
    , header_(new PopupHeader(QString(), QString(), this))
    , text_(new QLabel(this))
    , merge_(new QPushButton(tr("Merge and Delete"), this))
    , keep_(new QPushButton(tr("Keep the Worktree"), this))
    , delete_(new QPushButton(tr("Delete the Worktree"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &CloseAgentPopup::next);

    // Selectable: the folder is copied from it.
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 14, 16, 14);
    body->addWidget(text_, 1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    delete_->setObjectName(QStringLiteral("deleteWorktree"));
    for (QPushButton *button : {cancel, delete_, keep_, merge_}) {
        button->setParent(footer);
        button->setMinimumWidth(84);
    }
    // Highlighted, as in the message box it replaces: nothing is lost by mistake.
    cancel->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &CloseAgentPopup::next);
    connect(delete_, &QPushButton::clicked, this, [this] { choose(WorktreeFate::Delete); });
    connect(keep_, &QPushButton::clicked, this, [this] { choose(WorktreeFate::Keep); });
    connect(merge_, &QPushButton::clicked, this, [this] { choose(WorktreeFate::MergeAndDelete); });
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(delete_);
    footerLayout->addStretch(1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(keep_);
    footerLayout->addWidget(merge_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void CloseAgentPopup::popup(const Worktree &worktree)
{
    load(worktree);
    if (isVisible())
        return;
    showCentered();
    setFocus();
}

void CloseAgentPopup::load(const Worktree &worktree)
{
    const bool isQueued =
        std::ranges::any_of(queue_, [&worktree](const Worktree &queued) { return queued.name == worktree.name; });
    if (isQueued)
        return;
    queue_.append(worktree);
    showFirst();
}

const Worktree *CloseAgentPopup::asked() const
{
    return queue_.isEmpty() ? nullptr : &queue_.first();
}

qsizetype CloseAgentPopup::waitingCount() const
{
    return std::max<qsizetype>(queue_.size() - 1, 0);
}

void CloseAgentPopup::showFirst()
{
    const Worktree &worktree = queue_.first();
    header_->setTitle(tr("Close %1").arg(worktree.name));
    QString subtitle = tr("On the branch %1").arg(worktree.branch);
    if (const qsizetype waiting = waitingCount(); waiting > 0)
        subtitle += tr(" · %n more session(s) to close after this one", nullptr, int(waiting));
    header_->setSubtitle(subtitle);
    text_->setText(tr("The worktree of %1 holds changes or commits that are not merged.\n\nFolder: %2")
                       .arg(worktree.name, worktree.path));
}

void CloseAgentPopup::next()
{
    if (queue_.isEmpty())
        return;
    // Copied: askNext() drops it from the queue.
    const Worktree worktree = queue_.first();
    askNext();
    emit cancelled(worktree);
}

void CloseAgentPopup::askNext()
{
    if (!queue_.isEmpty())
        queue_.removeFirst();
    if (queue_.isEmpty()) {
        dismiss();
        return;
    }
    showFirst();
}

void CloseAgentPopup::setIconName(const QString &iconName)
{
    header_->setIconName(iconName);
}

void CloseAgentPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(closeRules)));
    header_->applyTheme(theme);
}

void CloseAgentPopup::choose(WorktreeFate fate)
{
    if (queue_.isEmpty())
        return;
    // Copied: askNext() drops it from the queue.
    const Worktree worktree = queue_.first();
    askNext();
    emit fateChosen(worktree, fate);
}

void CloseAgentPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        next();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
