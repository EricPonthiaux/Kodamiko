// SPDX-License-Identifier: GPL-3.0-only

#include "TitleBar.h"

#include "AppFonts.h"
#include "BrandMark.h"
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

namespace {

// Between the run block and the search actions, then before the window
// controls.
constexpr int groupSpacing = 14;
constexpr int windowControlsSpacing = 12;
const QColor runColor("#4fa36b");
const QColor stopColor("#d9534f");
// The run block's border, around its buttons.
constexpr int runBlockInset = 2;

// The height of every control but the window's: chips, buttons, the run
// block; the menu titles' padding comes close (see ThemeStyle).
int controlHeight()
{
    return UiMetrics::scaled(UiMetrics::toolbarButtonSize);
}

} // namespace

namespace {

// Duet's icon at the bar's start, drawn at the screen's resolution. Clicks
// go through: dragging it moves the window, like the bar.
class BrandLogo : public QWidget {
public:
    explicit BrandLogo(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("brandLogo"));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        AppFonts::followApplicationFont(this, [this] { setFixedHeight(controlHeight()); });
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setToolTip(QStringLiteral("Duet"));
    }

    QSize sizeHint() const override { return {controlHeight(), controlHeight()}; }

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
    setAttribute(Qt::WA_StyledBackground, false);

    leadingSpace_ = new QWidget(this);
    leadingSpace_->setFixedWidth(0);
    leadingSpace_->setAttribute(Qt::WA_TransparentForMouseEvents);
    leadingSpace_->hide();

    menuBar_ = new QMenuBar(this);
    menuBar_->setNativeMenuBar(false);
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);
    layout_->addWidget(leadingSpace_);
    logo_ = new BrandLogo(this);
    layout_->addWidget(logo_, 0, Qt::AlignVCenter);
    layout_->addSpacing(2);
    layout_->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout_->addSpacing(8);
    project_ = addChip();
    project_->setObjectName(QStringLiteral("projectChip"));
    AppFonts::setInterfaceFont(project_, 1);
    layout_->addStretch(1);
    layout_->addWidget(createRunBlock());
    layout_->addSpacing(groupSpacing);
    actions_ = new QHBoxLayout;
    actions_->setSpacing(2);
    layout_->addLayout(actions_);
    layout_->addSpacing(windowControlsSpacing);

    controls_ = new WindowControls(this);
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
    logo_->setFixedWidth(2 * tab.x() + tab.width());
}

QToolButton *TitleBar::addChip()
{
    auto *chip = new QToolButton(this);
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
    run_ = new QToolButton(runBlock_);
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
    // Spaces set the name apart from the folder icon and the chevron: a tool
    // button has no spacing of its own between its icon and its text.
    project_->setText(QStringLiteral(" %1  ▾").arg(name));
    project_->setToolTip(toolTip);
    project_->setMenu(menu);
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
    project_->setFixedHeight(control);
    const int folderSize = UiMetrics::scaled(UiMetrics::chipIconSize);
    project_->setIconSize(QSize(folderSize, folderSize));
    runBlock_->setFixedHeight(control);
    run_->setFixedHeight(control - runBlockInset);
    runDivider_->setFixedSize(1, control - 12);
    for (const ActionButton &entry : std::as_const(actionButtons_))
        sizeActionButton(entry);
    for (const auto kind : {WindowButton::Kind::Minimize, WindowButton::Kind::Maximize, WindowButton::Kind::Close})
        controls_->button(kind)->updateGeometry();
}

void TitleBar::sizeActionButton(const ActionButton &entry)
{
    const int size = controlHeight() - entry.inset;
    entry.button->setFixedSize(size, size);
}

void TitleBar::setWindowControlsVisible(bool visible)
{
    controls_->setVisible(visible);
}

void TitleBar::setLeadingSpace(int pixels)
{
    leadingSpace_->setFixedWidth(pixels);
    leadingSpace_->setVisible(pixels > 0);
}

WindowButton *TitleBar::windowButton(WindowButton::Kind kind) const
{
    return controls_->button(kind);
}

void TitleBar::applyTheme(const Theme &theme)
{
    theme_ = theme;
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
    const bool columnChanged = event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
                               event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange;
    if (watched == logoColumn_ && columnChanged)
        QMetaObject::invokeMethod(this, &TitleBar::updateLogoAlignment, Qt::QueuedConnection);
    return QWidget::eventFilter(watched, event);
}
