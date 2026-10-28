// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: what follows the current editor (find, go to, change marks,
// language servers).

#include "MainWindow.h"
#include "agent/EditorContext.h"
#include "agent/mcp/KDKTools.h"
#include "diagnostics/ProblemStore.h"
#include "editor/ChangeGutter.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "editor/MergeConflicts.h"
#include "lsp/LanguageServers.h"
#include "navigation/GoToPopup.h"
#include "search/FindInFilesPopup.h"
#include "ui/NoticeBar.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"
#include "workspace/SidePanel.h"

#include <QStatusBar>

void MainWindow::onCurrentTabChanged()
{
    EditorWidget *editor = editorTabs_->currentEditor();
    findBar_->setEditor(editor);
    if (!editor && findBar_->isVisible())
        findBar_->dismiss();
    editorStatus_->setEditor(mode_ == Mode::Edit ? editor : nullptr);
    const QString path = editorTabs_->currentFilePath();
    // Qt reports the current tab again when a tab before it closes or moves:
    // the tree then stays where the user left it.
    const bool isAnotherFile = path != currentTabPath_;
    currentTabPath_ = path;
    recentFiles_.touch(path);
    // Looked at: the file loses its dot.
    if (claudeTouched_.remove(path))
        updateClaudeMarks();
    if (filesPanel_->alwaysSelectOpenedFile() && isAnotherFile && !path.isEmpty())
        fileTree_->select(path);
    updateServerNotice();
}

void MainWindow::connectTreeToEditor()
{
    connect(fileTree_, &FileTreeView::filePreviewRequested, editorTabs_, &EditorTabs::openPreview);
    connect(editorTabs_, &EditorTabs::openFilesChanged, this, [this] {
        const QStringList paths = editorTabs_->openFilePaths();
        fileTree_->setOpenFiles(QSet<QString>(paths.cbegin(), paths.cend()));
    });
    connect(problemStore_, &ProblemStore::changed, this, &MainWindow::updateTreeProblems);
}

void MainWindow::updateTreeProblems()
{
    ProblemCounts counts;
    for (const QString &path : problemStore_->files()) {
        ProblemCount &count = counts[path];
        for (const Problem &problem : problemStore_->problems(path)) {
            if (problem.severity == Diagnostic::Severity::Error)
                ++count.errors;
            else
                ++count.warnings;
        }
    }
    fileTree_->setProblems(counts);
}

void MainWindow::warmUpLanguageServer(EditorWidget *editor)
{
    languageServers_->warmUp(editor->filePath(), editor->languageId(), editor->utf8Bytes().toByteArray());
}

void MainWindow::warmUpLanguageServers()
{
    for (EditorWidget *editor : editorTabs_->editors())
        warmUpLanguageServer(editor);
}

void MainWindow::updateServerNotice()
{
    const EditorWidget *editor = editorTabs_->currentEditor();
    const QString failure = editor ? languageServers_->failure(editor->languageId()) : QString();
    if (failure.isEmpty() || dismissedServerNotices_.contains(failure)) {
        serverNotice_->hide();
        return;
    }
    serverNotice_->showNotice(tr("Completion is limited to the words of the project: %1").arg(failure), tr("Retry"));
}

void MainWindow::selectOpenedFile()
{
    const QString path = editorTabs_->currentFilePath();
    if (path.isEmpty())
        return;
    setMode(Mode::Edit);
    sidebar_->showView(filesPanel_);
    if (!fileTree_->select(path)) {
        // Outside the scope: back to the whole project, as IntelliJ does.
        filesPanel_->setScopeIndex(0);
        if (!fileTree_->select(path)) {
            showStatus(tr("The current file is not in the project folder"));
            return;
        }
    }
    fileTree_->setFocus();
}

void MainWindow::showFind(bool withReplace)
{
    if (mode_ != Mode::Edit || !editorTabs_->currentEditor())
        return;
    editorTabs_->revealEditor(editorTabs_->currentEditor());
    if (withReplace)
        findBar_->showReplace();
    else
        findBar_->showFind();
}

void MainWindow::showGoTo(int tab)
{
    goToPopup_->popup(static_cast<GoToPopup::Tab>(tab));
}

void MainWindow::showFindInFiles()
{
    const EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    findInFiles_->popup(editor && editor->hasSelectedText() ? editor->selectedText() : QString());
}

void MainWindow::goToChange(bool forward)
{
    EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (!editor)
        return;
    editorTabs_->revealEditor(editor);
    ChangeGutter *gutter = editor->changeGutter();
    if (!(forward ? gutter->goToNext() : gutter->goToPrevious()))
        showStatus(gutter->hasBase() ? tr("No changes in this file") : tr("This file is not tracked by git"));
}

void MainWindow::goToConflict(MergeConflicts::Direction direction)
{
    EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (!editor)
        return;
    editorTabs_->revealEditor(editor);
    if (!editor->mergeConflicts()->goTo(direction))
        showStatus(tr("No merge conflicts in this file"));
}

void MainWindow::rollbackChange()
{
    const EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (editor && !editor->changeGutter()->rollbackAtCursor())
        showStatus(tr("No change on this line"));
}
