// SPDX-License-Identifier: GPL-3.0-only

#include "CommitHeader.h"

#include "CommitDelegate.h"
#include "git/FilePatch.h"
#include "ui/AppFonts.h"
#include "ui/UiIcons.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>

namespace {

// The message shows this many lines until expanded.
constexpr int collapsedMessageLines = 5;
// Expanded, the message takes at most this share of the pane it heads.
constexpr int expandedShareDivisor = 3;
constexpr int avatarSize = 18;

QString formatDate(const QDateTime &date)
{
    return QLocale().toString(date.toLocalTime(), QLocale::ShortFormat);
}

QLabel *mutedLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("historyMuted"));
    return label;
}

} // namespace

CommitHeader::CommitHeader(QWidget *parent)
    : QWidget(parent)
    , hint_(mutedLabel(this))
    , commit_(new QWidget(this))
    , summary_(new QLabel(commit_))
    , avatar_(new QLabel(commit_))
    , meta_(mutedLabel(commit_))
    , claudeMark_(new QLabel(tr("✳ Claude"), commit_))
    , shortId_(mutedLabel(commit_))
    , copyId_(new QToolButton(commit_))
    , message_(new QTextBrowser(commit_))
    , moreMessage_(new QToolButton(commit_))
    , stats_(new QLabel(commit_))
{
    setObjectName(QStringLiteral("commitHeader"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    hint_->setWordWrap(true);

    AppFonts::setInterfaceFont(summary_, 1.05, QFont::DemiBold);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    claudeMark_->setStyleSheet(QStringLiteral("color: %1").arg(CommitDelegate::claudeColor().name()));
    shortId_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    copyId_->setAutoRaise(true);
    copyId_->setFocusPolicy(Qt::NoFocus);
    copyId_->setToolTip(tr("Copy the commit's ID"));
    connect(copyId_, &QToolButton::clicked, this, [this] { QApplication::clipboard()->setText(id_); });
    auto *meta = new QHBoxLayout;
    meta->setSpacing(6);
    meta->addWidget(avatar_);
    meta->addWidget(meta_, 1);
    meta->addWidget(claudeMark_);
    meta->addWidget(shortId_);
    meta->addWidget(copyId_);

    message_->setOpenLinks(false);
    message_->setFrameShape(QFrame::NoFrame);
    // Part of the header, not a field: no border of its own.
    message_->setStyleSheet(QStringLiteral("QTextBrowser { border: none; border-radius: 0; padding: 0; }"));
    message_->document()->setDocumentMargin(0);
    moreMessage_->setObjectName(QStringLiteral("linkButton"));
    moreMessage_->setAutoRaise(true);
    moreMessage_->setFocusPolicy(Qt::NoFocus);
    connect(moreMessage_, &QToolButton::clicked, this, [this] {
        isMessageExpanded_ = !isMessageExpanded_;
        fitMessage();
    });

    auto *commitLayout = new QVBoxLayout(commit_);
    commitLayout->setContentsMargins(0, 0, 0, 0);
    commitLayout->setSpacing(6);
    commitLayout->addWidget(summary_);
    commitLayout->addLayout(meta);
    commitLayout->addWidget(message_);
    commitLayout->addWidget(moreMessage_, 0, Qt::AlignLeft);
    commitLayout->addWidget(stats_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 8);
    layout->addWidget(hint_);
    layout->addWidget(commit_);
    showHint({});
}

bool CommitHeader::showsCommit() const
{
    return !commit_->isHidden();
}

void CommitHeader::showHint(const QString &hint)
{
    id_.clear();
    message_->clear();
    commit_->hide();
    hint_->setText(hint);
    hint_->setVisible(!hint.isEmpty());
    // Nothing to say: no room taken above the diff.
    setVisible(!hint.isEmpty());
}

void CommitHeader::showCommit(const CommitDetails &details)
{
    const CommitSummary &commit = details.summary;
    id_ = commit.id;
    summary_->setText(commit.summary);
    avatar_->setPixmap(CommitDelegate::avatar(commit.author, avatarSize, devicePixelRatioF()));
    meta_->setText(QStringLiteral("%1 · %2").arg(commit.author, CommitDelegate::relativeDate(commit.date)));
    meta_->setToolTip(QStringLiteral("%1 <%2>\n%3").arg(commit.author, commit.email, formatDate(commit.date)));
    claudeMark_->setVisible(CommitDelegate::isMadeWithClaude(commit));
    shortId_->setText(commit.shortId);
    shortId_->setToolTip(commit.id);
    isMessageExpanded_ = false;
    message_->setHtml(messageHtml(details));
    stats_->setText(statsHtml(details));
    hint_->hide();
    commit_->show();
    show();
    // Once laid out.
    QTimer::singleShot(0, this, &CommitHeader::fitMessage);
}

void CommitHeader::applyTheme(const Theme &theme)
{
    copyId_->setIcon(UiIcons::icon(QStringLiteral("copy"), theme.mutedText));
    shortId_->setFont(AppFonts::codeFont(font().pointSizeF() * 0.95));
}

QString CommitHeader::statsHtml(const CommitDetails &details) const
{
    int added = 0;
    int removed = 0;
    for (const FileDiff &file : details.files) {
        const FilePatch patch = FilePatch::parse(file.patch);
        added += patch.addedLines();
        removed += patch.removedLines();
    }
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    return QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp; <span style='color:%3; font-weight:600'>+%4</span>"
                          "&nbsp; <span style='color:%5; font-weight:600'>−%6</span>")
        .arg(palette().color(QPalette::PlaceholderText).name(),
             details.files.size() == 1 ? tr("1 file changed") : tr("%1 files changed").arg(details.files.size()),
             dark ? QStringLiteral("#3fb950") : QStringLiteral("#1a7f37"), QString::number(added),
             dark ? QStringLiteral("#f85149") : QStringLiteral("#cf222e"), QString::number(removed));
}

QString CommitHeader::messageHtml(const CommitDetails &details) const
{
    const CommitSummary &commit = details.summary;
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    QString html;
    const QString body = details.message.section(QLatin1Char('\n'), 1).trimmed();
    if (!body.isEmpty())
        html += QStringLiteral("<p style='white-space:pre-wrap; margin:0 0 8px 0'>%1</p>").arg(body.toHtmlEscaped());
    QStringList notes{commit.email.toHtmlEscaped() + QStringLiteral(" · ") + formatDate(commit.date)};
    if (!details.committer.isEmpty())
        notes.append(
            tr("Committed by %1 on %2").arg(details.committer.toHtmlEscaped(), formatDate(details.commitDate)));
    if (details.parentIds.size() > 1)
        notes.append(tr("Merge of %1 — diff against %2")
                         .arg(details.parentIds.join(QStringLiteral(", ")), details.parentIds.first()));
    html += QStringLiteral("<p style='color:%1; margin:0'>%2</p>").arg(muted, notes.join(QStringLiteral("<br>")));
    if (!commit.refs.isEmpty()) {
        QStringList refs;
        for (const QString &ref : commit.refs)
            refs.append(QStringLiteral("<span style='color:%1; font-weight:600'>%2</span>")
                            .arg(CommitDelegate::refColor(ref).lighter(125).name(), ref.toHtmlEscaped()));
        html += QStringLiteral("<p style='margin:6px 0 0 0'>%1</p>").arg(refs.join(QStringLiteral("&nbsp;&nbsp; ")));
    }
    return html;
}

void CommitHeader::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitMessage(); // its width changed
}

// As tall as the message, up to a few lines until expanded.
void CommitHeader::fitMessage()
{
    if (!showsCommit())
        return;
    message_->document()->setTextWidth(message_->viewport()->width());
    const int wanted = qCeil(message_->document()->size().height()) + 2;
    const int collapsed = message_->fontMetrics().lineSpacing() * collapsedMessageLines;
    const bool isLong = wanted > collapsed;
    moreMessage_->setVisible(isLong);
    moreMessage_->setText(isMessageExpanded_ ? tr("Show less") : tr("Show more"));
    // Expanded, still leaving the diff room.
    const int paneHeight = parentWidget() ? parentWidget()->height() : 0;
    const int expanded = std::min(wanted, std::max(collapsed, paneHeight / expandedShareDivisor));
    const bool isCollapsed = isLong && !isMessageExpanded_;
    message_->setFixedHeight(isCollapsed ? collapsed : expanded);
    // Cut, it reads as cut: "Show more" says the rest is there.
    message_->setVerticalScrollBarPolicy(isCollapsed ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
}
