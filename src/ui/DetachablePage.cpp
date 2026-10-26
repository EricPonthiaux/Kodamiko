// SPDX-License-Identifier: GPL-3.0-only

#include "DetachablePage.h"

#include "DesktopIntegration.h"
#include "StrayWindowCheck.h"
#include "WindowFrame.h"
#include "WindowTitleBar.h"

#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace {

// Without a saved geometry, the window opens over the stack, this far in.
constexpr int windowOffsetPx = 40;

} // namespace

DetachablePage::DetachablePage(QStackedWidget *stack, QWidget *page, QString settingsKey, Texts texts)
    : QObject(stack)
    , stack_(stack)
    , page_(page)
    , settingsKey_(std::move(settingsKey))
    , texts_(std::move(texts))
    , placeholder_(createPlaceholder())
    , theme_(Theme::duet())
{
    Q_ASSERT(stack_->indexOf(page_) >= 0);
}

DetachablePage::~DetachablePage()
{
    saveState();
}

QWidget *DetachablePage::createPlaceholder()
{
    auto *placeholder = new QWidget(stack_);
    auto *message = new QLabel(texts_.awayMessage, placeholder);
    message->setAlignment(Qt::AlignCenter);
    message->setEnabled(false);
    auto *show = new QPushButton(tr("Show the Window"), placeholder);
    auto *attach = new QPushButton(texts_.attachAction, placeholder);
    connect(show, &QPushButton::clicked, this, &DetachablePage::raiseWindow);
    connect(attach, &QPushButton::clicked, this, &DetachablePage::attach);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(show);
    buttons->addWidget(attach);
    buttons->addStretch();
    auto *layout = new QVBoxLayout(placeholder);
    layout->addStretch();
    layout->addWidget(message);
    layout->addSpacing(8);
    layout->addLayout(buttons);
    layout->addStretch();
    placeholder->hide();
    return placeholder;
}

void DetachablePage::detach()
{
    if (window_)
        return;
    const int index = stack_->indexOf(page_);
    const bool wasCurrent = stack_->currentIndex() == index;
    const QSize size = page_->size();
    stack_->removeWidget(page_);
    stack_->insertWidget(index, placeholder_);
    if (wasCurrent)
        stack_->setCurrentIndex(index);

    // No parent: an independent window, which the main one does not cover.
    window_ = std::make_unique<QWidget>();
    StrayWindowCheck::allow(window_.get());
    window_->setWindowTitle(texts_.windowTitle);
    window_->addActions(windowActions_);
    frame_ = new WindowFrame(window_.get());
    titleBar_ = new WindowTitleBar(window_.get());
    auto *layout = new QVBoxLayout(window_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar_);
    layout->addWidget(page_);
    applyTheme(theme_);
    setFrameless(frameless_);
    page_->show();
    const QByteArray geometry = QSettings().value(settingsKey_ + QStringLiteral("/geometry")).toByteArray();
    if (geometry.isEmpty() || !window_->restoreGeometry(geometry)) {
        window_->resize(size);
        window_->move(stack_->mapToGlobal(QPoint(windowOffsetPx, windowOffsetPx)));
    }
    window_->installEventFilter(this);
    DesktopIntegration::followActiveSpace(window_.get());
    window_->show();
    raiseWindow();
    saveState();
    emit detachedChanged(true);
}

void DetachablePage::attach()
{
    if (!window_)
        return;
    saveState();
    const int index = stack_->indexOf(placeholder_);
    const bool wasCurrent = stack_->currentIndex() == index;
    stack_->removeWidget(placeholder_); // hidden, still a child of the stack
    stack_->insertWidget(index, page_);
    if (wasCurrent)
        stack_->setCurrentIndex(index);
    // Perhaps in the window's own close event: deleted once it returns.
    window_->removeEventFilter(this);
    window_->hide();
    frame_ = nullptr;
    titleBar_ = nullptr;
    window_.release()->deleteLater();
    emit detachedChanged(false);
}

void DetachablePage::raiseWindow()
{
    if (!window_)
        return;
    // Out of the taskbar if minimized, but still maximized or full screen
    // if it was: showNormal() would undo that.
    window_->setWindowState(window_->windowState() & ~Qt::WindowMinimized);
    window_->show();
    window_->raise();
    window_->activateWindow();
}

void DetachablePage::setWindowTitle(const QString &title)
{
    texts_.windowTitle = title;
    if (window_)
        window_->setWindowTitle(title);
}

void DetachablePage::setFrameless(bool frameless)
{
    frameless_ = frameless;
    if (!window_)
        return;
    frame_->setFrameless(frameless);
    titleBar_->setVisible(frameless);
    // The new window flags recreated the native window, shown again.
    if (window_->isVisible())
        DesktopIntegration::followActiveSpace(window_.get());
}

void DetachablePage::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (!window_)
        return;
    frame_->applyTheme(theme);
    titleBar_->applyTheme(theme);
}

void DetachablePage::setWindowActions(const QList<QAction *> &actions)
{
    windowActions_ = actions;
    if (window_)
        window_->addActions(actions);
}

bool DetachablePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != window_.get())
        return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::Close:
        event->ignore();
        attach();
        return true;
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
        emit windowActivationChanged();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

// While detached: where the window is, for the next detach.
void DetachablePage::saveState() const
{
    if (!window_)
        return;
    QSettings().setValue(settingsKey_ + QStringLiteral("/geometry"), window_->saveGeometry());
}
