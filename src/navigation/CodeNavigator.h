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

    EditorTabs *tabs_;
    SymbolIndex *index_;
    NavigationHistory history_;
};
