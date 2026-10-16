#pragma once

#include "CodeFormatter.h"
#include "EditorWidget.h"
#include "FileWatcher.h"
#include "ui/Theme.h"

#include <QHash>
#include <QSet>
#include <QTabWidget>

#include <optional>

class DiagramView;
class ImageView;

// Tabs for open files (and displayed diffs). Reloads files modified on
// disk — by Claude in particular — and asks what to do if the user had
// unsaved changes.
class EditorTabs : public QTabWidget {
    Q_OBJECT

public:
    explicit EditorTabs(const Theme &theme, QWidget *parent = nullptr);

    // Opens a text file in an editor (a Markdown file with its preview), an
    // image in a viewer, or a diagram (.flow) in the diagram editor. Returns
    // the tab, or nullptr on failure.
    QWidget *openFile(const QString &path);
    void showDiff(const QString &title, const QString &diffText);

    EditorWidget *currentEditor() const;
    // Text editor of a tab: the tab itself, or the editor of a Markdown tab.
    static EditorWidget *editorIn(QWidget *page);
    // Tab that holds `editor`.
    QWidget *pageOf(EditorWidget *editor) const;
    // A Markdown tab in Preview mode shows its editor too: to display a
    // position or a search in it.
    void revealEditor(EditorWidget *editor);
    EditorWidget *editorFor(const QString &path) const;
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
    void reloadChangeBases();

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

private:
    QWidget *openImage(const QString &absolutePath);
    QWidget *openDiagram(const QString &absolutePath);
    bool saveDiagram(DiagramView *view);
    void updateDiagramTabTitle(DiagramView *view);
    QList<DiagramView *> diagramViews() const;
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
    void applyFormatted(EditorWidget *editor, const std::optional<QString> &formatted, const QString &error,
                        bool quiet);
    void updateTabTitle(EditorWidget *editor);
    void updateTabIcon(QWidget *page);
    QList<EditorWidget *> editors() const;

    Theme theme_;
    FileWatcher watcher_;
    QSet<QString> pathsBeingResolved_; // conflicts awaiting an answer
    bool formatOnSave_ = false;
    // Editors whose formatter is running, and whether to save them after.
    QHash<EditorWidget *, bool> formatting_;
    EditorOptions editorOptions_;
};
