// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingsView.h"

#include "ReviewText.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int dotIconPx = 10;
constexpr qreal dotRadiusPx = 3.5;
constexpr int barSpacingPx = 6;

using ReviewReport::Finding;

QIcon dotIcon(const QColor &color, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(dotIconPx, dotIconPx) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(dotIconPx / 2.0, dotIconPx / 2.0), dotRadiusPx, dotRadiusPx);
    return {pixmap};
}

// "1 error", "3 errors".
QString countText(qsizetype count, const QString &one, const QString &many)
{
    return count == 1 ? one : many.arg(count);
}

} // namespace

ReviewFindingsView::ReviewFindingsView(QWidget *parent)
    : QWidget(parent)
    , list_(new ReviewFindingsList(this))
{
    setFocusProxy(list_);
    connect(list_, &ReviewFindingsList::selectionChanged, this, &ReviewFindingsView::updateBar);
    connect(list_, &ReviewFindingsList::findingRequested, this, &ReviewFindingsView::findingRequested);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(barSpacingPx);
    layout->addLayout(createBar());
    layout->addWidget(list_, 1);
    updateBar();
}

QLayout *ReviewFindingsView::createBar()
{
    all_ = createFilter(ReviewFindingsList::Filter::All, QString());
    errors_ = createFilter(ReviewFindingsList::Filter::Errors, ReviewText::errorColor(palette()));
    warnings_ = createFilter(ReviewFindingsList::Filter::Warnings, ReviewText::warningColor(palette()));
    all_->setChecked(true);
    selection_ = new QLabel(this);
    selection_->setForegroundRole(QPalette::PlaceholderText);
    resolve_ = new QPushButton(this);
    resolve_->setDefault(true);
    send_ = new QPushButton(this);
    dismiss_ = new QPushButton(tr("Dismiss"), this);
    dismiss_->setToolTip(tr("Remove the selected findings from the review and the editor (Del)"));
    connect(resolve_, &QPushButton::clicked, this, [this] {
        if (session_)
            session_->resolve(targets());
    });
    connect(send_, &QPushButton::clicked, this, [this] {
        if (session_)
            session_->send(targets());
    });
    connect(dismiss_, &QPushButton::clicked, this, [this] { list_->dismiss(list_->selectedFindings()); });

    auto *bar = new QHBoxLayout;
    bar->setSpacing(barSpacingPx);
    bar->addWidget(all_);
    bar->addWidget(errors_);
    bar->addWidget(warnings_);
    bar->addStretch();
    bar->addWidget(selection_);
    bar->addWidget(resolve_);
    bar->addWidget(send_);
    bar->addWidget(dismiss_);
    return bar;
}

QToolButton *ReviewFindingsView::createFilter(ReviewFindingsList::Filter filter, const QString &dotColor)
{
    auto *button = new QToolButton(this);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    if (!dotColor.isEmpty()) {
        button->setIcon(dotIcon(QColor(dotColor), devicePixelRatioF()));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    connect(button, &QToolButton::clicked, this, [this, filter] { list_->setFilter(filter); });
    return button;
}

void ReviewFindingsView::setSession(ReviewSession *session)
{
    session_ = session;
    list_->setSession(session);
}

void ReviewFindingsView::updateBar()
{
    const QList<Finding> findings = session_ ? session_->findings() : QList<Finding>();
    const qsizetype errors = std::ranges::count_if(
        findings, [](const Finding &finding) { return finding.severity == Diagnostic::Severity::Error; });
    all_->setText(countText(findings.size(), tr("1 finding"), tr("%1 findings")));
    errors_->setText(countText(errors, tr("1 error"), tr("%1 errors")));
    warnings_->setText(countText(findings.size() - errors, tr("1 warning"), tr("%1 warnings")));

    const qsizetype selected = list_->selectedFindings().size();
    const bool hasSelection = selected > 0;
    const QString agent = session_ ? session_->agentName() : QString();
    selection_->setText(hasSelection ? tr("%1 selected").arg(selected) : QString());
    // Resolve and Send leave out the findings handed over to be fixed.
    const bool canHandOver = session_ && !session_->notHandedOver(targets()).isEmpty();
    resolve_->setText(hasSelection ? tr("Resolve") : tr("Resolve All"));
    resolve_->setToolTip(tr("Have the %1 session that wrote the code check these problems and fix those that are "
                            "real (R)")
                             .arg(agent));
    resolve_->setEnabled(canHandOver);
    send_->setText(hasSelection ? tr("Send to %1").arg(agent) : tr("Send All to %1").arg(agent));
    send_->setToolTip(tr("Type these findings in %1's prompt, for you to complete and send").arg(agent));
    send_->setEnabled(canHandOver);
    dismiss_->setVisible(hasSelection);
}

QList<Finding> ReviewFindingsView::targets() const
{
    const QList<Finding> selected = list_->selectedFindings();
    return selected.isEmpty() ? list_->shownFindings() : selected;
}
