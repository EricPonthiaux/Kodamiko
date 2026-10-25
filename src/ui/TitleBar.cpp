// SPDX-License-Identifier: GPL-3.0-only

#include "TitleBar.h"

#include "AppFonts.h"
#include "BrandLogo.h"
#include "ElidingToolButton.h"
#include "FileIcons.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QTabBar>
#include <QToolButton>
#include <QWindow>

#include <algorithm>
#include <optional>

namespace {

constexpr int groupSpacing = 8;
constexpr int dragSpace = 24;
const QColor runColor("#4fa36b");
const QColor stopColor("#d9534f");
// The run block's border, around its buttons.
constexpr int runBlockInset = 2;
// Between a divider's ends and the top or bottom of a control.
constexpr int dividerInset = 6;

// The height of every control but the window's: chips, buttons, the run
// block; the menu titles' padding comes close (see ThemeStyle).
int controlHeight()
{
    return UiMetrics::scaled(UiMetrics::toolbarButtonSize);
}

// The width the bar's layout gives `widget`: its hint within its limits.
int hintedWidth(const QWidget *widget)
{
    return qBound(widget->minimumWidth(), widget->sizeHint().width(), widget->maximumWidth());
}

// Where the menu titles' texts lie in `menuBar`: from the menu bar's left
// edge to the first text, and from one text to the next. Empty without two
// visible menus.
struct MenuTitleSpacing {
    int firstTextLeft = 0;
    int textGap = 0;
};

std::optional<MenuTitleSpacing> menuTitleSpacing(QMenuBar *menuBar)
{
    QList<QAction *> menus = menuBar->actions();
    menus.removeIf([](const QAction *action) { return !action->isVisible() || action->isSeparator(); });
    if (menus.size() < 2)
        return std::nullopt;
    // The style pads each title equally on both sides of its text.
    const auto padding = [menuBar](QAction *action) {
        const int textWidth = menuBar->fontMetrics().size(Qt::TextShowMnemonic, action->text()).width();
        return (menuBar->actionGeometry(action).width() - textWidth) / 2;
    };
    const QRect first = menuBar->actionGeometry(menus[0]);
    const QRect second = menuBar->actionGeometry(menus[1]);
    const int firstPadding = padding(menus[0]);
    return MenuTitleSpacing{
        .firstTextLeft = first.left() + firstPadding,
        .textGap = second.left() - first.right() - 1 + firstPadding + padding(menus[1]),
    };
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::dark())
{
    setAttribute(Qt::WA_StyledBackground, false);

    menuBar_ = new QMenuBar(this);
    menuBar_->setNativeMenuBar(false);
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    menuBar_->installEventFilter(this);
    menuButton_ = new QToolButton(this);
    menuButton_->setObjectName(QStringLiteral("mainMenuButton"));
    menuButton_->setText(QStringLiteral("☰"));
    menuButton_->setToolTip(tr("Main Menu"));
    menuButton_->setAccessibleName(tr("Main Menu"));
    menuButton_->setAutoRaise(true);
    menuButton_->setPopupMode(QToolButton::InstantPopup);
    compactMenu_ = new QMenu(this);
    menuButton_->setMenu(compactMenu_);
    connect(compactMenu_, &QMenu::aboutToShow, this, &TitleBar::rebuildCompactMenu);
    menuButton_->hide();

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(groupSpacing);
    logo_ = new BrandLogo(&theme_, this);
    layout_->addWidget(logo_, 0, Qt::AlignVCenter);
    layout_->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout_->addWidget(menuButton_, 0, Qt::AlignVCenter);
    // The project and the run block in the middle of the bar: the stretches
    // share the room left equally, the balances make up for the difference
    // between the menus and the buttons at the right.
    layout_->addStretch(1);
    leftBalance_ = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    layout_->addSpacerItem(leftBalance_);
    project_ = addChip();
    project_->setObjectName(QStringLiteral("projectChip"));
    AppFonts::setInterfaceFont(project_, 1);
    layout_->addWidget(createRunBlock());
    rightBalance_ = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    layout_->addSpacerItem(rightBalance_);
    layout_->addStretch(1);
    actionGroup_ = new QWidget(this);
    actions_ = new QHBoxLayout(actionGroup_);
    actions_->setContentsMargins(0, 0, 0, 0);
    actions_->setSpacing(2);
    layout_->addWidget(actionGroup_);

    controlsDivider_ = new QFrame(this);
    controlsDivider_->setObjectName(QStringLiteral("titleDivider"));
    layout_->addWidget(controlsDivider_, 0, Qt::AlignVCenter);
    controls_ = new WindowControls(this);
    controls_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    layout_->addWidget(controls_);
    AppFonts::followApplicationFont(this, [this] { applyMetrics(); });
}

void TitleBar::alignLogoWith(QTabBar *column)
{
    Q_ASSERT(column);
    if (logoColumn_)
        logoColumn_->removeEventFilter(this);
    logoColumn_ = column;
    column->installEventFilter(this);
    updateLogoAlignment();
}

void TitleBar::updateLogoAlignment()
{
    if (!logoColumn_ || logoColumn_->count() == 0)
        return;
    // The tab bar can have spare width to the right of its actual tabs.
    const QRect tab = logoColumn_->tabRect(0);
    logo_->setPlaceWidth(2 * tab.x() + tab.width());
}

ElidingToolButton *TitleBar::addChip()
{
    auto *chip = new ElidingToolButton(this);
    chip->setTextSuffix(QStringLiteral("  ▾"));
    chip->setAutoRaise(true);
    chip->setFocusPolicy(Qt::NoFocus);
    chip->setPopupMode(QToolButton::InstantPopup);
    chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chip->setObjectName(QStringLiteral("chip"));
    layout_->addWidget(chip);
    return chip;
}

// [ ⚒ CMake: build default ▾ │ ▶ ], its parts inside the block's border.
QFrame *TitleBar::createRunBlock()
{
    runBlock_ = new QFrame(this);
    runBlock_->setObjectName(QStringLiteral("runBlock"));
    auto *row = new QHBoxLayout(runBlock_);
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(0);
    run_ = new ElidingToolButton(runBlock_, 260);
    run_->setTextSuffix(QStringLiteral("  ▾"));
    run_->setObjectName(QStringLiteral("runConfiguration"));
    run_->setAutoRaise(true);
    run_->setFocusPolicy(Qt::NoFocus);
    run_->setPopupMode(QToolButton::InstantPopup);
    run_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    row->addWidget(run_);
    runDivider_ = new QFrame(runBlock_);
    runDivider_->setObjectName(QStringLiteral("runDivider"));
    row->addWidget(runDivider_, 0, Qt::AlignVCenter);
    runActions_ = new QHBoxLayout;
    runActions_->setSpacing(0);
    row->addLayout(runActions_);
    runBlock_->hide();
    return runBlock_;
}

void TitleBar::setProject(const QString &name, const QString &toolTip, QMenu *menu)
{
    project_->setText(name);
    project_->setToolTip(toolTip);
    project_->setMenu(menu);
    project_->setAccessibleName(tr("Project: %1").arg(name));
    scheduleLayout();
}

QToolButton *TitleBar::actionButton(QAction *action, const QString &iconName, const QColor &color)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("titleAction"));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(UiIcons::icon(iconName, color.isValid() ? color : theme_.foreground));
    actionButtons_.append({button, iconName, color});
    sizeActionButton(actionButtons_.last());
    const auto update = [button, action] {
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        QString name = action->text();
        name.remove(QLatin1Char('&'));
        button->setToolTip(shortcut.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, shortcut));
        button->setAccessibleName(name);
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
    secondaryActions_.append(action);
    scheduleLayout();
}

