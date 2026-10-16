#include "PopupWidgets.h"

#include "UiIcons.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share, a.alphaF() * (1 - share) + b.alphaF() * share);
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

// A click on a settings row presses its switch.
class RowClick : public QObject {
public:
    RowClick(QAbstractButton *button, QObject *parent)
        : QObject(parent)
        , button_(button)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && button_->isEnabled()) {
            button_->click();
            return true;
        }
        return false;
    }

private:
    QAbstractButton *button_;
};

} // namespace

QString popupStyleSheet(const Theme &theme, const QString &extra)
{
    QString sheet = QStringLiteral(R"(
QLabel#pageDescription, QLabel#settingsDescription, QLabel#settingsSection, QLabel#footerHint { color: @muted; }
QFrame#settingsCard { background: @base; border: 1px solid @line; border-radius: 10px; }
QFrame#settingsSeparator { background: @line; border: none; margin: 0 16px; }
QScrollArea#pageScroll, QWidget#pageContent { background: transparent; border: none; }
QFrame#popupFooter { border: none; border-top: 1px solid @line; }
)") + extra;
    const QList<std::pair<QString, QColor>> colors = {
        {"@accentText", theme.accentText}, {"@accent", theme.accent},       {"@text", theme.foreground},
        {"@muted", theme.mutedText},       {"@window", theme.window},       {"@panel", theme.alternateBase},
        {"@base", theme.background},       {"@line", theme.line},           {"@hover", theme.hover},
        {"@selection", theme.selection},
    };
    for (const auto &[name, color] : colors)
        sheet.replace(name, color.name());
    return sheet;
}

// ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
    , animation_(new QVariantAnimation(this))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    animation_->setDuration(140);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        position_ = value.toReal();
        update();
    });
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        animation_->stop();
        if (!isVisible()) { // filled before showing: no animation
            position_ = checked ? 1 : 0;
            update();
            return;
        }
        animation_->setStartValue(position_);
        animation_->setEndValue(checked ? 1.0 : 0.0);
        animation_->start();
    });
}

QSize ToggleSwitch::sizeHint() const
{
    return {38, 22};
}

bool ToggleSwitch::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}

void ToggleSwitch::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &colors = palette();
    const QRectF track = QRectF(rect()).adjusted(1, 2, -1, -2);
    const qreal radius = track.height() / 2;

    QColor off = colors.color(QPalette::Mid);
    if (hovered_)
        off = off.lighter(isDark(colors) ? 125 : 95);
    QColor on = colors.color(QPalette::Accent);
    if (hovered_)
        on = on.lighter(110);
    QColor fill = mix(off, on, position_);
    if (!isEnabled())
        fill.setAlphaF(0.4);

    if (hasFocus()) { // keyboard focus: a ring
        painter.setPen(QPen(colors.color(QPalette::Accent), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-1.5, -1.5, 1.5, 1.5), radius + 1.5, radius + 1.5);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(track, radius, radius);

    const qreal knob = track.height() - 4;
    const qreal x = track.left() + 2 + position_ * (track.width() - knob - 4);
    const QRectF knobRect(x, track.top() + 2, knob, knob);
    painter.setBrush(QColor(0, 0, 0, 40)); // a hint of shadow
    painter.drawEllipse(knobRect.translated(0, 0.8));
    painter.setBrush(Qt::white);
    painter.drawEllipse(knobRect);
}

// ThemeCard

ThemeCard::ThemeCard(const Theme &theme, QWidget *parent)
    : QAbstractButton(parent)
    , theme_(theme)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(theme.title);
}

QSize ThemeCard::sizeHint() const
{
    return {176, 132};
}

void ThemeCard::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ThemeCard::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ThemeCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &colors = palette();
    const QColor accent = colors.color(QPalette::Accent);
    const QRectF card = QRectF(rect()).adjusted(2, 2, -2, -2);
    constexpr qreal radius = 10;
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
    const QColor dots[] = {QColor("#ff5f57"), QColor("#febc2e"), QColor("#28c840")};
    for (int i = 0; i < 3; ++i) {
        painter.setBrush(dots[i]);
        painter.drawEllipse(QPointF(bar.left() + 10 + i * 9, bar.center().y()), 2.6, 2.6);
    }
    const QRectF gutter(preview.left(), bar.bottom(), 18, preview.height() - bar.height());
    painter.fillRect(gutter, theme_.marginBackground);

    const auto color = [this](const char *category) {
        const int index = theme_.syntaxStyleIndex(QString::fromLatin1(category));
        return index >= 0 ? theme_.syntaxStyles[index].second.foreground : theme_.foreground;
    };
    // Each line: indentation, then (category, width) runs.
    struct Run {
        const char *category;
        qreal width;
    };
    const QList<std::pair<int, QList<Run>>> lines = {
        {0, {{"comment", 70}}},
        {0, {{"keyword", 26}, {"type", 22}, {"function", 34}, {"punctuation", 8}}},
        {1, {{"keyword", 18}, {"variable", 20}, {"operator", 6}, {"string", 44}}},
        {1, {{"function", 30}, {"punctuation", 6}, {"number", 14}, {"punctuation", 6}}},
        {1, {{"keyword", 30}, {"variable", 24}}},
        {0, {{"punctuation", 8}}},
    };
    qreal y = bar.bottom() + 9;
    for (const auto &[indent, runs] : lines) {
        // The current line, as in the editor.
        if (&runs == &lines[2].second)
            painter.fillRect(QRectF(gutter.right(), y - 3.5, preview.width() - gutter.width(), 10), theme_.caretLine);
        painter.setBrush(theme_.marginForeground);
        painter.drawRoundedRect(QRectF(gutter.left() + 6, y, 6, 3), 1.5, 1.5);
        qreal x = gutter.right() + 8 + indent * 12;
        for (const Run &run : runs) {
            const qreal width = std::min(run.width, preview.right() - 8 - x);
            if (width <= 0)
                break;
            painter.setBrush(color(run.category));
            painter.drawRoundedRect(QRectF(x, y, width, 3), 1.5, 1.5);
            x += width + 5;
        }
        y += 12;
    }
    painter.restore();

    // The name, and a check mark when chosen.
    const QRectF label(card.left() + 12, preview.bottom(), card.width() - 24, labelHeight);
    QFont font = this->font();
    if (isChecked())
        font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(colors.color(QPalette::WindowText));
    painter.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, theme_.title);
    if (isChecked()) {
        const QPointF center(label.right() - 7, label.center().y());
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(center, 8, 8);
        painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF({center + QPointF(-3.5, 0), center + QPointF(-1, 2.6), center + QPointF(3.6, -2.4)}));
    }

    // The border: the accent when chosen or under the mouse.
    QColor border = colors.color(QPalette::Mid);
    qreal borderWidth = 1;
    if (isChecked()) {
        border = accent;
        borderWidth = 2;
    } else if (hovered_ || hasFocus()) {
        border = mix(border, accent, 0.6);
    }
    painter.setPen(QPen(border, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card, radius, radius);
    painter.setPen(QPen(colors.color(QPalette::Mid), 1));
    painter.drawLine(QPointF(card.left() + borderWidth, preview.bottom()),
                     QPointF(card.right() - borderWidth, preview.bottom()));
}

