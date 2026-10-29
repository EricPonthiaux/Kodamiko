// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>

#include <utility>

class QHBoxLayout;
class QLabel;
class QTabBar;
class QToolButton;

// The header of a side panel view, as above the file tree: the view's
// title at the left, a detail in gray after it, icon buttons at the right,
// a line under it, or a selected tab when placed in the side panel.
class ViewHeader : public QFrame {
    Q_OBJECT

public:
    ViewHeader(const QString &title, QWidget *parent);
    ViewHeader(QWidget *title, QWidget *parent);

    // Shares the editor tabs' height, including their top margin.
    void alignWith(QTabBar *tabs);

    // An icon button at the right, after those already added; `iconName`
    // is one of the UiIcons, drawn in the color of setIconColor().
    QToolButton *addButton(const QString &iconName, const QString &toolTip);
    // "2 errors, 1 warning"; empty hides it.
    void setDetail(const QString &text);
    void setIconColor(const QColor &color);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateHeight();

    QPointer<QTabBar> editorTabBar_;
    QHBoxLayout *layout_;
    QLabel *detail_;
    QList<std::pair<QToolButton *, QString>> buttons_; // and their icon names
};
