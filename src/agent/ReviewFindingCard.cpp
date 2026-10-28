// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingCard.h"

#include "ReviewExcerpt.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int stripeWidthPx = 3;
constexpr int stripeInsetPx = 8;
// The stripe's left edge, inside the card's selection frame.
constexpr int stripeLeftPx = 1;
// Between the stripe and the card's content.
constexpr int stripeGapPx = 10;
constexpr int topMarginPx = 8;
constexpr int rightMarginPx = 10;
constexpr int bottomMarginPx = 10;
// Between the header, the problem, the excerpt and the scenario.
constexpr int sectionSpacingPx = 6;
// Between the severity, the line and the sent mark.
constexpr int headerSpacingPx = 10;
constexpr int actionSpacingPx = 6;
constexpr qreal cornerRadiusPx = 6;
constexpr qreal severityScale = 0.78;
constexpr qreal severityLetterSpacingPx = 0.8;
constexpr qreal selectedAlpha = 0.16;
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

void setTextColor(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    widget->setPalette(palette);
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
    if (status == Status::Sent)
        problem->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(problem);
    auto *excerpt = new ReviewExcerpt(finding, this);
    excerpt->setVisible(!excerpt->isEmpty());
    layout->addWidget(excerpt);
    if (!finding.scenario.isEmpty()) {
        QLabel *scenario = wrappedLabel(tr("When it fails: %1").arg(finding.scenario), this);
        scenario->setForegroundRole(QPalette::PlaceholderText);
        QFont italic = scenario->font();
        italic.setItalic(true);
        scenario->setFont(italic);
        layout->addWidget(scenario);
    }
    updateActions();
}

QLayout *ReviewFindingCard::createHeader(Status status, const QString &agentName)
{
    auto *severity = new QLabel(severityText(finding_.severity).toUpper(), this);
    AppFonts::setInterfaceFont(severity, severityScale, QFont::Bold, severityLetterSpacingPx);
    setTextColor(severity, severityColor());
    auto *place = new QLabel(QStringLiteral("<a href=\"open\">:%1</a>").arg(finding_.line), this);
    place->setTextFormat(Qt::RichText);
    place->setToolTip(tr("Show line %1 (double-click the finding)").arg(finding_.line));
    connect(place, &QLabel::linkActivated, this, &ReviewFindingCard::activated);

    auto *header = new QHBoxLayout;
    header->setSpacing(headerSpacingPx);
    header->addWidget(severity);
    header->addWidget(place);
    if (status == Status::Sent) {
        auto *sent = new QLabel(tr("✓ Sent to %1").arg(agentName), this);
        sent->setForegroundRole(QPalette::PlaceholderText);
        header->addWidget(sent);
    }
    header->addStretch();

    actions_ = new QWidget(this);
    auto *actions = new QHBoxLayout(actions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(actionSpacingPx);
    if (status == Status::Open) {
        QPushButton *resolve =
            createAction(tr("Resolve"), tr("Have the %1 session that wrote the code check this problem and fix it if "
                                           "it is real (R)")
                                            .arg(agentName));
        connect(resolve, &QPushButton::clicked, this, &ReviewFindingCard::resolveRequested);
        actions->addWidget(resolve);
        QPushButton *send =
            createAction(tr("Send to %1").arg(agentName),
                         tr("Type this finding in %1's prompt, for you to complete and send").arg(agentName));
        connect(send, &QPushButton::clicked, this, &ReviewFindingCard::sendRequested);
        actions->addWidget(send);
    }
    QPushButton *dismiss = createAction(tr("Dismiss"), tr("Remove this finding from the review and the editor (Del)"));
    connect(dismiss, &QPushButton::clicked, this, &ReviewFindingCard::dismissRequested);
    actions->addWidget(dismiss);
    // The header keeps its height while the actions hide.
    QSizePolicy policy = actions_->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    actions_->setSizePolicy(policy);
    header->addWidget(actions_);
    return header;
}

QPushButton *ReviewFindingCard::createAction(const QString &text, const QString &toolTip)
{
    auto *button = new QPushButton(text, actions_);
    button->setToolTip(toolTip);
    // The list keeps the keyboard.
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void ReviewFindingCard::setSelected(bool selected)
{
    if (isSelected_ == selected)
        return;
    isSelected_ = selected;
    updateActions();
    update();
}

void ReviewFindingCard::setCurrent(bool current)
{
    if (isCurrent_ == current)
        return;
    isCurrent_ = current;
    updateActions();
    update();
}

void ReviewFindingCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(card, cornerRadiusPx, cornerRadiusPx);
    if (isSelected_)
        painter.fillPath(shape, withAlpha(palette().color(QPalette::Highlight), selectedAlpha));
    else if (isHovered_)
        painter.fillPath(shape, withAlpha(palette().color(QPalette::Text), hoveredAlpha));
    if (isCurrent_) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
        painter.drawPath(shape);
    }
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
    updateActions();
    update();
    QWidget::enterEvent(event);
}

void ReviewFindingCard::leaveEvent(QEvent *event)
{
    isHovered_ = false;
    updateActions();
    update();
    QWidget::leaveEvent(event);
}

void ReviewFindingCard::updateActions()
{
    actions_->setVisible(isHovered_ || isSelected_ || isCurrent_);
}

QColor ReviewFindingCard::severityColor() const
{
    return {finding_.severity == Diagnostic::Severity::Error ? ReviewText::errorColor(palette())
                                                             : ReviewText::warningColor(palette())};
}