// SettingsCard

SettingsCard::SettingsCard(QWidget *parent)
    : QFrame(parent)
    , layout_(new QVBoxLayout(this))
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
        auto *descriptionLabel = new QLabel(description, row);
        descriptionLabel->setObjectName(QStringLiteral("settingsDescription"));
        descriptionLabel->setWordWrap(true);
        text->addWidget(descriptionLabel);
    }
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 11, 16, 11);
    layout->setSpacing(24);
    layout->addLayout(text, 1);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    // A click on the text toggles a switch, like a check box's label.
    if (auto *button = qobject_cast<QAbstractButton *>(control)) {
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
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addWidget(widget);
    layout_->addWidget(holder);
}

QPixmap iconTile(const QString &icon, const Theme &theme, int size, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor tint = theme.accent;
    tint.setAlpha(theme.isDark ? 46 : 30);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tint);
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.27, size * 0.27);
    // The icons are drawn at 16 pixels: scaled up to half the tile.
    const qreal margin = size / 4.0;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(QRectF(margin, margin, size - 2 * margin, size - 2 * margin),
                       UiIcons::icon(icon, theme.accent).pixmap(QSize(16, 16), 2.0), QRectF());
    return pixmap;
}

QWidget *sectionHeading(const QString &text)
{
    auto *label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("settingsSection"));
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.82);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.9);
    label->setFont(font);
    label->setContentsMargins(4, 0, 0, 0);
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
        if (modifiers & Qt::MetaModifier) keys.append(QStringLiteral("⌃"));
        if (modifiers & Qt::AltModifier) keys.append(QStringLiteral("⌥"));
        if (modifiers & Qt::ShiftModifier) keys.append(QStringLiteral("⇧"));
        if (modifiers & Qt::ControlModifier) keys.append(QStringLiteral("⌘"));
#else
        if (modifiers & Qt::ControlModifier) keys.append(QStringLiteral("Ctrl"));
        if (modifiers & Qt::AltModifier) keys.append(QStringLiteral("Alt"));
        if (modifiers & Qt::ShiftModifier) keys.append(QStringLiteral("Shift"));
        if (modifiers & Qt::MetaModifier) keys.append(QStringLiteral("Meta"));
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
    const bool selected = option.state & QStyle::State_Selected;
    const QPalette &colors = option.palette;
    const QColor capFill = selected ? QColor(255, 255, 255, 40) : colors.color(QPalette::Button);
    const QColor capBorder = selected ? QColor(255, 255, 255, 90) : colors.color(QPalette::Mid);
    const QColor text = selected ? QColor(Qt::white) : colors.color(QPalette::Text);
    const QColor muted = selected ? QColor(255, 255, 255, 170) : colors.color(QPalette::PlaceholderText);

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
            drawWord(QStringLiteral("or"), muted);
        first = false;
        const QList<QStringList> chords = keysOf(shortcut);
        for (int c = 0; c < chords.size(); ++c) {
            if (c > 0)
                drawWord(QStringLiteral("then"), muted);
            for (const QString &key : chords[c]) {
                const qreal width = std::max(metrics.horizontalAdvance(key) + 12, height);
                const QRectF cap(x, top, width, height);
                painter->setPen(QPen(capBorder, 1));
                painter->setBrush(capFill);
                painter->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
                // The key's lower edge, slightly thicker.
                painter->setPen(QPen(capBorder, 1.5));
                painter->drawLine(QPointF(cap.left() + 3, cap.bottom() - 0.5), QPointF(cap.right() - 3, cap.bottom() - 0.5));
                painter->setPen(text);
                painter->drawText(cap, Qt::AlignCenter, key);
                x += width + 3;
            }
            x += 3;
        }
    }
    painter->restore();
}
