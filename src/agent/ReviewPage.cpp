// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewPage.h"

#include "ReviewBar.h"
#include "ReviewFindingsPanel.h"
#include "ui/AppFonts.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int marginPx = 8;
constexpr int invitationWidthPx = 520;

} // namespace

ReviewPage::ReviewPage(QWidget *parent)
    : QWidget(parent)
    , bar_(new ReviewBar(this))
    , findings_(new ReviewFindingsPanel(this))
{
    invitation_ = createInvitation();
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    findings_->setClosable(false);
    connect(start_, &QPushButton::clicked, this, &ReviewPage::reviewRequested);
    connect(bar_, &ReviewBar::reviewAgainRequested, this, &ReviewPage::reviewRequested);
    connect(bar_, &ReviewBar::closeRequested, this, &ReviewPage::closeRequested);
    connect(bar_, &ReviewBar::findingsRequested, findings_, [this] { findings_->setFocus(); });
    connect(findings_, &ReviewFindingsPanel::findingRequested, this, &ReviewPage::findingRequested);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout->setSpacing(marginPx);
    layout->addWidget(invitation_, 1);
    layout->addWidget(bar_);
    layout->addWidget(findings_, 1);
    // Below a bar alone, the page's background.
    layout->addStretch();
    showState();
}

QWidget *ReviewPage::createInvitation()
{
    auto *invitation = new QWidget(this);
    auto *title = new QLabel(tr("Review"), invitation);
    AppFonts::setInterfaceFont(title, 1.3, QFont::DemiBold);
    invitationText_ = new QLabel(invitation);
    invitationText_->setWordWrap(true);
    invitationText_->setAlignment(Qt::AlignCenter);
    invitationText_->setForegroundRole(QPalette::PlaceholderText);
    invitationText_->setFixedWidth(invitationWidthPx);
    start_ = new QPushButton(tr("Review the Staged Changes"), invitation);
    start_->setDefault(true);
    auto *layout = new QVBoxLayout(invitation);
    layout->addStretch(2);
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(invitationText_, 0, Qt::AlignHCenter);
    layout->addSpacing(marginPx);
    layout->addWidget(start_, 0, Qt::AlignHCenter);
    layout->addStretch(3);
    setAgentName(QString());
    return invitation;
}

void ReviewPage::setSession(ReviewSession *session)
{
    if (session_)
        session_->disconnect(this);
    session_ = session;
    bar_->setSession(session);
    findings_->setSession(session);
    if (session) {
        connect(session, &ReviewSession::stateChanged, this, &ReviewPage::showState);
        connect(session, &ReviewSession::findingsChanged, this, &ReviewPage::showState);
    }
    showState();
}

void ReviewPage::setAgentName(const QString &name)
{
    const QString reviewer = name.isEmpty() ? tr("agent") : name;
    invitationText_->setText(tr("Before the diff and the commit, a separate %1 session reads the staged changes and "
                                "lists the problems it finds. It does not see your conversation and may only read "
                                "the project.")
                                 .arg(reviewer));
    // Centered in the layout, a wrapped label gets only its one-line height.
    invitationText_->setMinimumHeight(invitationText_->heightForWidth(invitationWidthPx));
}

void ReviewPage::applyTheme(const Theme &theme)
{
    findings_->applyTheme(theme);
}

void ReviewPage::showState()
{
    invitation_->setVisible(!session_);
    // The bar hides itself without a session.
    const bool hasFindings =
        session_ && session_->state() == ReviewSession::State::Finished && !session_->findings().isEmpty();
    findings_->setVisible(hasFindings);
}
