// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "NavigationHistory.h"
#include "SymbolIndex.h"

#include <QObject>

class EditorTabs;
class EditorWidget;

// "Go to Definition" (Ctrl+B, Ctrl+click) and the jump history,
// in the editor tabs.
class CodeNavigator : public QObject {
    Q_OBJECT

public:
    CodeNavigator(EditorTabs *tabs, SymbolIndex *index, QObject *parent = nullptr);

    void goToDefinition();
    // Opens the definition of the project's symbol `name`, a member of
    // `container` when it is not empty, in `filePath` (relative to the
    // project) when it is not empty: an element of a diagram. A menu at the
    // pointer when several are as likely; the file itself when the symbol is
    // not in it or `name` is empty; a status message when nothing is found.
    void goToSymbol(const QString &name, const QString &container, const QString &filePath);
    void goBack();
    void goForward();

    // Opens `filePath` at a byte `position`; the jump is recorded in the history.
    void goTo(const QString &filePath, int position);
    // Opens `filePath` at `line` (from 0) and selects `length` characters
    // from `column`; the jump is recorded in the history.
    void goToLine(const QString &filePath, int line, int column = 0, int length = 0);

signals:
    void statusMessage(const QString &message);

private:
    std::optional<NavigationHistory::Location> currentLocation() const;
    void jumpTo(const SymbolLocation &target);
    void showLocation(const NavigationHistory::Location &location);
    void chooseTarget(EditorWidget *editor, const QList<SymbolLocation> &targets);
    // The menu of `targets`, opened at `globalPoint` over `clicked`.
    void chooseTargetAt(QWidget &clicked, QPoint globalPoint, const QList<SymbolLocation> &targets);
    QString notFoundMessage(const QString &name) const;
    // No symbol found: opens the linked `file` when there is one (absolute
    // path), else says that `qualifiedName` was not found.
    void showLinkedFile(const QString &file, const QString &qualifiedName);

    EditorTabs *tabs_;
    SymbolIndex *index_;
    NavigationHistory history_;
};
