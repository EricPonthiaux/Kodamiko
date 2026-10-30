// SPDX-License-Identifier: GPL-3.0-only

#include "PopupWidgets.h"

#include "AppFonts.h"
#include "Paragraph.h"
#include "PopupMetrics.h"
#include "ThemeStyle.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share, a.alphaF() * (1 - share) + b.alphaF() * share);
}

// A click on a settings row presses its switch.
class RowClick : public QObject {
public:
    RowClick(QAbstractButton *button, QObject *parent)
        : QObject(parent)
        , button_(button)
    {
    }

    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && button_->isEnabled()) {
            button_->click();
            return true;
        }
        return false;
    }

private:
    QAbstractButton *button_;
};

void drawWindowDots(QPainter &painter, const QRectF &bar)
{
    const std::array<QColor, 3> dots = {QColor("#ff5f57"), QColor("#febc2e"), QColor("#28c840")};
    for (std::size_t i = 0; i < dots.size(); ++i) {
        painter.setBrush(dots[i]);
        painter.drawEllipse(QPointF(bar.left() + 10 + qreal(i) * 9, bar.center().y()), 2.6, 2.6);
    }
}

QColor syntaxColor(const Theme &theme, const char *category)
{
    const int index = theme.syntaxStyleIndex(QString::fromLatin1(category));
    return index >= 0 ? theme.syntaxStyles[index].second.foreground : theme.foreground;
}

// A run of code of one category in a theme's miniature.
struct CodeRun {
    const char *category;
    qreal width;
};

// Lines of code as bars of the theme's syntax colors, right of `gutter`.
void drawCodeLines(QPainter &painter, const Theme &theme, const QRectF &preview, const QRectF &gutter)
{
    // Each line: indentation, then (category, width) runs.
    const QList<std::pair<int, QList<CodeRun>>> lines = {
        {0, {{"comment", 70}}},
        {0, {{"keyword", 26}, {"type", 22}, {"function", 34}, {"punctuation", 8}}},
        {1, {{"keyword", 18}, {"variable", 20}, {"operator", 6}, {"string", 44}}},
        {1, {{"function", 30}, {"punctuation", 6}, {"number", 14}, {"punctuation", 6}}},
        {1, {{"keyword", 30}, {"variable", 24}}},
        {0, {{"punctuation", 8}}},
    };
    qreal y = gutter.top() + 9;
    for (const auto &[indent, runs] : lines) {
        // The current line, as in the editor.
        if (&runs == &lines[2].second)
            painter.fillRect(QRectF(gutter.right(), y - 3.5, preview.width() - gutter.width(), 10), theme.caretLine);
        painter.setBrush(theme.marginForeground);
        painter.drawRoundedRect(QRectF(gutter.left() + 6, y, 6, 3), 1.5, 1.5);
        qreal x = gutter.right() + 8 + indent * 12;
        for (const CodeRun &run : runs) {
            const qreal width = std::min(run.width, preview.right() - 8 - x);
            if (width <= 0)
                break;
            painter.setBrush(syntaxColor(theme, run.category));
            painter.drawRoundedRect(QRectF(x, y, width, 3), 1.5, 1.5);
            x += width + 5;
        }
        y += 12;
    }
}

void drawCheckMark(QPainter &painter, const QPointF &center, const QColor &accent, const QColor &mark)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(center, 8, 8);
    painter.setPen(QPen(mark, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(
        QPolygonF({center + QPointF(-3.5, 0), center + QPointF(-1, 2.6), center + QPointF(3.6, -2.4)}));
}

struct KeycapColors {
    QColor fill;
    QColor border;
    QColor text;
    QColor muted;
};

// The palette's keycaps, on a selected row too: the selection is a tint
// (ThemeStyle::selectionColor), not a strong color to stand out from.
KeycapColors keycapColors(const QPalette &palette)
{
    return {palette.color(QPalette::Button), palette.color(QPalette::Mid), palette.color(QPalette::Text),
            palette.color(QPalette::PlaceholderText)};
}

void drawKeycap(QPainter &painter, const QRectF &cap, const QString &key, const KeycapColors &colors)
{
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.fill);
    painter.drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    // The key's lower edge, slightly thicker.
    painter.setPen(QPen(colors.border, 1.5));
    painter.drawLine(QPointF(cap.left() + 3, cap.bottom() - 0.5), QPointF(cap.right() - 3, cap.bottom() - 0.5));
    painter.setPen(colors.text);
    painter.drawText(cap, Qt::AlignCenter, key);
}

} // namespace

