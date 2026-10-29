// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingCard.h"

#include "ReviewExcerpt.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int stripeWidthPx = 3;
constexpr int stripeInsetPx = 8;
// The stripe's left edge, inside the card's selection frame.
constexpr int stripeLeftPx = 1;
// Between the stripe and the card's content.
constexpr int stripeGapPx = 10;
constexpr int topMarginPx = 16;
constexpr int rightMarginPx = 18;
constexpr int bottomMarginPx = 16;
// Between the header, the problem, the excerpt and the scenario.
constexpr int sectionSpacingPx = 12;
// Between the severity, the line and the sent mark.
constexpr int headerSpacingPx = 10;
constexpr int actionSpacingPx = 6;
constexpr qreal cornerRadiusPx = 6;
constexpr qreal severityScale = 0.78;
constexpr qreal severityLetterSpacingPx = 0.8;
constexpr qreal selectedAlpha = 0.08;
constexpr qreal hoveredAlpha = 0.05;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

QString severityText(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Error:
        return ReviewFindingCard::tr("Error");
    case Diagnostic::Severity::Warning:
        return ReviewFindingCard::tr("Warning");
    }
    return {};
}

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

} // namespace

ReviewFindingCard::ReviewFindingCard(const ReviewReport::Finding &finding,
                                     Status status,
                                     const QString &agentName,
                                     QWidget *parent)
    : QWidget(parent)
    , finding_(finding)
{
    setAttribute(Qt::WA_Hover);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(stripeLeftPx + stripeWidthPx + stripeGapPx, topMarginPx, rightMarginPx, bottomMarginPx);
    layout->setSpacing(sectionSpacingPx);
    layout->addLayout(createHeader(status, agentName));
    QLabel *problem = wrappedLabel(finding.problem, this);
    AppFonts::setInterfaceFont(problem, 1.05, QFont::DemiBold);
    if (status == Status::Sent)
        problem->setObjectName(QStringLiteral("reviewMuted"));
    layout->addWidget(problem);
    auto *excerpt = new ReviewExcerpt(finding, this);
    excerpt->setVisible(!excerpt->isEmpty());
    layout->addWidget(excerpt);
    if (!finding.scenario.isEmpty()) {
        scenario_ = wrappedLabel(tr("When it happens\n%1").arg(finding.scenario), this);
        scenario_->setObjectName(QStringLiteral("reviewMuted"));
        scenario_->hide();
        layout->addWidget(scenario_);
        details_ = new QToolButton(this);
        details_->setText(tr("See scenario"));
        details_->setCheckable(true);
        details_->setArrowType(Qt::RightArrow);
        details_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(details_, &QToolButton::toggled, this, [this](bool expanded) {
            setExpanded(expanded);
            emit detailsToggled(expanded);
        });
        layout->addWidget(details_, 0, Qt::AlignLeft);
    }
    layout->addWidget(createActions(status, agentName));
}

QLayout *ReviewFindingCard::createHeader(Status status, const QString &agentName)
{
    auto *severity = new QLabel(severityText(finding_.severity).toUpper(), this);
    AppFonts::setInterfaceFont(severity, severityScale, QFont::Bold, severityLetterSpacingPx);
    severity->setObjectName(finding_.severity == Diagnostic::Severity::Error ? QStringLiteral("reviewError")
                                                                             : QStringLiteral("reviewWarning"));
    selection_ = new QCheckBox(this);
    selection_->setAccessibleName(tr("Select finding: %1").arg(finding_.problem));
    connect(selection_, &QCheckBox::clicked, this, [this] { emit pressed(Qt::ControlModifier); });
    auto *place = new QToolButton(this);
    place->setText(tr("Line %1").arg(finding_.line));
    place->setObjectName(QStringLiteral("reviewLine"));
    place->setToolTip(tr("Show line %1 (double-click the finding)").arg(finding_.line));
    connect(place, &QToolButton::clicked, this, &ReviewFindingCard::activated);

    auto *header = new QHBoxLayout;
    header->setSpacing(headerSpacingPx);
    header->addWidget(selection_);
    header->addWidget(severity);
    header->addWidget(place);
    if (status == Status::Sent) {
        auto *sent = new QLabel(tr("Handed to %1").arg(agentName), this);
        sent->setObjectName(QStringLiteral("reviewMuted"));
        sent->setToolTip(tr("The finding has been handed over for a follow-up. It is not marked as fixed."));
        header->addStretch();
        header->addWidget(sent);
    }
    header->addStretch();
    return header;
}

