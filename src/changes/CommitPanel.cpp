// SPDX-License-Identifier: GPL-3.0-only

#include "CommitPanel.h"

#include "IdentityPopup.h"
#include "agent/CommitMessage.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "ui/UiIcons.h"
#include "workspace/ChoicePopup.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int bodyHeight = 60;
// git's convention for a summary: longer, tools cut it.
constexpr int summaryLimit = 72;
constexpr int meterHeightPx = 3;

} // namespace

CommitPanel::CommitPanel(GitCommands *gitCommands, QWidget *parent)
    : QFrame(parent)
    , commands_(gitCommands)
    , summary_(new QLineEdit(this))
    , summaryCount_(new QLabel(this))
    , summaryBar_(new QProgressBar(this))
    , body_(new QPlainTextEdit(this))
    , amendCheckbox_(new QCheckBox(tr("Amend"), this))
    , generateButton_(new QPushButton(this))
    , reviewButton_(new QPushButton(tr("Review"), this))
    , commitButton_(new QPushButton(tr("Commit"), this))
{
    setObjectName(QStringLiteral("commitPanel"));
    summary_->setObjectName(QStringLiteral("commitSummary"));
    summary_->setPlaceholderText(tr("Summary, in the imperative (Ctrl+Enter to commit)"));
    summaryCount_->setObjectName(QStringLiteral("commitSummaryCount"));
    summaryCount_->setToolTip(tr("Summaries longer than %1 characters are cut by git's tools").arg(summaryLimit));
    summaryBar_->setRange(0, summaryLimit);
    summaryBar_->setTextVisible(false);
    summaryBar_->setFixedHeight(meterHeightPx);
    body_->setObjectName(QStringLiteral("commitBody"));
    body_->setTabChangesFocus(true);
    body_->setMinimumHeight(bodyHeight);
    body_->setPlaceholderText(tr("Why, not how (optional)"));
    amendCheckbox_->setToolTip(tr("Replace the last commit with the staged changes and this message"));
    generateButton_->setText(tr("Generate"));
    commitButton_->setDefault(true);
    commitButton_->setToolTip(tr("Commit the staged changes (Ctrl+Enter)"));
    for (QWidget *field : {static_cast<QWidget *>(summary_), static_cast<QWidget *>(body_)}) {
        for (const int key : {Qt::Key_Return, Qt::Key_Enter}) {
            const auto *shortcut =
                new QShortcut(QKeySequence(Qt::CTRL | key), field, nullptr, nullptr, Qt::WidgetShortcut);
            connect(shortcut, &QShortcut::activated, this, &CommitPanel::commit);
        }
    }
    // The summary is one line: Enter goes on to the body.
    connect(summary_, &QLineEdit::returnPressed, body_, qOverload<>(&QWidget::setFocus));
    connect(summary_, &QLineEdit::textChanged, this, &CommitPanel::updateMeter);
    connect(amendCheckbox_, &QCheckBox::toggled, this, &CommitPanel::onAmendToggled);
    connect(commitButton_, &QPushButton::clicked, this, &CommitPanel::commit);
    connect(generateButton_, &QPushButton::clicked, this, &CommitPanel::generateMessage);
    connect(reviewButton_, &QPushButton::clicked, this, &CommitPanel::reviewRequested);

    // Amend at the left; the secondary action, then the main one, at the right.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    actions->addWidget(amendCheckbox_);
    actions->addStretch();
    actions->addWidget(reviewButton_);
    actions->addWidget(generateButton_);
    actions->addWidget(commitButton_);

    // The meter: the count at the left of its track.
    auto *meter = new QHBoxLayout;
    meter->setSpacing(8);
    meter->addWidget(summaryCount_);
    meter->addWidget(summaryBar_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(summary_);
    layout->addLayout(meter);
    // The body takes the height the panel is given; the actions stay below.
    layout->addWidget(body_, 1);
    layout->addSpacing(2);
    layout->addLayout(actions);
    updateMeter();
}

QString CommitPanel::message() const
{
    const QString summary = summary_->text().trimmed();
    const QString body = body_->toPlainText().trimmed();
    if (body.isEmpty())
        return summary;
    return summary + QStringLiteral("\n\n") + body;
}

void CommitPanel::setMessage(const QString &message)
{
    const QString trimmed = message.trimmed();
    const qsizetype lineEnd = trimmed.indexOf(QLatin1Char('\n'));
    const QString summary = lineEnd < 0 ? trimmed : trimmed.left(lineEnd).trimmed();
    const QString body = lineEnd < 0 ? QString() : trimmed.mid(lineEnd + 1).trimmed();
    // Inserted over a selection rather than set, so that Ctrl+Z undoes it.
    summary_->selectAll();
    summary_->insert(summary);
    QTextCursor cursor(body_->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(body);
}

void CommitPanel::clearMessage()
{
    summary_->clear();
    body_->clear();
}

void CommitPanel::updateMeter()
{
    const int length = int(summary_->text().size());
    const bool isOver = length > summaryLimit;
    summaryCount_->setText(QStringLiteral("%1 / %2").arg(length).arg(summaryLimit));
    summaryCount_->setStyleSheet(isOver ? QStringLiteral("color: %1").arg(overColor_.name()) : QString());
    summaryBar_->setValue(std::min(length, summaryLimit));
    const QColor &fill = isOver ? overColor_ : meterColor_;
    summaryBar_->setStyleSheet(QStringLiteral("QProgressBar { border: none; border-radius: 1px; background: %1; }"
                                              "QProgressBar::chunk { border-radius: 1px; background: %2; }")
                                   .arg(trackColor_.name(QColor::HexArgb), fill.name()));
}

QSize CommitPanel::sizeHint() const
{
    return minimumSizeHint();
}

void CommitPanel::setStatus(const RepositoryStatus *status, const QString &pathspec)
{
    status_ = status;
    pathspec_ = pathspec;
    updateActions();
}

void CommitPanel::reset()
{
    clearMessage();
    const QSignalBlocker blocker(amendCheckbox_);
    amendCheckbox_->setChecked(false);
}

void CommitPanel::updateActions()
{
    const bool hasChanges = status_ && (!status_->staged.isEmpty() || !status_->unstaged.isEmpty());
    const bool hasLastCommit = status_ && !status_->info.unborn;
    commitButton_->setEnabled(!commands_->isBusy() && hasChanges);
    generateButton_->setEnabled(hasChanges);
    reviewButton_->setEnabled(status_ && !status_->staged.isEmpty());
    amendCheckbox_->setEnabled(hasChanges && hasLastCommit);
    if (!hasChanges)
        amendCheckbox_->setChecked(false); // the changes it was for are gone
}

void CommitPanel::focusMessage()
{
    summary_->setFocus();
}

void CommitPanel::onAmendToggled(bool amend)
{
    if (!status_)
        return;
    const QString lastMessage = status_->info.headMessage.trimmed();
    if (amend && message().isEmpty())
        setMessage(lastMessage);
    else if (!amend && message() == lastMessage)
        clearMessage();
}

void CommitPanel::generateMessage()
{
    if (!status_)
        return;
    // As commit(): with nothing staged, everything will be.
    const bool isAmend = amendCheckbox_->isChecked();
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
    setMessage(message);
    summary_->setFocus();
    emit statusMessage(tr("Commit message written by %1: review it, then commit (Ctrl+Enter)").arg(agentName_));
}

void CommitPanel::commit()
{
    if (!status_ || commands_->isBusy())
        return;
    const QString message = this->message();
    const bool amend = amendCheckbox_->isChecked();
    if (summary_->text().trimmed().isEmpty()) {
        emit statusMessage(tr("Write the commit's summary first"));
        summary_->setFocus();
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
            const QSignalBlocker blocker(amendCheckbox_);
            amendCheckbox_->setChecked(false);
            clearMessage();
        },
        [this, message, amend, stageEverything](const GitError &error) {
            if (error.kind() != GitError::Kind::NoIdentity)
                return false;
            askIdentityThenCommit(message, amend, stageEverything);
            return true;
        });
}

void CommitPanel::askIdentityThenCommit(const QString &message, bool amend, bool stageEverything)
{
    if (!status_)
        return;
    const RepositoryInfo info = status_->info;
    const auto save = [this, info, message, amend, stageEverything](const GitOperations::Identity &identity,
                                                                    GitOperations::ConfigScope scope) {
        commands_->run(
            tr("Set identity"),
            [info, identity, scope] {
                GitOperations::setIdentity(info, identity, scope);
                return QObject::tr("Commits are now signed %1 <%2>").arg(identity.name, identity.email);
            },
            [this, message, amend, stageEverything] { runCommit(message, amend, stageEverything); });
    };
    // Read here, on the UI thread: two quick git config calls.
    auto *popup = new IdentityPopup(QFileInfo(info.workDir).fileName(), GitOperations::identity(info), save, this);
    popup->popup();
}

void CommitPanel::setAgent(const QString &name, const QString &iconName)
{
    agentName_ = name;
    agentIconName_ = iconName;
    generateButton_->setToolTip(
        tr("Have the %1 session in the terminal write the commit message from the changes to commit (Ctrl+Alt+G)")
            .arg(name));
    reviewButton_->setToolTip(
        tr("Have a separate %1 session review the staged changes, its findings shown in the diff").arg(name));
}

void CommitPanel::applyTheme(const Theme &theme)
{
    generateButton_->setIcon(UiIcons::icon(agentIconName_, theme.foreground));
    meterColor_ = theme.accent;
    overColor_ = theme.error;
    trackColor_ = theme.line;
    updateMeter();
}
