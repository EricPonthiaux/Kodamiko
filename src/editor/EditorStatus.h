#pragma once

#include <QPointer>
#include <QWidget>

class EditorWidget;
class QToolButton;

// Status bar widgets of the current editor, as in IntelliJ: cursor position
// ("12:5", with the selection size) that opens Go to Line, line ending
// ("LF") and indentation ("4 spaces") that open a menu to change them, and
// the file's encoding.
class EditorStatus : public QWidget {
    Q_OBJECT

public:
    explicit EditorStatus(QWidget *parent = nullptr);

    // nullptr hides the widgets.
    void setEditor(EditorWidget *editor);

    // Ctrl+G: popup asking for "line[:column]".
    void goToLine();

    // "line[:column]", from 1 → line and column from 0. nullopt if invalid.
    static std::optional<std::pair<int, int>> parseLineColumn(const QString &text);

    QString positionText() const;
    QString lineEndingText() const;
    QString encodingText() const;
    QString indentationText() const;

private:
    void update();
    // Moving the cursor signals both a cursor and a selection change: the
    // texts are computed once, after both.
    void scheduleUpdate();
    bool updateScheduled_ = false;
    void showLineEndingMenu();
    void showIndentationMenu();
    QToolButton *addButton(const QString &toolTip);

    QPointer<EditorWidget> editor_;
    QToolButton *position_;
    QToolButton *lineEnding_;
    QToolButton *encoding_;
    QToolButton *indentation_;
};