void TitleBar::setRunConfiguration(const RunChip &chip)
{
    const bool shown = !chip.name.isEmpty();
    runBlock_->setVisible(shown);
    run_->setIcon(chip.icon);
    run_->setText(chip.name);
    run_->setToolTip(chip.toolTip.isEmpty() ? chip.name : chip.name + QLatin1Char('\n') + chip.toolTip);
    run_->setMenu(chip.menu);
    run_->setAccessibleName(tr("Run configuration: %1").arg(chip.name));
    running_ = chip.running;
    if (runButton_)
        runButton_->setVisible(shown && !chip.running);
    if (stopButton_)
        stopButton_->setVisible(shown && chip.running);
    scheduleLayout();
}

void TitleBar::setRunActions(QAction *run, QAction *stop)
{
    runButton_ = actionButton(run, QStringLiteral("run"), runColor);
    stopButton_ = actionButton(stop, QStringLiteral("stop"), stopColor);
    // One shown at a time, at the same place and size: nothing moves.
    for (ActionButton &entry : actionButtons_) {
        if (entry.button != runButton_ && entry.button != stopButton_)
            continue;
        entry.button->setObjectName(QStringLiteral("runAction"));
        entry.inset = runBlockInset;
        sizeActionButton(entry);
        runActions_->addWidget(entry.button);
    }
    runButton_->setVisible(runBlock_->isVisibleTo(this) && !running_);
    stopButton_->setVisible(runBlock_->isVisibleTo(this) && running_);
}