QString popupStyleSheet(const Theme &theme, const QString &extra)
{
    QString sheet = QStringLiteral(R"(
QLabel#pageDescription, QLabel#settingsDescription, QLabel#footerHint { color: @muted; }
QFrame#popupHeader { border: none; border-bottom: 1px solid @line; }
QLabel#settingsSection { border: none; border-bottom: 1px solid @line; padding-bottom: 4px; }
QFrame#settingsCard { background: transparent; border: none; border-radius: 0; }
QFrame#settingsSeparator { background: @line; border: none; margin: 0; }
QScrollArea#pageScroll, QWidget#pageContent { background: transparent; border: none; }
QFrame#popupFooter { border: none; border-top: 1px solid @line; }
QFrame#popupSidebar { background: @window; border: none; border-right: 1px solid @line; }
QListWidget, QTreeWidget { background: transparent; border: none; outline: 0; }
QListWidget::item, QTreeWidget::item { padding: 3px @rowPaddingpx; border-radius: @rowRadiuspx; color: @text; }
QListWidget::item:hover, QTreeWidget::item:hover { background: @hover; }
QListWidget::item:selected, QTreeWidget::item:selected { background: @slate; color: @text; }
QListWidget#popupNav::item { margin: 1px 0; }
QLabel#navHeading, QLabel#settingState { color: @muted; }
QLabel#popupError, QLabel[error="true"] { color: @error; }
QLabel#popupWarning { color: @warning; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)") + extra;
    sheet.replace(QStringLiteral("@rowPadding"), QString::number(PopupMetrics::rowPadding));
    sheet.replace(QStringLiteral("@rowRadius"), QString::number(PopupMetrics::rowRadius));
    const QList<std::pair<QString, QColor>> colors = {
        {"@accentText", theme.accentText},
        {"@accent", theme.accent},
        {"@text", theme.foreground},
        {"@muted", theme.mutedText},
        {"@window", theme.window},
        {"@panel", theme.alternateBase},
        {"@base", theme.background},
        {"@line", theme.line},
        {"@hover", theme.hover},
        {"@selection", theme.selection},
        {"@slate", ThemeStyle::selectionColor(theme)},
        {"@error", theme.error},
        {"@warning", theme.warning},
    };
    for (const auto &[name, color] : colors)
        sheet.replace(name, color.name());
    return sheet;
}

// ThemeCard

ThemeCard::ThemeCard(const Theme &theme, QWidget *parent)
    : QAbstractButton(parent)
    , theme_(theme)
{
    setCheckable(true);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(theme.title);
    // Narrower in a narrow popup: the four themes stay side by side.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize ThemeCard::sizeHint() const
{
    return {176, 132};
}

QSize ThemeCard::minimumSizeHint() const
{
    return {120, 132};
}

void ThemeCard::enterEvent(QEnterEvent *event)
{
    hover_.setHovered(true);
    QAbstractButton::enterEvent(event);
}

void ThemeCard::leaveEvent(QEvent *event)
{
    hover_.setHovered(false);
    QAbstractButton::leaveEvent(event);
}

void ThemeCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &colors = palette();
    const QColor accent = colors.color(QPalette::Accent);
    const QRectF card = QRectF(rect()).adjusted(2, 2, -2, -2);
    constexpr qreal radius = 4;
    constexpr qreal labelHeight = 30;

    // The miniature: a title bar, a gutter, lines of code.
    const QRectF preview(card.left(), card.top(), card.width(), card.height() - labelHeight);
    QPainterPath clip;
    clip.addRoundedRect(card, radius, radius);
    painter.save();
    painter.setClipPath(clip);
    painter.fillRect(preview, theme_.background);
    const QRectF bar(preview.left(), preview.top(), preview.width(), 16);
    painter.fillRect(bar, theme_.window);
    painter.setPen(Qt::NoPen);
    drawWindowDots(painter, bar);
    const QRectF gutter(preview.left(), bar.bottom(), 18, preview.height() - bar.height());
    painter.fillRect(gutter, theme_.marginBackground);
    drawCodeLines(painter, theme_, preview, gutter);
    painter.restore();

    // The name, and a check mark when chosen.
    const QRectF label(card.left() + 12, preview.bottom(), card.width() - 24, labelHeight);
    QFont font = this->font();
    if (isChecked())
        font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(colors.color(QPalette::WindowText));
    painter.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, theme_.title);
    if (isChecked())
        drawCheckMark(painter, QPointF(label.right() - 7, label.center().y()), accent, ThemeStyle::applied().onAccent);

    // The border: the accent when chosen or under the mouse.
    QColor border = colors.color(QPalette::Mid);
    qreal borderWidth = 1;
    if (isChecked()) {
        border = accent;
        borderWidth = 2;
    } else {
        const qreal highlight = hasFocus() ? 1.0 : hover_.progress();
        border = mix(border, accent, 0.6 * highlight);
    }
    painter.setPen(QPen(border, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card, radius, radius);
    painter.setPen(QPen(colors.color(QPalette::Mid), 1));
    painter.drawLine(QPointF(card.left() + borderWidth, preview.bottom()),
                     QPointF(card.right() - borderWidth, preview.bottom()));
}

// SettingsCard

SettingsCard::SettingsCard(QWidget *parent, TogglePlacement toggles)
    : QFrame(parent)
    , layout_(new QVBoxLayout(this))
    , toggles_(toggles)
{
    setObjectName(QStringLiteral("settingsCard"));
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

void SettingsCard::addSeparator()
{
    if (layout_->isEmpty())
        return;
    auto *line = new QFrame(this);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFixedHeight(1);
    layout_->addWidget(line);
}

QWidget *SettingsCard::addRow(const QString &title, const QString &description, QWidget *control)
{
    addSeparator();
    auto *row = new QWidget(this);
    auto *titleLabel = new QLabel(title, row);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(titleLabel);
    if (!description.isEmpty()) {
        auto *descriptionLabel = new QLabel(row);
        descriptionLabel->setObjectName(QStringLiteral("settingsDescription"));
        Paragraph::setText(descriptionLabel, description);
        text->addWidget(descriptionLabel);
    }
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    // A check box is a classic one: before its title, aligned on its first line.
    if (toggles_ == TogglePlacement::BeforeTitle && qobject_cast<QCheckBox *>(control)) {
        // Centered on the title's line.
        auto *box = new QVBoxLayout;
        box->addSpacing(std::max(0, (titleLabel->sizeHint().height() - control->sizeHint().height()) / 2));
        box->addWidget(control);
        box->addStretch();
        layout->setSpacing(10);
        layout->addLayout(box);
        layout->addLayout(text, 1);
    } else {
        layout->setSpacing(24);
        layout->addLayout(text, 1);
        layout->addWidget(control, 0, Qt::AlignVCenter);
    }
    // A click on the text toggles a switch, like a check box's label.
    if (auto *button = qobject_cast<QAbstractButton *>(control)) {
        button->setAccessibleName(title);
        titleLabel->setBuddy(button);
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(new RowClick(button, row));
    }
    layout_->addWidget(row);
    return row;
}

void SettingsCard::addWidget(QWidget *widget)
{
    addSeparator();
    auto *holder = new QWidget(this);
    auto *layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->addWidget(widget);
    layout_->addWidget(holder);
}

// PopupHeader

PopupHeader::PopupHeader(QString iconName, const QString &title, QWidget *parent, Style style)
    : QFrame(parent)
    , iconName_(std::move(iconName))
    , icon_(new QLabel(this))
    , title_(new QLabel(title, this))
    , subtitle_(new QLabel(this))
    , buttons_(new QHBoxLayout)
    , close_(new QToolButton(this))
{
    const bool compact = style == Style::Compact;
    setObjectName(QStringLiteral("popupHeader"));
    AppFonts::setInterfaceFont(title_, compact ? 0.85 : 1.1, compact ? QFont::Medium : QFont::DemiBold);
    subtitle_->setObjectName(QStringLiteral("pageDescription"));
    subtitle_->hide();
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setToolTip(tr("Close (Esc)"));
    if (compact) {
        setFixedHeight(UiMetrics::compactTitleBarHeight);
        close_->setFixedSize(UiMetrics::compactCloseButtonSize);
    } else {
        UiMetrics::setScaledSize(close_, QSize(26, 26));
    }
    connect(close_, &QToolButton::clicked, this, &PopupHeader::closeRequested);
    buttons_->setSpacing(6);

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(compact ? UiMetrics::compactTitleBarMargins : QMargins(16, 10, 10, 10));
    layout->setHorizontalSpacing(compact ? UiMetrics::compactTitleBarSpacing : 8);
    layout->setVerticalSpacing(compact ? 0 : 2);
    layout->addWidget(icon_, 0, 0, Qt::AlignVCenter);
    layout->addWidget(title_, 0, 1, Qt::AlignVCenter);
    layout->addLayout(buttons_, 0, 2, Qt::AlignVCenter);
    layout->addWidget(close_, 0, 3, Qt::AlignVCenter);
    layout->addWidget(subtitle_, 1, 1, 1, 2);
    layout->setColumnStretch(1, 1);
}

void PopupHeader::setTitle(const QString &title)
{
    title_->setText(title);
}

void PopupHeader::setSubtitle(const QString &text)
{
    Paragraph::setText(subtitle_, text);
    subtitle_->setVisible(!text.isEmpty());
}

void PopupHeader::addButton(QWidget *button)
{
    buttons_->addWidget(button);
}

void PopupHeader::applyTheme(const Theme &theme)
{
    icon_->setPixmap(UiIcons::icon(iconName_, theme.accent)
                         .pixmap(UiMetrics::scaledSquare(UiMetrics::iconSize), devicePixelRatioF()));
    close_->setIcon(UiIcons::icon(QStringLiteral("close"), theme.mutedText));
}

QWidget *sectionHeading(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("settingsSection"));
    AppFonts::setInterfaceFont(label, 1, QFont::DemiBold);
    // Its underline would otherwise indent the text by half an "x".
    label->setIndent(0);
    return label;
}

// KeycapDelegate

QList<QStringList> KeycapDelegate::keysOf(const QKeySequence &shortcut)
{
    QList<QStringList> chords;
    for (int i = 0; i < shortcut.count(); ++i) {
        const QKeyCombination combination = shortcut[i];
        const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
        QStringList keys;
#ifdef Q_OS_MACOS
        if (modifiers & Qt::MetaModifier)
            keys.append(QStringLiteral("⌃"));
        if (modifiers & Qt::AltModifier)
            keys.append(QStringLiteral("⌥"));
        if (modifiers & Qt::ShiftModifier)
            keys.append(QStringLiteral("⇧"));
        if (modifiers & Qt::ControlModifier)
            keys.append(QStringLiteral("⌘"));
#else
        if (modifiers & Qt::ControlModifier)
            keys.append(QStringLiteral("Ctrl"));
        if (modifiers & Qt::AltModifier)
            keys.append(QStringLiteral("Alt"));
        if (modifiers & Qt::ShiftModifier)
            keys.append(QStringLiteral("Shift"));
        if (modifiers & Qt::MetaModifier)
            keys.append(QStringLiteral("Meta"));
#endif
        keys.append(QKeySequence(combination.key()).toString(QKeySequence::NativeText));
        chords.append(keys);
    }
    return chords;
}

QSize KeycapDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), option.fontMetrics.height() + 14));
    return size;
}

void KeycapDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    item.text.clear();
    const QWidget *widget = item.widget;
    widget->style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);

    const QList<QKeySequence> shortcuts =
        QKeySequence::listFromString(index.data(shortcutsRole).toString(), QKeySequence::PortableText);
    QFont font = option.font;
    font.setPointSizeF(font.pointSizeF() * 0.86);
    const QFontMetricsF metrics(font);
    const KeycapColors colors = keycapColors(option.palette);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(option.rect);
    painter->setFont(font);
    const qreal height = metrics.height() + 6;
    const qreal top = option.rect.top() + (option.rect.height() - height) / 2;
    qreal x = option.rect.left() + 6;
    const auto drawWord = [&](const QString &word, const QColor &color) {
        painter->setPen(color);
        const qreal width = metrics.horizontalAdvance(word);
        painter->drawText(QRectF(x, top, width, height), Qt::AlignCenter, word);
        x += width + 6;
    };
    bool first = true;
    for (const QKeySequence &shortcut : shortcuts) {
        if (shortcut.isEmpty())
            continue;
        if (!first)
            drawWord(QStringLiteral("or"), colors.muted);
        first = false;
        const QList<QStringList> chords = keysOf(shortcut);
        for (int c = 0; c < chords.size(); ++c) {
            if (c > 0)
                drawWord(QStringLiteral("then"), colors.muted);
            for (const QString &key : chords[c]) {
                const qreal width = std::max(metrics.horizontalAdvance(key) + 12, height);
                drawKeycap(*painter, QRectF(x, top, width, height), key, colors);
                x += width + 3;
            }
            x += 3;
        }
    }
    painter->restore();
}
