// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewPage.h"

#include "ReviewView.h"
#include "ui/AppFonts.h"
#include "ui/ThemeStyle.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int marginPx = 8;
constexpr int invitationWidthPx = 520;

} // namespace

ReviewPage::ReviewPage(QWidget *parent)
    : QWidget(parent)
{
    invitation_ = createInvitation();
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    connect(start_, &QPushButton::clicked, this, &ReviewPage::reviewRequested);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout->setSpacing(marginPx);
    layout->addWidget(invitation_, 1);
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
    if (session_ == session)
        return;
    if (view_) {
        view_->hide();
        layout()->removeWidget(view_);
        view_->disconnect(this);
        view_->deleteLater();
        view_ = nullptr;
    }
    session_ = session;
    invitation_->setVisible(!session);
    if (session) {
        view_ = new ReviewView(session, this);
        layout()->addWidget(view_);
        connect(view_, &ReviewView::reviewRequested, this, &ReviewPage::reviewRequested);
        connect(view_, &ReviewView::closeRequested, this, &ReviewPage::closeRequested);
        connect(view_, &ReviewView::inspectRequested, this, &ReviewPage::inspectRequested);
        connect(view_, &ReviewView::findingRequested, this, &ReviewPage::findingRequested);
    }
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
    setPalette(ThemeStyle::palette(theme));
}