void TitleBar::applyMetrics()
{
    setFixedHeight(UiMetrics::scaled(UiMetrics::titleBarHeight));
    const int control = controlHeight();
    layout_->setSpacing(UiMetrics::scaled(groupSpacing));
    menuButton_->setFixedSize(control, control);
    project_->setFixedHeight(control);
    const int folderSize = UiMetrics::scaled(UiMetrics::chipIconSize);
    project_->setIconSize(QSize(folderSize, folderSize));
    runBlock_->setFixedHeight(control);
    run_->setFixedHeight(control - runBlockInset);
    run_->setIconSize(UiMetrics::scaledSquare(UiMetrics::iconSize));
    runDivider_->setFixedSize(1, control - 2 * dividerInset);
    controlsDivider_->setFixedSize(1, control - 2 * dividerInset);
    for (const ActionButton &entry : std::as_const(actionButtons_))
        sizeActionButton(entry);
    for (const auto kind : {WindowButton::Kind::Minimize, WindowButton::Kind::Maximize, WindowButton::Kind::Close})
        controls_->button(kind)->updateGeometry();
    scheduleLayout();
}

void TitleBar::scheduleLayout()
{
    if (layoutScheduled_)
        return;
    layoutScheduled_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            layoutScheduled_ = false;
            updateResponsiveLayout();
        },
        Qt::QueuedConnection);
}

int TitleBar::boundedRunBlockWidth() const
{
    return runBlock_->sizeHint().width() - run_->sizeHint().width() + run_->boundedWidth();
}

void TitleBar::updateResponsiveLayout()
{
    layout_->setSpacing(UiMetrics::scaled(width() < UiMetrics::scaled(600) ? groupSpacing / 2 : groupSpacing));
    fitLogoToMenuTitles();
    // Measure the expanded controls at their bounded widths, independent of
    // which groups were hidden or how far the labels grew on the previous
    // resize.
    int fixed = UiMetrics::scaled(dragSpace) + layout_->contentsMargins().left() + layout_->contentsMargins().right();
    int groups = 0;
    const std::initializer_list<const QWidget *> fixedWidgets{logo_, controlsDivider_, controls_};
    for (const QWidget *widget : fixedWidgets) {
        if (widget->isHidden())
            continue;
        fixed += hintedWidth(widget);
        ++groups;
    }
    fixed += project_->boundedWidth();
    ++groups;
    if (!runBlock_->isHidden()) {
        fixed += boundedRunBlockWidth();
        ++groups;
    }
    const int gap = layout_->spacing();
    const int actionWidth = secondaryActions_.isEmpty() ? 0 : actionGroup_->sizeHint().width() + gap;
    const int available = width() - fixed - groups * gap;
    const bool compact = available < menuBar_->sizeHint().width() + actionWidth;
    menuBar_->setVisible(!compact);
    menuButton_->setVisible(compact);
    const int menuWidth = compact ? menuButton_->width() : menuBar_->sizeHint().width();
    const bool showsActions = actionWidth > 0 && available >= menuWidth + actionWidth;
    actionGroup_->setVisible(showsActions);
    // The room left lengthens the labels before it goes to the drag space.
    int spare = available - menuWidth - (showsActions ? actionWidth : 0);
    spare -= project_->widenInto(spare);
    run_->widenInto(runBlock_->isHidden() ? 0 : spare);
    const int minimumWithLogo = layout_->minimumSize().width() + (logo_->isHidden() ? hintedWidth(logo_) + gap : 0);
    // macOS's traffic lights take the logo's place, over the column.
    const bool hasLeadingSpace = layout_->contentsMargins().left() > 0;
    logo_->setVisible(!hasLeadingSpace && width() >= minimumWithLogo);
    centerProjectGroup();
}

