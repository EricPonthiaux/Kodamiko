// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectListDelegate.h"

#include "projects/ProjectConfig.h"
#include "ui/AppFonts.h"
#include "ui/UiIcons.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace {

constexpr qreal rowRadius = 6;
constexpr qreal rowGap = 4; // between two rows
constexpr qreal sidePadding = 14;
constexpr qreal avatarSize = 34;
constexpr qreal avatarRadius = 8;
constexpr qreal buttonSize = 26; // the ⋯ and Locate… buttons' height
constexpr qreal whenWidth = 96;
constexpr qreal spacing = 12;
constexpr qreal chipGap = 6;
constexpr qreal chipPadding = 9;
constexpr qreal dotSize = 7;
constexpr int tintAlpha = 40; // the avatars' backgrounds, 16% of their color

// Where the parts of a project's row go; the chips (or the Locate…
// button) end at `contentRight` and go leftwards.
struct RowGeometry {
    QRectF background;
    QRectF avatar;
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
            .avatar = QRectF(content.left(), middle - avatarSize / 2, avatarSize, avatarSize),
            .more = more,
            .when = when,
            .contentRight = when.left() - 2 * spacing};
}

QString locateText()
{
    return QObject::tr("Locate…");
}

QRectF locateRect(const RowGeometry &geometry, const QFontMetrics &metrics)
{
    const qreal width = metrics.horizontalAdvance(locateText()) + 2 * spacing;
    return {geometry.contentRight - width, geometry.avatar.center().y() - buttonSize / 2, width, buttonSize};
}

QColor syntaxColor(const Theme &theme, QStringView capture)
{
    const int index = theme.syntaxStyleIndex(capture);
    return index < 0 ? theme.accent : theme.syntaxStyles[index].second.foreground;
}

// Each language family in a color of the theme's syntax, the same in every row.
QColor languageColor(const Theme &theme, const QString &family)
{
    using Capture = std::pair<QStringView, QStringView>;
    static constexpr std::array<Capture, 4> captures = {{
        {u"java", u"number"},
        {u"javascript", u"property"},
        {u"cpp", u"function"},
        {u"python", u"string"},
    }};
    const auto found = std::ranges::find(captures, QStringView(family), &Capture::first);
    return syntaxColor(theme, found == captures.end() ? QStringView(u"keyword") : found->second);
}

// A color of the theme's syntax picked by the name: a project keeps it.
QColor avatarColor(const Theme &theme, const QString &name)
{
    static constexpr std::array<QStringView, 7> captures = {
        u"function", u"keyword", u"type", u"number", u"string", u"property", u"variable.parameter"};
    return syntaxColor(theme, captures[qHash(name, 0) % captures.size()]);
}

// "angular-shop" → "AS", "Duet" → "DU".
QString initials(const QString &name)
{
    static const QRegularExpression separators(QStringLiteral("[-_.\\s]+"));
    const QStringList words = name.split(separators, Qt::SkipEmptyParts);
    if (words.size() >= 2)
        return (words[0].left(1) + words[1].left(1)).toUpper();
    return name.left(2).toUpper();
}

struct Chip {
    QString title;
    QString version;        // empty: none shown
    QColor dot;             // invalid: no dot
    bool isPending = false; // a chip for what is still to do, dashed
};

QList<Chip> chipsOf(const ProjectConfig &project, const Theme &theme)
{
    if (!project.configured)
        return {{.title = QObject::tr("Not set up"), .isPending = true}};
    QList<Chip> chips;
    for (const LanguageSetup &setup : project.languages) {
        if (!setup.enabled)
            continue;
        const bool showsVersion =
            !setup.runtimeVersion.isEmpty() && setup.runtimeVersion != QLatin1String(ProjectLanguages::systemVersion);
        chips.append({.title = ProjectLanguages::title(setup.language),
                      .version = showsVersion ? setup.runtimeVersion : QString(),
                      .dot = languageColor(theme, setup.language)});
    }
    if (chips.isEmpty())
        chips.append({.title = QObject::tr("No language server"), .isPending = true});
    return chips;
}

QFont smallFont(const QFont &font)
{
    QFont small = font;
    small.setPointSizeF(font.pointSizeF() * 0.85);
    return small;
}

void paintAvatar(QPainter *painter, const QRectF &area, const QString &text, const QColor &color)
{
    QColor tint = color;
    tint.setAlpha(tintAlpha);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tint);
    painter->drawRoundedRect(area, avatarRadius, avatarRadius);
    painter->setPen(color);
    painter->drawText(area, Qt::AlignCenter, text);
}

qreal chipWidth(const Chip &chip, const QFontMetrics &metrics)
{
    qreal width = metrics.horizontalAdvance(chip.title) + 2 * chipPadding;
    if (chip.dot.isValid())
        width += dotSize + chipGap;
    if (!chip.version.isEmpty())
        width += chipGap + metrics.horizontalAdvance(chip.version);
    return width;
}

