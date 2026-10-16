#pragma once

#include "FindInFiles.h"
#include "syntax/Theme.h"
#include "workspace/OverlayPopup.h"

#include <QTimer>

#include <functional>

class EditorWidget;
class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;

// Find in Files (Ctrl+Shift+F), in the manner of IntelliJ's popup: searches
// the project as you type, lists the matching lines with their file, and
// previews the selected one below. Enter opens it in the editor.
class FindInFilesPopup : public OverlayPopup {
    Q_OBJECT

public:
    explicit FindInFilesPopup(const Theme &theme, QWidget *parent);

    void setFolder(const QString &folder);
    // Text of the files open with unsaved changes, read at each search.
    void setBufferProvider(std::function<QHash<QString, QString>()> provider);
    // Opens the popup; `text` (the editor's selection) replaces the query
    // if it is not empty.
    void popup(const QString &text = {});
    void applyTheme(const Theme &theme);

    QLineEdit *searchField() const { return search_; }
    QLineEdit *maskField() const { return mask_; }
    QTreeWidget *results() const { return results_; }
    void setOptions(bool matchCase, bool wholeWords, bool regularExpression);
    const QList<FindMatch> &matches() const { return matches_; }
    bool isSearching() const { return engine_.isRunning(); }
    // Runs the search now instead of after the typing pause.
    void searchNow();

signals:
    // `line` and `column` from 0, in characters.
    void matchChosen(const QString &filePath, int line, int column, int length);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void scheduleSearch();
    void showResults(const QList<FindMatch> &matches, bool truncated, int filesSearched);
    void updatePreview();
    void fitLocationColumn();
    void openCurrent();
    FindQuery query() const;
    QToolButton *addOption(const QString &text, const QString &toolTip);

    Theme theme_;
    QString folder_;
    std::function<QHash<QString, QString>()> buffers_;
    FindInFilesSearch engine_;
    QList<FindMatch> matches_;
    QTimer searchTimer_;

    QLabel *summary_;
    QLineEdit *search_;
    QToolButton *matchCase_;
    QToolButton *wholeWords_;
    QToolButton *regularExpression_;
    QCheckBox *useMask_;
    QLineEdit *mask_;
    QTreeWidget *results_;
    EditorWidget *preview_;
    QString previewPath_;
};
