#include "NoticeBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

namespace {

// A warning sign: a rounded triangle with "!".
QPixmap warningIcon(const QColor &color, qreal ratio)
{
    QPixmap pixmap(QSize(16, 16) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(QPolygonF({QPointF(8, 2), QPointF(14.5, 13.5), QPointF(1.5, 13.5)}));
    painter.drawLine(QPointF(8, 6.5), QPointF(8, 9.5));
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(8, 11.6), 0.9, 0.9);
    return pixmap;
}

} // namespace

NoticeBar::NoticeBar(QWidget *parent)
    : QFrame(parent)
    , icon_(new QLabel(this))
    , text_(new QLabel(this))
    , action_(new QPushButton(this))
    , close_(new QToolButton(this))
{
    setObjectName(QStringLiteral("noticeBar"));
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    action_->hide();
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setToolTip(tr("Hide"));
    close_->setText(QStringLiteral("✕"));
    connect(action_, &QPushButton::clicked, this, &NoticeBar::actionClicked);
    connect(close_, &QToolButton::clicked, this, [this] {
        hide();
        emit dismissed();
    });

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 6, 6);
    layout->setSpacing(10);
    layout->addWidget(icon_, 0, Qt::AlignTop);
    layout->addWidget(text_, 1);
    layout->addWidget(action_, 0, Qt::AlignVCenter);
    layout->addWidget(close_, 0, Qt::AlignTop);
    hide();
}

void NoticeBar::showNotice(const QString &text, const QString &action)
{
    text_->setText(text);
    action_->setText(action);
    action_->setVisible(!action.isEmpty());
    show();
}

QString NoticeBar::text() const
{
    return text_->text();
}

void NoticeBar::applyTheme(const Theme &theme)
{
    // Amber: a warning, not an error.
    const QColor amber = theme.isDark ? QColor("#e5c07b") : QColor("#b07800");
    QColor background = amber;
    background.setAlpha(theme.isDark ? 30 : 26);
    setStyleSheet(QStringLiteral("QFrame#noticeBar { background: %1; border: none; border-bottom: 1px solid %2; }"
                                 "QFrame#noticeBar QLabel { background: transparent; }")
                      .arg(QStringLiteral("rgba(%1,%2,%3,%4)")
                               .arg(background.red())
                               .arg(background.green())
                               .arg(background.blue())
                               .arg(background.alphaF()),
                           theme.line.name()));
    icon_->setPixmap(warningIcon(amber, devicePixelRatioF()));
}
