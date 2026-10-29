// SPDX-License-Identifier: GPL-3.0-only

#include "ResumeCard.h"

#include "ProjectAvatar.h"
#include "ProjectListDelegate.h"
#include "ui/AppFonts.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <utility>

namespace {

constexpr qreal radius = 8;
constexpr qreal sidePadding = 16;
constexpr qreal topPadding = 12;
constexpr qreal bottomPadding = 11;
constexpr qreal gap = 10; // between the card's three parts
constexpr qreal avatarSize = 36;
constexpr qreal arrowSize = 14;
constexpr qreal dotSize = 5;
constexpr int cardMinimumWidth = 260;
// The tints of the accent the card is drawn in, from its left to its right.
constexpr qreal fillStart = 0.13;
constexpr qreal fillEnd = 0.05;
constexpr qreal borderShare = 0.4;

QFont kickerFont(const QFont &base)
{
    QFont font = AppFonts::codeFont(base.pointSizeF() * 0.75);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    return font;
}

QFont nameFont(const QFont &base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 1.45);
    font.setWeight(QFont::Medium);
    return font;
}

QFont pathFont(const QFont &base)
{
    return AppFonts::codeFont(base.pointSizeF() * 0.8);
}

QFont detailFont(const QFont &base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 0.85);
    return font;
}

// "C++ / Python", or what is still to do.
QString languagesOf(const ProjectConfig &project)
{
    if (!project.configured)
        return QObject::tr("Not set up yet");
    QStringList titles;
    for (const LanguageSetup &setup : project.languages) {
        if (setup.enabled)
            titles.append(ProjectLanguages::title(setup.language));
    }
    return titles.isEmpty() ? QObject::tr("No language server") : titles.join(QStringLiteral(" / "));
}

} // namespace

ResumeCard::ResumeCard(Theme theme, QWidget *parent)
    : QAbstractButton(parent)
    , theme_(std::move(theme))
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ResumeCard::setProject(const ProjectConfig &project)
{
    project_ = project;
    setToolTip(QDir::toNativeSeparators(project.path));
    setAccessibleName(tr("Continue with %1").arg(project.name));
    updateGeometry();
    update();
}

QSize ResumeCard::sizeHint() const
{
    const qreal kicker = QFontMetricsF(kickerFont(font())).height();
    const qreal details = QFontMetricsF(detailFont(font())).height();
    const qreal height = topPadding + kicker + gap + avatarSize + gap + 1 + gap + details + bottomPadding;
    return {cardMinimumWidth, int(std::ceil(height))};
}

QSize ResumeCard::minimumSizeHint() const
{
    return sizeHint();
}

void ResumeCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintBackground(painter);
    const qreal identityTop = paintKicker(painter, topPadding) + gap;
    const qreal lineTop = paintIdentity(painter, identityTop) + gap;
    painter.setPen(QPen(theme_.line, 1));
    painter.drawLine(QPointF(sidePadding, lineTop + 0.5), QPointF(width() - sidePadding, lineTop + 0.5));
    paintDetails(painter, lineTop + 1 + gap);
}

void ResumeCard::paintBackground(QPainter &painter) const
{
    const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient fill(area.topLeft(), area.bottomRight());
    fill.setColorAt(0, ThemeStyle::mixed(theme_.window, theme_.accent, fillStart));
    fill.setColorAt(1, ThemeStyle::mixed(theme_.window, theme_.accent, fillEnd));
    // The border lights up to the accent under the mouse or the keyboard.
    const qreal lit = hasFocus() ? 1 : hover_.progress();
    const QColor rest = ThemeStyle::mixed(theme_.window, theme_.accent, borderShare);
    painter.setPen(QPen(ThemeStyle::mixed(rest, theme_.accent, lit), hasFocus() ? 1.5 : 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(area, radius, radius);
}

qreal ResumeCard::paintKicker(QPainter &painter, qreal top) const
{
    const QFont font = kickerFont(this->font());
    const qreal height = QFontMetricsF(font).height();
    const QRectF line(sidePadding, top, width() - 2 * sidePadding, height);
    painter.setFont(font);
    painter.setPen(theme_.accent);
    painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, tr("CONTINUE WHERE YOU LEFT OFF"));
    const QRectF arrow(line.right() - arrowSize, line.center().y() - arrowSize / 2, arrowSize, arrowSize);
    UiIcons::icon(QStringLiteral("right"), theme_.accent).paint(&painter, arrow.toRect());
    return line.bottom();
}

qreal ResumeCard::paintIdentity(QPainter &painter, qreal top) const
{
    const QRectF avatar(sidePadding, top, avatarSize, avatarSize);
    QFont initials = font();
    initials.setWeight(QFont::DemiBold);
    painter.setFont(initials);
    ProjectAvatar::paint(&painter, avatar, ProjectAvatar::initials(project_.name),
                         ProjectAvatar::color(theme_, project_.path));

    const qreal left = avatar.right() + gap + 2;
    const int textWidth = int(width() - sidePadding - left);
    const QFont name = nameFont(font());
    const QFont path = pathFont(font());
    const qreal nameHeight = QFontMetricsF(name).height();
    const qreal pathHeight = QFontMetricsF(path).height();
    const qreal textTop = avatar.center().y() - (nameHeight + pathHeight) / 2;
    painter.setFont(name);
    painter.setPen(theme_.foreground);
    painter.drawText(QRectF(left, textTop, textWidth, nameHeight), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(name).elidedText(project_.name, Qt::ElideRight, textWidth));
    painter.setFont(path);
    painter.setPen(theme_.mutedText);
    painter.drawText(
        QRectF(left, textTop + nameHeight, textWidth, pathHeight), Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(path).elidedText(QDir::toNativeSeparators(project_.path), Qt::ElideMiddle, textWidth));
    return avatar.bottom();
}

void ResumeCard::paintDetails(QPainter &painter, qreal top) const
{
    const QFont font = detailFont(this->font());
    const QRectF line(sidePadding, top, width() - 2 * sidePadding, QFontMetricsF(font).height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme_.accent);
    painter.drawEllipse(QRectF(line.left(), line.center().y() - dotSize / 2, dotSize, dotSize));
    painter.setFont(font);
    painter.setPen(theme_.mutedText);
    const QString opened = LastOpened::describe(project_.lastOpened, QDate::currentDate());
    painter.drawText(line, Qt::AlignRight | Qt::AlignVCenter, opened);
    const qreal openedWidth = QFontMetricsF(font).horizontalAdvance(opened);
    const QRectF languages = line.adjusted(dotSize + gap - 2, 0, -openedWidth - gap, 0);
    painter.drawText(languages, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(font).elidedText(languagesOf(project_), Qt::ElideRight, int(languages.width())));
}

void ResumeCard::enterEvent(QEnterEvent *event)
{
    hover_.setHovered(true);
    QAbstractButton::enterEvent(event);
}

void ResumeCard::leaveEvent(QEvent *event)
{
    hover_.setHovered(false);
    QAbstractButton::leaveEvent(event);
}
