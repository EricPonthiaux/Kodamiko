// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FindInFiles.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

#include <QTimer>

#include <functional>

class EditorWidget;
class QCheckBox;
class QKeyEvent;
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
    explicit FindInFilesPopup(Theme theme, QWidget *parent);

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

public:
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *createFilters();
    QWidget *createResultsPane();
    QWidget *createPreviewPane();
    void setEmptyMessage(const QString &text);
    void scheduleSearch();
    void showResults(const QList<FindMatch> &matches, bool truncated, int filesSearched);
    QString summaryText(bool truncated, qsizetype fileCount) const;
    bool handleSearchFieldKey(const QKeyEvent &key);
    QToolButton *optionForShortcut(int key) const;
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

    QLabel *summary_ = nullptr;
    QLabel *empty_ = nullptr;
    QWidget *previewPane_ = nullptr;
    QLabel *previewLocation_ = nullptr;
    QLineEdit *search_ = nullptr;
    QToolButton *matchCase_ = nullptr;
    QToolButton *wholeWords_ = nullptr;
    QToolButton *regularExpression_ = nullptr;
    QCheckBox *useMask_ = nullptr;
    QLineEdit *mask_ = nullptr;
    QTreeWidget *results_ = nullptr;
    EditorWidget *preview_ = nullptr;
    QString previewPath_;
};
