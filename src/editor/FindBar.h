#pragma once

#include "ui/Theme.h"

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <optional>

class EditorWidget;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QToolButton;

// Find (Ctrl+F) and Replace (Ctrl+R) bar of the current editor, in the
// manner of IntelliJ: searches as you type, highlights every match and
// shows "3/12". Options: match case, whole words, regular expression.
// Enter or F3: next match; Shift+Enter or Shift+F3: previous; Escape closes
// the bar and gives the focus back to the editor.
class FindBar : public QWidget {
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    // Editor searched; nullptr when the current tab is not a text editor.
    void setEditor(EditorWidget *editor);

    // Opens the bar, the search field filled with the selection (if it holds
    // on one line) and selected.
    void showFind();
    void showReplace();
    // Hides the bar, its highlights included, and gives the focus back to the
    // editor.
    void dismiss();

    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAll();

    void setOptions(bool matchCase, bool wholeWords, bool regularExpression);
    void applyTheme(const Theme &theme);

    QLineEdit *searchField() const { return search_; }

    QLineEdit *replaceField() const { return replace_; }

    int matchCount() const { return static_cast<int>(matches_.size()); }

    // Number of the selected match, from 1; 0 if the selection is not a match.
    int currentMatch() const;

    bool isPatternValid() const { return valid_; }

    struct Match {
        long start;
        long end;
    };

    struct Query {
        QString text;
        int flags = 0; // SCFIND_*
        int limit = 0; // most matches returned
    };

    // Matches of `query` in the editor; nullopt for an invalid regular
    // expression.
    static std::optional<QList<Match>> findAll(EditorWidget *editor, const Query &query);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void open(bool withReplace);
    int searchFlags() const;
    // Rereads the matches and their highlighting.
    void refresh();
    void scheduleRefresh();
    // Selects the first match from the cursor (or the previous one).
    void selectFrom(long position, bool forward);
    void select(const Match &match);
    void updateCount();
    void clearHighlights(EditorWidget *editor);
    QToolButton *addOption(const QString &text, const QString &toolTip);
    // Keys of the search and replace fields; true if handled.
    bool handleFieldKey(const QObject *field, const QKeyEvent &key);
    QToolButton *optionForKey(int key) const;

    QPointer<EditorWidget> editor_;
    Theme theme_;
    QLineEdit *search_;
    QLineEdit *replace_;
    QWidget *replaceRow_;
    QToolButton *matchCase_;
    QToolButton *wholeWords_;
    QToolButton *regularExpression_;
    QLabel *count_;
    QList<Match> matches_;
    bool valid_ = true;
    QTimer refreshTimer_;
};
