// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Problem.h"

#include <QList>
#include <QSet>
#include <QTimer>
#include <QWidget>

class ProblemStore;
class QAction;
class QPoint;
class QLabel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class TreeColumnFit;
class ViewHeader;

// Problems pane: every problem of the project (the language servers', the
// builds'), by file, the files with errors first. A click opens the file
// at the problem. Rebuilt shortly after the problems change.
class ProblemsView : public QWidget {
    Q_OBJECT

public:
    explicit ProblemsView(ProblemStore *problems, QWidget *parent = nullptr);

    // Folders are shown relative to it.
    void setProjectFolder(const QString &folder);
    void setDarkIcons(bool dark);
    // Color of the header's line icons, from the theme.
    void setIconColor(const QColor &color);

    QTreeWidget *tree() const { return tree_; }

    // The actions of the context menu of a problem or a file, about
    // selectedProblems(). Not owned.
    void setContextActions(const QList<QAction *> &actions) { contextActions_ = actions; }

    // The selected problem, or every problem of the selected file; none
    // without a selection.
    Problems selectedProblems() const;

    int errorCount() const { return errors_; }

    int warningCount() const { return warnings_; }

signals:
    // `line` and `column` from 0; `column` -1 when it is not known.
    void problemActivated(const QString &path, int line, int column);
    void countsChanged(int errors, int warnings);

private:
    void scheduleRefresh();
    void refresh();
    void activate(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &position);
    // Every file; remembered as for a click on each.
    void setAllExpanded(bool expanded);

    ProblemStore *problems_;
    QString folder_;
    bool darkIcons_ = true;
    QTimer refreshTimer_;
    int errors_ = 0;
    int warnings_ = 0;
    QSet<QString> collapsedFiles_;
    QList<QAction *> contextActions_;

    ViewHeader *header_;
    QTreeWidget *tree_;
    TreeColumnFit *columnFit_;
    QLabel *placeholder_;
    QStackedWidget *stack_;
};
