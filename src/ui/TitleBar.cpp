#include "TitleBar.h"

#include "AppFonts.h"
#include "BrandMark.h"
#include "UiIcons.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QWindow>

namespace {

constexpr int barHeight = 40;
constexpr int controlWidth = 46;
// The height of every control but the window's: chips, buttons, the run
// block; the menu titles' padding comes close (see ThemeStyle).
constexpr int controlHeight = 30;
// Between the run block and the search actions, then before the window
// controls.
constexpr int groupSpacing = 14;
constexpr int windowControlsSpacing = 12;
const QColor runColor("#4fa36b");
const QColor stopColor("#d9534f");

} // namespace

WindowButton::WindowButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent)
    , kind_(kind)
    , theme_(Theme::dark())
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
}

QSize WindowButton::sizeHint() const
{
    return {controlWidth, barHeight};
}

void WindowButton::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

void WindowButton::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void WindowButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void WindowButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = theme_.foreground;
    if (hovered_ || isDown()) {
        // Closing turns red, as everywhere.
        const QColor background =
            kind_ == Kind::Close ? QColor("#e81123") : theme_.hover.lighter(theme_.isDark ? 140 : 97);
        painter.fillRect(rect(), isDown() ? background.darker(115) : background);
        if (kind_ == Kind::Close)
            color = Qt::white;
    }
    painter.setPen(QPen(color, 1.1));
    painter.setBrush(Qt::NoBrush);
    const QPointF center = QRectF(rect()).center();
    const qreal half = 5;
    switch (kind_) {
    case Kind::Minimize:
        painter.drawLine(QPointF(center.x() - half, center.y()), QPointF(center.x() + half, center.y()));
        break;
    case Kind::Maximize:
        if (window()->isMaximized()) {
            // Restore: two overlapping squares.
            painter.drawRoundedRect(QRectF(center.x() - half, center.y() - half + 2, 2 * half - 2, 2 * half - 2), 1.5,
                                    1.5);
            QPainterPath back;
            back.moveTo(center.x() - half + 2, center.y() - half + 2);
            back.lineTo(center.x() - half + 2, center.y() - half);
            back.lineTo(center.x() + half, center.y() - half);
            back.lineTo(center.x() + half, center.y() + half - 2);
            back.lineTo(center.x() + half - 2, center.y() + half - 2);
            painter.drawPath(back);
        } else {
            painter.drawRoundedRect(QRectF(center.x() - half, center.y() - half, 2 * half, 2 * half), 1.5, 1.5);
        }
        break;
    case Kind::Close:
        painter.drawLine(QPointF(center.x() - half, center.y() - half), QPointF(center.x() + half, center.y() + half));
        painter.drawLine(QPointF(center.x() + half, center.y() - half), QPointF(center.x() - half, center.y() + half));
        break;
    }
}

namespace {

// Duet's icon at the bar's start, drawn at the screen's resolution. Clicks
// go through: dragging it moves the window, like the bar.
class BrandLogo : public QWidget {
public:
    explicit BrandLogo(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(20, 20);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setToolTip(QStringLiteral("Duet"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        BrandMark::paintAppIcon(&painter, rect());
    }
};

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::dark())
{
    setFixedHeight(barHeight);
    setAttribute(Qt::WA_StyledBackground, false);

    leadingSpace_ = new QWidget(this);
    leadingSpace_->setFixedWidth(0);
    leadingSpace_->setAttribute(Qt::WA_TransparentForMouseEvents);

    menuBar_ = new QMenuBar(this);
    menuBar_->setNativeMenuBar(false);
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(6, 0, 0, 0);
    layout_->setSpacing(4);
    layout_->addWidget(leadingSpace_);
    layout_->addSpacing(4);
    layout_->addWidget(new BrandLogo(this), 0, Qt::AlignVCenter);
    layout_->addSpacing(2);
    layout_->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout_->addSpacing(8);
    project_ = addChip();
    project_->setObjectName(QStringLiteral("projectChip"));
    project_->setIconSize(QSize(16, 16));
    AppFonts::setInterfaceFont(project_, 1);
    layout_->addStretch(1);
    claude_ = addChip();
    connect(claude_, &QToolButton::clicked, this, &TitleBar::claudeClicked);
    claude_->hide();
    layout_->addStretch(1);
    layout_->addWidget(createRunBlock());
    layout_->addSpacing(groupSpacing);
    actions_ = new QHBoxLayout;
    actions_->setSpacing(2);
    layout_->addLayout(actions_);
    layout_->addSpacing(windowControlsSpacing);

    minimize_ = new WindowButton(WindowButton::Kind::Minimize, this);
    minimize_->setToolTip(tr("Minimize"));
    connect(minimize_, &QAbstractButton::clicked, this, [this] { window()->showMinimized(); });
    maximize_ = new WindowButton(WindowButton::Kind::Maximize, this);
    connect(maximize_, &QAbstractButton::clicked, this, &TitleBar::toggleMaximized);
    close_ = new WindowButton(WindowButton::Kind::Close, this);
    close_->setToolTip(tr("Close"));
    connect(close_, &QAbstractButton::clicked, this, [this] { window()->close(); });
    for (WindowButton *button : {minimize_, maximize_, close_})
        layout_->addWidget(button);
    updateMaximizeButton();

    if (parent)
        parent->window()->installEventFilter(this); // maximized or not
}

QToolButton *TitleBar::addChip()
{
    auto *chip = new QToolButton(this);
    chip->setAutoRaise(true);
    chip->setFocusPolicy(Qt::NoFocus);
    chip->setPopupMode(QToolButton::InstantPopup);
    chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chip->setObjectName(QStringLiteral("chip"));
    chip->setFixedHeight(controlHeight);
    layout_->addWidget(chip);
    return chip;
}

// [ ⚒ CMake: build default ▾ │ ▶ ], its parts inside the block's border.
QFrame *TitleBar::createRunBlock()
{
    runBlock_ = new QFrame(this);
    runBlock_->setObjectName(QStringLiteral("runBlock"));
    runBlock_->setFixedHeight(controlHeight);
    auto *row = new QHBoxLayout(runBlock_);
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(0);
    run_ = new QToolButton(runBlock_);
    run_->setAutoRaise(true);
    run_->setFocusPolicy(Qt::NoFocus);
    run_->setPopupMode(QToolButton::InstantPopup);
    run_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    run_->setFixedHeight(controlHeight - 2);
    row->addWidget(run_);
    auto *divider = new QFrame(runBlock_);
    divider->setObjectName(QStringLiteral("runDivider"));
    divider->setFixedSize(1, controlHeight - 12);
    row->addWidget(divider, 0, Qt::AlignVCenter);
    runActions_ = new QHBoxLayout;
    runActions_->setSpacing(0);
    row->addLayout(runActions_);
    runBlock_->hide();
    return runBlock_;
}

void TitleBar::setProject(const QString &name, const QString &toolTip, QMenu *menu)
{
    // Spaces set the name apart from the folder icon and the chevron: a tool
    // button has no spacing of its own between its icon and its text.
    project_->setText(QStringLiteral(" %1  ▾").arg(name));
    project_->setToolTip(toolTip);
    project_->setMenu(menu);
}

void TitleBar::setClaudeState(const QIcon &icon, const QString &text, const QString &toolTip)
{
    claude_->setVisible(!text.isEmpty());
    claude_->setIcon(icon);
    claude_->setText(text);
    claude_->setToolTip(toolTip);
}

QToolButton *TitleBar::actionButton(QAction *action, const QString &iconName, const QColor &color)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("titleAction"));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(controlHeight, controlHeight);
    button->setIcon(UiIcons::icon(iconName, color.isValid() ? color : theme_.foreground));
    actionButtons_.append({button, iconName, color});
    button->setIconSize(QSize(16, 16));
    const auto update = [button, action] {
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        QString name = action->text();
        name.remove(QLatin1Char('&'));
        button->setToolTip(shortcut.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, shortcut));
        button->setEnabled(action->isEnabled());
    };
    update();
    connect(action, &QAction::changed, button, update);
    connect(button, &QToolButton::clicked, action, &QAction::trigger);
    return button;
}