void TitleBar::fitLogoToMenuTitles()
{
    const std::optional<MenuTitleSpacing> titles = menuTitleSpacing(menuBar_);
    if (!titles)
        return;
    const int textLeftOfMenuBar = layout_->spacing() + titles->firstTextLeft;
    logo_->setTrailingGap(std::max(0, titles->textGap - textLeftOfMenuBar));
}

int TitleBar::sideWidth(std::initializer_list<const QWidget *> widgets) const
{
    int width = 0;
    for (const QWidget *widget : widgets)
        if (!widget->isHidden())
            width += hintedWidth(widget) + layout_->spacing();
    return width;
}

void TitleBar::centerProjectGroup()
{
    const int left = layout_->contentsMargins().left() + sideWidth({logo_, menuBar_, menuButton_});
    const int right = sideWidth({actionGroup_, controlsDivider_, controls_}) + layout_->contentsMargins().right();
    int center = project_->sizeHint().width();
    if (!runBlock_->isHidden())
        center += layout_->spacing() + runBlock_->sizeHint().width();
    // Off center rather than over its neighbors, or without room to drag.
    const int room = width() - left - center - right - UiMetrics::scaled(dragSpace);
    const int balance = qBound(0, qAbs(left - right), qMax(room, 0));
    const int leftBalance = right > left ? balance : 0;
    const int rightBalance = left > right ? balance : 0;
    // Only on a change: invalidating the layout schedules this again.
    if (leftBalance_->sizeHint().width() == leftBalance && rightBalance_->sizeHint().width() == rightBalance)
        return;
    leftBalance_->changeSize(leftBalance, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    rightBalance_->changeSize(rightBalance, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    layout_->invalidate();
}

void TitleBar::rebuildCompactMenu()
{
    compactMenu_->clear();
    compactMenu_->addActions(menuBar_->actions());
    if (actionGroup_->isHidden() && !secondaryActions_.isEmpty()) {
        compactMenu_->addSeparator();
        compactMenu_->addActions(secondaryActions_);
    }
}

bool TitleBar::event(QEvent *event)
{
    const bool result = QWidget::event(event);
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::StyleChange)
        scheduleLayout();
    return result;
}

void TitleBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
    // The layout already placed the controls for the new width, with the
    // previous one's balances and labels: place them again at once, the run
    // block first so that the bar sees its new width.
    runBlock_->layout()->activate();
    layout_->activate();
}

void TitleBar::sizeActionButton(const ActionButton &entry)
{
    const int size = controlHeight() - entry.inset;
    entry.button->setFixedSize(size, size);
    entry.button->setIconSize(UiMetrics::scaledSquare(UiMetrics::iconSize));
}

void TitleBar::setWindowControlsVisible(bool visible)
{
    controls_->setVisible(visible);
    controlsDivider_->setVisible(visible);
    scheduleLayout();
}

void TitleBar::setLeadingSpace(int pixels)
{
    QMargins margins = layout_->contentsMargins();
    margins.setLeft(pixels);
    layout_->setContentsMargins(margins);
    scheduleLayout();
}

void TitleBar::setTrailingSpace(int pixels)
{
    QMargins margins = layout_->contentsMargins();
    margins.setRight(pixels);
    layout_->setContentsMargins(margins);
    scheduleLayout();
}

void TitleBar::setLogoScale(qreal scale)
{
    logo_->setScale(scale);
}

WindowButton *TitleBar::windowButton(WindowButton::Kind kind) const
{
    return controls_->button(kind);
}

void TitleBar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    logo_->update();
    project_->setIcon(FileIcons::folder(theme.isDark)); // the files tree's
    controls_->applyTheme(theme);
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
        WindowControls::toggleMaximized(window());
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == menuBar_ && (event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionRemoved ||
                                event->type() == QEvent::ActionChanged))
        scheduleLayout();
    const bool columnChanged = event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
                               event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange;
    if (watched == logoColumn_ && columnChanged)
        QMetaObject::invokeMethod(this, &TitleBar::updateLogoAlignment, Qt::QueuedConnection);
    return QWidget::eventFilter(watched, event);
}