struct ChipColors {
    QColor fill;
    QColor text;
    QColor muted;
    QColor border;
};

void paintChip(QPainter *painter, const QRectF &area, const Chip &chip, const ChipColors &colors)
{
    const qreal radius = area.height() / 2;
    if (chip.isPending) {
        painter->setPen(QPen(colors.border, 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter->setPen(colors.muted);
        painter->drawText(area, Qt::AlignCenter, chip.title);
        return;
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.fill);
    painter->drawRoundedRect(area, radius, radius);
    qreal x = area.left() + chipPadding;
    if (chip.dot.isValid()) {
        painter->setBrush(chip.dot);
        painter->drawEllipse(QRectF(x, area.center().y() - dotSize / 2, dotSize, dotSize));
        x += dotSize + chipGap;
    }
    const QFontMetricsF metrics(painter->font());
    painter->setPen(colors.text);
    painter->drawText(QRectF(x, area.top(), area.right() - x, area.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      chip.title);
    if (chip.version.isEmpty())
        return;
    x += metrics.horizontalAdvance(chip.title) + chipGap;
    painter->setPen(colors.muted);
    painter->drawText(QRectF(x, area.top(), area.right() - x, area.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      chip.version);
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

// The project's name, its folder below, centred together in `area`.
void paintNames(QPainter *painter,
                const QRectF &area,
                const std::pair<QString, QString> &nameAndPath,
                const QFont &font,
                const NameColors &colors)
{
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    const QFont path = AppFonts::codeFont(font.pointSizeF() * 0.85);
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
    const int content = std::max(2 * lineHeight + 4, int(avatarSize));
    return {option.rect.width(), content + 22 + int(rowGap)};
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
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
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
    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;
    if (isSelected || isHovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(isSelected ? theme_.selection : theme_.hover);
        painter->drawRoundedRect(geometry.background, rowRadius, rowRadius);
        // The ⋯ button shows on the row it can act on.
        const QRect icon = geometry.more.toRect().adjusted(5, 5, -5, -5);
        UiIcons::icon(QStringLiteral("more"), theme_.mutedText).paint(painter, icon);
    }

    const auto project = index.data(projectRole).value<ProjectConfig>();
    const bool isMissing = index.data(missingRole).toBool();
    QFont bold = option.font;
    bold.setWeight(QFont::DemiBold);
    painter->setFont(bold);
    if (isMissing)
        paintAvatar(painter, geometry.avatar, QStringLiteral("!"), theme_.warning);
    else
        paintAvatar(painter, geometry.avatar, initials(index.data(Qt::DisplayRole).toString()),
                    avatarColor(theme_, project.path));

    const QFont small = smallFont(option.font);
    painter->setFont(small);
    painter->setPen(theme_.mutedText);
    painter->drawText(geometry.when, Qt::AlignRight | Qt::AlignVCenter,
                      LastOpened::describe(project.lastOpened, QDate::currentDate()));

    qreal right = geometry.contentRight;
    if (isMissing) {
        const QRectF locate = locateRect(geometry, option.fontMetrics);
        painter->setFont(option.font);
        paintButton(painter, locate, locateText(), theme_);
        right = locate.left();
    } else {
        right = paintChips(painter, option, index, right);
    }

    const qreal left = geometry.avatar.right() + spacing;
    const QRectF names(left, geometry.background.top(), right - spacing - left, geometry.background.height());
    const NameColors colors{.name = isMissing ? theme_.mutedText : theme_.foreground, .path = theme_.mutedText};
    paintNames(painter, names, {index.data(Qt::DisplayRole).toString(), project.path}, option.font, colors);
}

qreal ProjectListDelegate::paintChips(QPainter *painter,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index,
                                      qreal right) const
{
    const QFont small = smallFont(option.font);
    const QFontMetrics metrics(small);
    painter->setFont(small);
    const bool isSelected = option.state & QStyle::State_Selected;
    QColor onSelection = theme_.accentText;
    onSelection.setAlpha(26);
    const ChipColors colors{.fill = isSelected ? onSelection : theme_.button,
                            .text = theme_.foreground,
                            .muted = theme_.mutedText,
                            .border = theme_.border};
    const qreal height = metrics.height() + 8;
    const qreal top = rowGeometry(option.rect).avatar.center().y() - height / 2;
    const QList<Chip> chips = chipsOf(index.data(projectRole).value<ProjectConfig>(), theme_);
    for (const Chip &chip : std::views::reverse(chips)) {
        const qreal width = chipWidth(chip, metrics);
        paintChip(painter, QRectF(right - width, top, width, height), chip, colors);
        right -= width + chipGap;
    }
    return right + chipGap;
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
