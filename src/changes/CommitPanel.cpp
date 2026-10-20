// SPDX-License-Identifier: GPL-3.0-only

#include "CommitPanel.h"

#include "agent/CommitMessage.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "ui/UiIcons.h"
#include "workspace/ChoicePopup.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

constexpr int messageHeight = 84;

} // namespace

CommitPanel::CommitPanel(GitCommands *commands, QWidget *parent)
    : QFrame(parent)
    , commands_(commands)
    , message_(new QPlainTextEdit(this))
    , amend_(new QCheckBox(tr("Amend"), this))
    , generateButton_(new QPushButton(this))
    , commitButton_(new QPushButton(tr("Commit"), this))
{
    setObjectName(QStringLiteral("commitPanel"));
    message_->setTabChangesFocus(true);
    message_->setFixedHeight(messageHeight);
    message_->setPlaceholderText(tr("Commit message (Ctrl+Enter to commit)"));
    amend_->setToolTip(tr("Replace the last commit with the staged changes and this message"));
    generateButton_->setText(tr("Generate"));
    commitButton_->setDefault(true);
    commitButton_->setToolTip(tr("Commit the staged changes (Ctrl+Enter)"));
    for (const int key : {Qt::Key_Return, Qt::Key_Enter}) {
        const auto *shortcut =
            new QShortcut(QKeySequence(Qt::CTRL | key), message_, nullptr, nullptr, Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, &CommitPanel::commit);
    }
    connect(amend_, &QCheckBox::toggled, this, &CommitPanel::onAmendToggled);
    connect(commitButton_, &QPushButton::clicked, this, &CommitPanel::commit);
    connect(generateButton_, &QPushButton::clicked, this, &CommitPanel::generateMessage);

    // Amend at the left; the secondary action, then the main one, at the right.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    actions->addWidget(amend_);
    actions->addStretch();
    actions->addWidget(generateButton_);
    actions->addWidget(commitButton_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(message_);
    layout->addLayout(actions);
}

void CommitPanel::setStatus(const RepositoryStatus *status, const QString &pathspec)
{
    status_ = status;
    pathspec_ = pathspec;
    updateActions();
}

void CommitPanel::reset()
{
    message_->clear();
    const QSignalBlocker blocker(amend_);
    amend_->setChecked(false);
}

void CommitPanel::updateActions()
{
    commitButton_->setEnabled(!commands_->isBusy() && status_);
    amend_->setEnabled(status_ && !status_->info.unborn);
}

void CommitPanel::focusMessage()
{
    message_->setFocus();
}

void CommitPanel::onAmendToggled(bool amend)
{
    if (!status_)
        return;
    const QString lastMessage = status_->info.headMessage.trimmed();
    if (amend && message_->toPlainText().trimmed().isEmpty())
        message_->setPlainText(lastMessage);
    else if (!amend && message_->toPlainText().trimmed() == lastMessage)
        message_->clear();
}

void CommitPanel::generateMessage()
{
    if (!status_)
        return;
    // As commit(): with nothing staged, everything will be.
    const bool isAmend = amend_->isChecked();
    const bool stagesEverything = status_->staged.isEmpty() && !isAmend;
    if (stagesEverything && status_->unstaged.isEmpty()) {
        emit statusMessage(tr("Nothing to commit"));
        return;
    }
    CommitMessage::Scope scope = CommitMessage::Scope::Staged;
    if (isAmend)
        scope = CommitMessage::Scope::Amend;
    else if (stagesEverything)
        scope = CommitMessage::Scope::Everything;
    emit messageRequested(CommitMessage::request(scope, pathspec_));
    emit statusMessage(tr("%1 is writing the commit message in its session…").arg(agentName_));
}

void CommitPanel::showGeneratedMessage(const QString &message)
{
    // Through a cursor, so that Ctrl+Z brings back what was written before.
    QTextCursor cursor(message_->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(message);
    message_->setTextCursor(cursor);
    message_->setFocus();
    emit statusMessage(tr("Commit message written by %1: review it, then commit (Ctrl+Enter)").arg(agentName_));
}

void CommitPanel::commit()
{
    if (!status_ || commands_->isBusy())
        return;
    const QString message = message_->toPlainText().trimmed();
    const bool amend = amend_->isChecked();
    if (message.isEmpty()) {
        emit statusMessage(tr("Write the commit message first"));
        message_->setFocus();
        return;
    }
    if (!status_->staged.isEmpty() || amend) {
        runCommit(message, amend, false);
        return;
    }
    if (status_->unstaged.isEmpty()) {
        emit statusMessage(tr("Nothing to commit"));
        return;
    }
    auto *popup =
        new ChoicePopup({.title = tr("Commit"),
                         .location = {},
                         .message = tr("No changes are staged. Stage all changes (%n file(s)) and commit "
                                       "them?",
                                       nullptr, static_cast<int>(status_->unstaged.size())),
                         .details = {}},
                        {{tr("Stage All and Commit"), [this, message] { runCommit(message, false, true); }}}, this);
    popup->popup();
}

void CommitPanel::runCommit(const QString &message, bool amend, bool stageEverything)
{
    if (!status_)
        return;
    commands_->run(
        tr("Commit"),
        [info = status_->info, message, amend, stageEverything, path = pathspec_] {
            if (stageEverything)
                GitOperations::stage(info, {path});
            const QString id = GitOperations::commit(info, message, amend);
            return amend ? QObject::tr("Last commit replaced by %1").arg(id) : QObject::tr("Created commit %1").arg(id);
        },
        [this] {
            const QSignalBlocker blocker(amend_);
            amend_->setChecked(false);
            message_->clear();
        });
}

void CommitPanel::setAgent(const QString &name, const QString &iconName)
{
    agentName_ = name;
    agentIconName_ = iconName;
    generateButton_->setToolTip(
        tr("Have the %1 session in the terminal write the commit message from the changes to commit (Ctrl+Alt+G)")
            .arg(name));
}

void CommitPanel::applyTheme(const Theme &theme)
{
    generateButton_->setIcon(UiIcons::icon(agentIconName_, theme.foreground));
}
