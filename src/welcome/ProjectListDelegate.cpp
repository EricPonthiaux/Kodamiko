// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectListDelegate.h"

#include "projects/ProjectConfig.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>

#include <utility>

namespace {

constexpr qreal rowRadius = 6;
constexpr qreal rowGap = 4;      // between two rows
constexpr qreal rowPadding = 14; // above and below the names
constexpr qreal sidePadding = 18;
constexpr qreal iconSize = 20;
constexpr qreal nameGap = 16;
constexpr qreal buttonSize = 24; // the ⋯ and Locate… buttons' height
constexpr qreal whenWidth = 84;
constexpr qreal spacing = 10;

// Where the parts of a project's row go; the names (or the Locate…
// button) end at `contentRight`.
struct RowGeometry {
    QRectF background;
    QRectF icon;
    QRectF more;
    QRectF when;
    qreal contentRight = 0;
};

RowGeometry rowGeometry(const QRect &rect)
{
    const QRectF background = QRectF(rect).adjusted(0, rowGap / 2, 0, -rowGap / 2);
    const QRectF content = background.adjusted(sidePadding, 0, -sidePadding / 2, 0);
    const qreal middle = content.center().y();
    const QRectF more(content.right() - buttonSize, middle - buttonSize / 2, buttonSize, buttonSize);
    const QRectF when(more.left() - spacing - whenWidth, content.top(), whenWidth, content.height());
    return {.background = background,
            .icon = QRectF(content.left(), middle - iconSize / 2, iconSize, iconSize),
            .more = more,
            .when = when,
            .contentRight = when.left() - spacing};
}

QString locateText()
{
    return QObject::tr("Locate…");
}

QRectF locateRect(const RowGeometry &geometry, const QFontMetrics &metrics)
{
    const qreal width = metrics.horizontalAdvance(locateText()) + 2 * spacing;
    return {geometry.contentRight - width, geometry.icon.center().y() - buttonSize / 2, width, buttonSize};
}

QFont smallFont(const QFont &font)
{
    QFont small = font;
    small.setPointSizeF(font.pointSizeF() * 0.85);
    return small;
}

void paintButton(QPainter *painter, const QRectF &area, const QString &text, const Theme &theme)
{
    painter->setPen(QPen(theme.border, 1));
    painter->setBrush(theme.button);
    painter->drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), rowRadius, rowRadius);
    painter->setPen(theme.foreground);
    painter->drawText(area, Qt::AlignCenter, text);
}

struct NameColors {
    QColor name;
    QColor path;
};

void paintRowBackground(QPainter *painter,
                        const RowGeometry &geometry,
                        const QStyleOptionViewItem &option,
                        const Theme &theme)
{
    const bool isSelected = option.state & QStyle::State_Selected;
    if (!isSelected && !(option.state & QStyle::State_MouseOver))
        return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(isSelected ? ThemeStyle::mixed(theme.window, theme.accent, 0.12) : theme.hover);
    painter->drawRoundedRect(geometry.background, rowRadius, rowRadius);
    if (isSelected) {
        painter->setBrush(theme.accent);
        painter->drawRoundedRect(QRectF(geometry.background.left(), geometry.background.top() + rowPadding, 3,
                                        geometry.background.height() - 2 * rowPadding),
                                 1.5, 1.5);
    }
    const QRect icon = geometry.more.toRect().adjusted(5, 5, -5, -5);
    UiIcons::icon(QStringLiteral("more"), theme.mutedText).paint(painter, icon);
}

// The project's name, its folder below, centred together in `area`.
void paintNames(QPainter *painter,
                const QRectF &area,
                const std::pair<QString, QString> &nameAndPath,
                const QFont &font,
                const NameColors &colors)
{
    QFont bold = font;
    bold.setWeight(QFont::Medium);
    const QFont path = smallFont(font);
    const qreal lineHeight = QFontMetricsF(font).height();
    const qreal top = area.center().y() - lineHeight - 2;
    const int width = int(area.width());
    painter->setFont(bold);
    painter->setPen(colors.name);
    painter->drawText(QRectF(area.left(), top, width, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(bold).elidedText(nameAndPath.first, Qt::ElideRight, width));
    painter->setFont(path);
    painter->setPen(colors.path);
    painter->drawText(
        QRectF(area.left(), top + lineHeight + 4, width, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(path).elidedText(QDir::toNativeSeparators(nameAndPath.second), Qt::ElideMiddle, width));
}

} // namespace

ProjectListDelegate::ProjectListDelegate(Theme theme, Actions actions, QObject *parent)
    : QStyledItemDelegate(parent)
    , theme_(std::move(theme))
    , actions_(std::move(actions))
{
}

QSize ProjectListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const int lineHeight = option.fontMetrics.height();
    if (index.data(headingRole).toBool())
        return {option.rect.width(), lineHeight + 18};
    const int content = std::max(2 * lineHeight + 4, int(iconSize));
    return {option.rect.width(), content + int(2 * rowPadding + rowGap)};
}

void ProjectListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (index.data(headingRole).toBool())
        paintHeading(painter, option, index);
    else
        paintProject(painter, option, index);
    painter->restore();
}

void ProjectListDelegate::paintHeading(QPainter *painter,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    QFont font = smallFont(option.font);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);
    painter->setPen(theme_.mutedText);
    const QRectF area = QRectF(option.rect).adjusted(sidePadding, 0, -sidePadding, -6);
    painter->drawText(area, Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::DisplayRole).toString());
}

void ProjectListDelegate::paintProject(QPainter *painter,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    const RowGeometry geometry = rowGeometry(option.rect);
    paintRowBackground(painter, geometry, option, theme_);

    const auto project = index.data(projectRole).value<ProjectConfig>();
    const bool isMissing = index.data(missingRole).toBool();
    const bool isSelected = option.state & QStyle::State_Selected;
    QColor iconColor = isSelected ? theme_.accent : theme_.mutedText;
    if (isMissing)
        iconColor = theme_.warning;
    UiIcons::icon(QStringLiteral("folder"), iconColor).paint(painter, geometry.icon.toRect());

    const QFont whenFont = smallFont(option.font);
    painter->setFont(whenFont);
    painter->setPen(theme_.mutedText);
    const QString when = LastOpened::describe(project.lastOpened, QDate::currentDate());
    painter->drawText(geometry.when, Qt::AlignRight | Qt::AlignVCenter,
                      QFontMetrics(whenFont).elidedText(when, Qt::ElideRight, int(geometry.when.width())));

    qreal right = geometry.contentRight;
    if (isMissing) {
        const QRectF locate = locateRect(geometry, option.fontMetrics);
        painter->setFont(option.font);
        paintButton(painter, locate, locateText(), theme_);
        right = locate.left() - spacing;
    }

    const qreal left = geometry.icon.right() + nameGap;
    const QRectF names(left, geometry.background.top(), right - left, geometry.background.height());
    const NameColors colors{.name = isMissing ? theme_.mutedText : theme_.foreground, .path = theme_.mutedText};
    paintNames(painter, names, {index.data(Qt::DisplayRole).toString(), project.path}, option.font, colors);
}

bool ProjectListDelegate::editorEvent(QEvent *event,
                                      QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index)
{
    const bool isClick = event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick;
    if (!isClick || index.data(headingRole).toBool())
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const RowGeometry geometry = rowGeometry(option.rect);
    const bool isRelease = event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton;
    // A double click on a button does not open the project.
    if (geometry.more.contains(mouse->position())) {
        if (isRelease && actions_.showMenu)
            actions_.showMenu(index, mouse->globalPosition().toPoint());
        return true;
    }
    const bool isMissing = index.data(missingRole).toBool();
    if (isMissing && locateRect(geometry, option.fontMetrics).contains(mouse->position())) {
        if (isRelease && actions_.locate)
            actions_.locate(index);
        return true;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

namespace LastOpened {

QString describe(const QDateTime &opened, const QDate &today)
{
    if (!opened.isValid())
        return {};
    constexpr qint64 daysInAWeek = 7;
    constexpr qint64 daysInAMonth = 30;
    const qint64 days = opened.date().daysTo(today);
    if (days <= 0)
        return QObject::tr("Today");
    if (days == 1)
        return QObject::tr("Yesterday");
    if (days < daysInAWeek)
        return QObject::tr("%1 days ago").arg(days);
    if (days < 2 * daysInAWeek)
        return QObject::tr("Last week");
    if (days < daysInAMonth)
        return QObject::tr("%1 weeks ago").arg(days / daysInAWeek);
    // In English, like the rest of the interface.
    return QLocale::c().toString(opened.date(), QStringLiteral("MMM yyyy"));
}

} // namespace LastOpened
