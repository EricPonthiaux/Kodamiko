// SPDX-License-Identifier: GPL-3.0-only

#include "PreviewTab.h"

#include <QStyle>
#include <QTabBar>
#include <QTabWidget>

PreviewTab::PreviewTab(QTabWidget *tabs)
    : tabs_(tabs)
{
    // A tab moved: the preview is kept, or no longer last.
    QObject::connect(tabs_->tabBar(), &QTabBar::tabMoved, tabs_, [this] { keep(page_); });
    QObject::connect(tabs_->tabBar(), &QTabBar::tabBarDoubleClicked, tabs_,
                     [this](int index) { keep(tabs_->widget(index)); });
}

int PreviewTab::insertionIndex() const
{
    const int index = page_ ? tabs_->indexOf(page_) : -1;
    return index >= 0 ? index : tabs_->count();
}

void PreviewTab::setPage(QWidget *page)
{
    page_ = page;
    updateStyle();
}

void PreviewTab::keep(const QWidget *page)
{
    if (!page_ || page != page_)
        return;
    page_ = nullptr;
    updateStyle();
}

void PreviewTab::tabRemoved()
{
    if (page_ && tabs_->indexOf(page_) < 0) {
        page_ = nullptr;
        updateStyle();
    }
}

void PreviewTab::updateStyle()
{
    QTabBar *bar = tabs_->tabBar();
    const bool hasPreview = page_ != nullptr;
    if (bar->property("preview").toBool() == hasPreview)
        return;
    bar->setProperty("preview", hasPreview);
    // A dynamic property in a style sheet's selector is read when polishing.
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
    bar->update();
}
