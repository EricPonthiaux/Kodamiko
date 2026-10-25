// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingsPanel.h"

#include "ReviewFindingsTable.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"
#include "ui/UiIcons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

ReviewFindingsPanel::ReviewFindingsPanel(QWidget *parent)
    : QFrame(parent)
    , title_(new QLabel(this))
    , summary_(new QLabel(this))
    , close_(new QToolButton(this))
    , table_(new ReviewFindingsTable(this))
{
    setObjectName(QStringLiteral("reviewFindings"));
    // Opaque: the diff under it does not show through.
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.1, QFont::DemiBold);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::PlainText);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_->setForegroundRole(QPalette::PlaceholderText);
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setToolTip(tr("Close the findings and show the diff (Esc)"));
    close_->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(close_, &QToolButton::clicked, this, &ReviewFindingsPanel::closeRequested);
    connect(table_, &ReviewFindingsTable::findingRequested, this, &ReviewFindingsPanel::findingRequested);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(title_, 1);
    header->addWidget(close_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(6);
    layout->addLayout(header);
    layout->addWidget(summary_);
    layout->addWidget(table_, 1);
}

void ReviewFindingsPanel::setSession(ReviewSession *session)
{
    if (session_)
        session_->disconnect(this);
    session_ = session;
    table_->setSession(session);
    if (!session)
        return;
    connect(session, &ReviewSession::stateChanged, this, &ReviewFindingsPanel::showSummary);
    connect(session, &ReviewSession::findingsChanged, this, &ReviewFindingsPanel::showSummary);
    showSummary();
}

void ReviewFindingsPanel::setClosable(bool closable)
{
    close_->setVisible(closable);
    close_->setEnabled(closable); // its Esc shortcut with it
}

void ReviewFindingsPanel::showSummary()
{
    const qsizetype count = session_->findings().size();
    const QString findings = count == 1 ? tr("1 finding") : tr("%1 findings").arg(count);
    title_->setText(tr("Review of %1 · %2").arg(session_->subject(), findings));
    summary_->setText(session_->summary());
    summary_->setVisible(!session_->summary().isEmpty());
}

void ReviewFindingsPanel::applyTheme(const Theme &theme)
{
    close_->setIcon(UiIcons::icon(QStringLiteral("close"), theme.mutedText));
}
