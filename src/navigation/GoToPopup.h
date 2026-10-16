#pragma once

#include "SymbolIndex.h"
#include "ui/OverlayPopup.h"

#include <QThreadPool>

class QCheckBox;
class QLineEdit;
class QTabBar;
class QTreeWidget;
class RecentFiles;

// One popup for Go to File (Ctrl+Shift+N), Go to Symbol (Ctrl+Alt+Shift+N)
// and Recent Files (Ctrl+E), in the manner of IntelliJ: a tab per list, Tab
// switches from one to the next, typing a few letters filters (same fuzzy
// match as the command palette). "name:42" opens a file at line 42. In
// Recent Files, "Edited by Claude" (or Ctrl+E again) shows the files Claude
// wrote instead of those opened.
class GoToPopup : public OverlayPopup {
    Q_OBJECT

public:
    enum class Tab { Files, Symbols, Recent };

    GoToPopup(SymbolIndex *index, RecentFiles *recent, QWidget *parent);
    ~GoToPopup() override;

    // Working folder: the list of its files is reread at each opening.
    void setFolder(const QString &folder);
    void popup(Tab tab);

    Tab tab() const;
    void setTab(Tab tab);
    QLineEdit *searchField() const { return search_; }
    QTreeWidget *list() const { return list_; }
    QCheckBox *editedByClaude() const { return editedByClaude_; }
    // Lists are complete (the file list has been read).
    bool isReady() const { return !scanning_; }

    // "file.cpp:42" → {"file.cpp", 41}; without a line number, -1.
    static std::pair<QString, int> splitLine(const QString &query);
    // Score of a relative path for Go to File: the file name counts more
    // than the folders; a query with a "/" also matches the folders. -1 if
    // it does not match.
    static int fileScore(const QString &relativePath, const QString &query);

signals:
    // `line` from 0, -1 to keep the file's position.
    void fileChosen(const QString &path, int line);
    void symbolChosen(const SymbolLocation &symbol);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        QString title;
        QString location;
        QString filePath;
        int symbol = -1; // in symbols_
    };

    void scanFiles();
    void filter();
    void filterFiles(const QString &query, QList<Entry> &entries) const;
    void filterSymbols(const QString &query, QList<Entry> &entries) const;
    void filterRecent(const QString &query, QList<Entry> &entries) const;
    void openCurrent();
    QString relativePath(const QString &path) const;

    SymbolIndex *index_;
    RecentFiles *recent_;
    QString folder_;
    QStringList files_; // relative paths
    QList<SymbolLocation> symbols_;
    QList<Entry> shown_;
    bool scanning_ = false;
    int generation_ = 0;
    QThreadPool pool_;

    QTabBar *tabs_;
    QCheckBox *editedByClaude_;
    QLineEdit *search_;
    QTreeWidget *list_;
};