QWidget *ReviewFindingCard::createActions(Status status, const QString &agentName)
{
    actions_ = new QWidget(this);
    auto *actions = new QHBoxLayout(actions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(actionSpacingPx);
    actions->addStretch();
    QPushButton *dismiss =
        createAction(tr("Dismiss"), tr("Remove this finding from the review and the editor (Del). You can undo this."));
    connect(dismiss, &QPushButton::clicked, this, &ReviewFindingCard::dismissRequested);
    actions->addWidget(dismiss);
    if (status == Status::Open) {
        QPushButton *send = createAction(
            tr("Prepare prompt"), tr("Type this finding in %1's prompt, for you to complete and send").arg(agentName));
        connect(send, &QPushButton::clicked, this, &ReviewFindingCard::sendRequested);
        actions->addWidget(send);
        QPushButton *resolve =
            createAction(tr("Ask %1 to fix").arg(agentName),
                         tr("Have the %1 session that wrote the code check this problem and fix it if "
                            "it is real (R)")
                             .arg(agentName));
        resolve->setObjectName(QStringLiteral("reviewPrimary"));
        connect(resolve, &QPushButton::clicked, this, &ReviewFindingCard::resolveRequested);
        actions->addWidget(resolve);
    }
    return actions_;
}

QPushButton *ReviewFindingCard::createAction(const QString &text, const QString &toolTip)
{
    auto *button = new QPushButton(text, actions_);
    button->setToolTip(toolTip);
    return button;
}

void ReviewFindingCard::setSelected(bool selected)
{
    if (isSelected_ == selected)
        return;
    isSelected_ = selected;
    const QSignalBlocker block(selection_);
    selection_->setChecked(selected);
    update();
}

void ReviewFindingCard::setCurrent(bool current)
{
    if (isCurrent_ == current)
        return;
    isCurrent_ = current;
    update();
}

void ReviewFindingCard::setExpanded(bool expanded)
{
    if (!scenario_)
        return;
    const QSignalBlocker block(details_);
    details_->setChecked(expanded);
    details_->setText(expanded ? tr("Hide scenario") : tr("See scenario"));
    details_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    scenario_->setVisible(expanded);
}

void ReviewFindingCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(card, cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(shape, palette().color(QPalette::AlternateBase));
    if (isSelected_)
        painter.fillPath(shape, withAlpha(palette().color(QPalette::Highlight), selectedAlpha));
    else if (isHovered_)
        painter.fillPath(shape, withAlpha(palette().color(QPalette::Text), hoveredAlpha));
    painter.setPen(QPen(palette().color(isCurrent_ || isSelected_ ? QPalette::Accent : QPalette::Mid), 1));
    painter.drawPath(shape);
    painter.setPen(Qt::NoPen);
    painter.setBrush(severityColor());
    const QRectF stripe(stripeLeftPx, stripeInsetPx, stripeWidthPx, height() - 2 * stripeInsetPx);
    painter.drawRoundedRect(stripe, stripeWidthPx / 2.0, stripeWidthPx / 2.0);
}

void ReviewFindingCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit pressed(event->modifiers());
    QWidget::mousePressEvent(event);
}

void ReviewFindingCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated();
}

void ReviewFindingCard::enterEvent(QEnterEvent *event)
{
    isHovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void ReviewFindingCard::leaveEvent(QEvent *event)
{
    isHovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

QColor ReviewFindingCard::severityColor() const
{
    return {finding_.severity == Diagnostic::Severity::Error ? ReviewText::errorColor(palette())
                                                             : ReviewText::warningColor(palette())};
}
