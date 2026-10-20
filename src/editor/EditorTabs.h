// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CodeFormatter.h"
#include "EditorWidget.h"
#include "FileWatcher.h"
#include "PreviewTab.h"
#include "ui/Theme.h"

#include <QHash>
#include <QSet>
#include <QTabWidget>

#include <optional>

class DiagramView;
class ImageView;
class TabBarBaseLine;

// Tabs for open files (and displayed diffs). Reloads files modified on
// disk — by Claude in particular — and asks what to do if the user had
// unsaved changes.
class EditorTabs : public QTabWidget {
    Q_OBJECT

public:
    explicit EditorTabs(Theme theme, QWidget *parent = nullptr);

    // Opens a text file in an editor (a Markdown or HTML file with its preview), an
    // image in a viewer, or a diagram (Mermaid, PlantUML…) in the diagram editor. Returns
    // the tab, or nullptr on failure.
    QWidget *openFile(const QString &path);
    // Shows the file in the preview tab (see PreviewTab), in place of the
    // previous preview, leaving the focus where it is; a file already open
    // is only brought to the front. Returns the tab, or nullptr on failure.
    QWidget *openPreview(const QString &path);
    void showDiff(const QString &title, const QString &diffText);
    // A tab for `page`, which shows no file (Claude drawing a diagram…).
    void showPage(QWidget *page, const QString &title, const QString &iconName);
    // Replaces `page` with the file at `path`, at the same place.
    void replacePage(QWidget *page, const QString &path);
    void closePage(QWidget *page);
    // A read-only text in the tab named `title`, opened or, if it is open,
    // updated in place; `iconName` is a UiIcons name.
    EditorWidget *showText(const QString &title,
                           const QString &text,
                           const QString &languageId,
                           const QString &iconName);
    // The tab showText() opened under `title`, or nullptr.
    EditorWidget *textView(const QString &title) const;

    EditorWidget *currentEditor() const;
    // The text editors of every tab.
    QList<EditorWidget *> editors() const;
    // Text editor of a tab: the tab itself, or the editor of a Markdown or HTML tab.
    static EditorWidget *editorIn(QWidget *page);
    // Tab that holds `editor`.
    QWidget *pageOf(EditorWidget *editor) const;
    // A Markdown or HTML tab in Preview mode shows its editor too: to display a
    // position or a search in it.
    void revealEditor(EditorWidget *editor) const;
    // A Markdown tab in Preview mode scrolls its preview to the heading of
    // the caret of `editor`, which it hides.
    void scrollPreviewToCaret(EditorWidget *editor) const;
    EditorWidget *editorFor(const QString &path) const;

    // The open project: the HTML pages opened next can load its files.
    void setProjectFolder(const QString &folder) { projectFolder_ = folder; }

    ImageView *imageViewFor(const QString &path) const;
    DiagramView *diagramViewFor(const QString &path) const;

    // Paths of open files, in tab order (excluding diffs).
    QStringList openFilePaths() const;
    QString currentFilePath() const;
    // Text of the files with unsaved changes, by path.
    QHash<QString, QString> unsavedTexts() const;

    // Display settings of every editor, open now or later.
    void setEditorOptions(const EditorOptions &options);

    const EditorOptions &editorOptions() const { return editorOptions_; }

    // Formats the current editor with its language's formatter (see
    // CodeFormatter), without blocking; the result is reported by
    // statusMessage.
    void formatCurrent();

    // Formats each text file before saving it.
    void setFormatOnSave(bool enabled) { formatOnSave_ = enabled; }

    bool formatOnSave() const { return formatOnSave_; }

    // With format on save, a file is written once its formatter has
    // finished: true then only means the save has started.
    bool saveCurrent();
    bool saveAll();
    void closeCurrent();
    // Closes all tabs, asking for confirmation if there are unsaved changes.
    bool closeAll();