void TitleBar::addActionButton(QAction *action, const QString &iconName)
{
    actions_->addWidget(actionButton(action, iconName));
}

void TitleBar::setRunConfiguration(const RunChip &chip)
{
    const bool shown = !chip.name.isEmpty();
    runBlock_->setVisible(shown);
    run_->setIcon(chip.icon);
    run_->setText(chip.name + QStringLiteral("  ▾"));
    run_->setToolTip(chip.toolTip);
    run_->setMenu(chip.menu);
    running_ = chip.running;
    if (runButton_)
        runButton_->setVisible(shown && !chip.running);
    if (stopButton_)
        stopButton_->setVisible(shown && chip.running);
}

void TitleBar::setRunActions(QAction *run, QAction *stop)
{
    runButton_ = actionButton(run, QStringLiteral("run"), runColor);
    stopButton_ = actionButton(stop, QStringLiteral("stop"), stopColor);
    // One shown at a time, at the same place and size: nothing moves.
    for (QToolButton *button : {runButton_, stopButton_}) {
        button->setObjectName(QStringLiteral("runAction"));
        button->setFixedSize(controlHeight - 2, controlHeight - 2);
        runActions_->addWidget(button);
    }
    runButton_->setVisible(runBlock_->isVisibleTo(this) && !running_);
    stopButton_->setVisible(runBlock_->isVisibleTo(this) && running_);
}

void TitleBar::setWindowControlsVisible(bool visible)
{
    for (WindowButton *button : {minimize_, maximize_, close_})
        button->setVisible(visible);
}

void TitleBar::setLeadingSpace(int pixels)
{
    leadingSpace_->setFixedWidth(pixels);
}

WindowButton *TitleBar::windowButton(WindowButton::Kind kind) const
{
    switch (kind) {
    case WindowButton::Kind::Minimize:
        return minimize_;
    case WindowButton::Kind::Maximize:
        return maximize_;
    case WindowButton::Kind::Close:
        break;
    }
    return close_;
}

void TitleBar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    project_->setIcon(UiIcons::icon(QStringLiteral("folder"), theme.mutedText));
    for (WindowButton *button : {minimize_, maximize_, close_})
        button->applyTheme(theme);
    for (const ActionButton &entry : std::as_const(actionButtons_))
        entry.button->setIcon(UiIcons::icon(entry.iconName, entry.color.isValid() ? entry.color : theme.foreground));
    update();
}

void TitleBar::paintEvent(QPaintEvent *)
{
    // A line under the bar; the background is the window's.
    QPainter painter(this);
    painter.setPen(theme_.line);
    painter.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        toggleMaximized();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::toggleMaximized()
{
    QWidget *top = window();
    if (top->isMaximized())
        top->showNormal();
    else
        top->showMaximized();
}

void TitleBar::updateMaximizeButton()
{
    maximize_->setToolTip(window()->isMaximized() ? tr("Restore") : tr("Maximize"));
    maximize_->update();
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::WindowStateChange)
        updateMaximizeButton();
    return QWidget::eventFilter(watched, event);
}
