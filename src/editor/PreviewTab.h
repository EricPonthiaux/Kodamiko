// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPointer>

class QTabWidget;
class QWidget;

// The editor's preview tab, as VS Code's: a file shown by a single click in
// the file tree takes the place of the one shown before, until it is kept
// open (a double-click, an edit, a move of its tab). The preview is always
// the last tab: new tabs go before it, so the style sheet can set its title
// in italics (QTabBar[preview="true"]::tab:last, see ThemeStyle).
class PreviewTab {
public:
    // `tabs` must outlive this object; its tab bar's "preview" property
    // follows the preview.
    explicit PreviewTab(QTabWidget *tabs);
    PreviewTab(const PreviewTab &) = delete;
    PreviewTab &operator=(const PreviewTab &) = delete;
    PreviewTab(PreviewTab &&) = delete;
    PreviewTab &operator=(PreviewTab &&) = delete;
    ~PreviewTab() = default;

    // The page shown as a preview, null if none.
    QWidget *page() const { return page_; }

    // Where a new tab goes: before the preview.
    int insertionIndex() const;
    // `page`, the last tab, becomes the preview.
    void setPage(QWidget *page);
    // `page` stays open: it is no longer the preview, if it was.
    void keep(const QWidget *page);
    // To call when a tab is removed: the preview may be gone.
    void tabRemoved();

private:
    void updateStyle();

    QTabWidget *tabs_;
    QPointer<QWidget> page_;
};