    // Call when a file may have changed on disk.
    void syncWithDisk(const QString &path);
    // Rereads the git index versions the change marks compare with.
    void reloadChangeBases() const;

    // Whether a file open under `path` (the file itself, or one in that
    // folder) has unsaved changes.
    bool hasUnsavedChangesUnder(const QString &path) const;
    // `path` was deleted: closes the tabs of the files under it, discarding
    // their unsaved changes.
    void closeFilesUnder(const QString &path);
    // `oldPath` was renamed `newPath`: the tabs of the files under it are
    // reopened from the new path, at the same place, with the same cursor
    // position. They must have no unsaved changes.
    void followRename(const QString &oldPath, const QString &newPath);

    void applyTheme(const Theme &theme);

signals:
    void statusMessage(const QString &message);
    void fileSaved(const QString &path);
    // Ctrl+click in the current editor.
    void definitionRequested();
    // A file was opened in a new text editor (not emitted for diffs).
    void editorOpened(EditorWidget *editor);
    // A tab was opened or closed: openFilePaths() may have changed.
    void openFilesChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

private:
    // Duet's mark, centered, when no file is open.
    void paintEmptyMark();
    // Adds a tab, before the preview's.
    int addPage(QWidget *page, const QString &title);
    // The tab showing the file at `absolutePath`, null if none.
    QWidget *pageFor(const QString &absolutePath) const;
    QWidget *openImage(const QString &absolutePath);
    // The diagram editor's tab; null, with `error`, if Duet cannot read the
    // diagram (the file then opens as text).
    QWidget *openDiagram(const QString &absolutePath, QString *error);
    bool saveDiagram(DiagramView *view);
    void syncDiagramWithDisk(DiagramView *diagram, const QString &path);
    void updateDiagramTabTitle(DiagramView *view);
    QList<DiagramView *> diagramViews() const;
    void syncEditorWithDisk(EditorWidget *editor, const QString &path);
    // Asks whether to reload a file modified both in its tab and on disk.
    bool confirmReload(const QString &filePath, const QString &question);
    bool closeTab(int index, bool askToSave = true);
    // Path of the file shown in `page`, empty for a diff.
    QString filePathOf(QWidget *page) const;
    // Pages of the files under `path`.
    QList<QWidget *> pagesUnder(const QString &path) const;
    // Closes the given pages, stopping if the user cancels.
    bool closeTabs(const QList<QWidget *> &pages);
    void showTabContextMenu(const QPoint &pos);
    // Saves now, formatting first (blocking) with format on save: for the
    // "Save changes?" question, which needs the outcome.
    bool saveEditor(EditorWidget *editor);
    // Saves soon: formats first, off the UI thread, with format on save.
    bool startSaving(EditorWidget *editor);
    bool writeEditor(EditorWidget *editor);
    // `quiet`: no message when the file has no installed formatter or is
    // already formatted (format on save). `saveAfter`: writes the file
    // once formatted, whatever the outcome.
    void formatEditor(EditorWidget *editor, bool quiet, bool saveAfter = false);
    std::optional<CodeFormatter::Command> formatterFor(EditorWidget *editor, bool quiet);
    void applyFormatted(EditorWidget *editor,
                        const std::optional<QString> &formatted,
                        const QString &error,
                        bool quiet);
    // Indents `editor` as clang-format lays its file out, when clang-format
    // is its formatter.
    void applyFormatterStyle(EditorWidget *editor);
    void updateTabTitle(EditorWidget *editor);
    void updateTabIcon(QWidget *page);

    Theme theme_;
    TabBarBaseLine *baseLine_ = nullptr; // over the tab bar, which owns it
    QString projectFolder_;
    FileWatcher watcher_;
    QSet<QString> pathsBeingResolved_; // conflicts awaiting an answer
    bool formatOnSave_ = false;
    // Editors whose formatter is running, and whether to save them after.
    QHash<EditorWidget *, bool> formatting_;
    EditorOptions editorOptions_;
    PreviewTab preview_{this};
};
