#include "EditorTabs.h"

#include "ChangeGutter.h"
#include "CodeFormatter.h"
#include "completion/CompletionController.h"
#include "EditorWidget.h"
#include "ui/UiIcons.h"
#include "viewers/ImageView.h"
#include "viewers/MarkdownView.h"
#include "workspace/FileIcons.h"
#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramView.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QTabBar>

EditorTabs::EditorTabs(const Theme &theme, QWidget *parent)
    : QTabWidget(parent)
    , theme_(theme)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QWidget::customContextMenuRequested, this, &EditorTabs::showTabContextMenu);
    connect(&watcher_, &FileWatcher::fileChanged, this, &EditorTabs::syncWithDisk);
}

QWidget *EditorTabs::openFile(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (EditorWidget *existing = editorFor(absolutePath)) {
        setCurrentWidget(pageOf(existing));
        return pageOf(existing);
    }
    if (ImageView *existing = imageViewFor(absolutePath)) {
        setCurrentWidget(existing);
        return existing;
    }
    if (ImageView::isImageFile(absolutePath))
        return openImage(absolutePath);
#ifdef DUET_WITH_DIAGRAMS
    if (DiagramView *existing = diagramViewFor(absolutePath)) {
        setCurrentWidget(existing);
        return existing;
    }
    if (DiagramView::isDiagramFile(absolutePath))
        return openDiagram(absolutePath);
#endif

    auto *editor = new EditorWidget(theme_, this);
    editor->setOptions(editorOptions_);
    attachCompletion(editor);
    QString error;
    if (!editor->load(absolutePath, &error)) {
        delete editor;
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Could not open %1:\n%2").arg(absolutePath, error));
        return nullptr;
    }

    // A Markdown file opens with its preview.
    QWidget *page = editor;
    if (MarkdownPage::isMarkdownFile(absolutePath)) {
        auto *markdown = new MarkdownPage(editor, theme_, this);
        connect(markdown, &MarkdownPage::openFileRequested, this, [this](const QString &path) { openFile(path); });
        page = markdown;
    }
    const int index = addTab(page, QFileInfo(absolutePath).fileName());
    updateTabIcon(page);
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(editor, &EditorWidget::modificationChanged, this, [this, editor] { updateTabTitle(editor); });
    connect(editor, &EditorWidget::definitionRequested, this, &EditorTabs::definitionRequested);
    connect(editor->changeGutter(), &ChangeGutter::diffRequested, this, &EditorTabs::showDiff);
    watcher_.watch(absolutePath);
    page->setFocus();
    return page;
}

QWidget *EditorTabs::openImage(const QString &absolutePath)
{
    auto *view = new ImageView(theme_, this);
    QString error;
    if (!view->load(absolutePath, &error)) {
        delete view;
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Could not open %1:\n%2").arg(absolutePath, error));
        return nullptr;
    }

    const int index = addTab(view, QFileInfo(absolutePath).fileName());
    updateTabIcon(view);
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(view, &ImageView::zoomChanged, this, [this, view] {
        if (currentWidget() == view)
            emit statusMessage(view->description());
    });
    emit statusMessage(view->description());
    watcher_.watch(absolutePath);
    view->setFocus();
    return view;
}

QWidget *EditorTabs::openDiagram(const QString &absolutePath)
{
#ifdef DUET_WITH_DIAGRAMS
    auto *view = new DiagramView(theme_, this);
    QString error;
    if (!view->load(absolutePath, &error)) {
        delete view;
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Could not open %1:\n%2").arg(absolutePath, error));
        return nullptr;
    }

    const int index = addTab(view, QFileInfo(absolutePath).fileName());
    updateTabIcon(view);
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(view, &DiagramView::modificationChanged, this, [this, view] { updateDiagramTabTitle(view); });
    watcher_.watch(absolutePath);
    view->setFocus();
    return view;
#else
    Q_UNUSED(absolutePath);
    return nullptr;
#endif
}

void EditorTabs::showDiff(const QString &title, const QString &diffText)
{
    auto *view = new EditorWidget(theme_, this);
    view->setOptions(editorOptions_);
    view->showReadOnlyText(diffText.isEmpty() ? tr("(no textual differences)") : diffText,
                           QStringLiteral("diff"));
    const int index = addTab(view, title);
    updateTabIcon(view);
    setCurrentIndex(index);
}

EditorWidget *EditorTabs::currentEditor() const
{
    return editorIn(currentWidget());
}

EditorWidget *EditorTabs::editorIn(QWidget *page)
{
    if (auto *markdown = qobject_cast<MarkdownPage *>(page))
        return markdown->editor();
    return qobject_cast<EditorWidget *>(page);
}

QWidget *EditorTabs::pageOf(EditorWidget *editor) const
{
    for (int i = 0; i < count(); ++i) {
        if (editorIn(widget(i)) == editor)
            return widget(i);
    }
    return nullptr;
}

void EditorTabs::revealEditor(EditorWidget *editor)
{
    if (auto *markdown = qobject_cast<MarkdownPage *>(pageOf(editor)))
        markdown->revealEditor();
}

EditorWidget *EditorTabs::editorFor(const QString &path) const
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (EditorWidget *editor : editors()) {
        if (editor->filePath() == absolutePath)
            return editor;
    }
    return nullptr;
}

ImageView *EditorTabs::imageViewFor(const QString &path) const
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < count(); ++i) {
        auto *view = qobject_cast<ImageView *>(widget(i));
        if (view && view->filePath() == absolutePath)
            return view;
    }
    return nullptr;
}

DiagramView *EditorTabs::diagramViewFor(const QString &path) const
{
#ifdef DUET_WITH_DIAGRAMS
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (DiagramView *view : diagramViews()) {
        if (view->filePath() == absolutePath)
            return view;
    }
#else
    Q_UNUSED(path);
#endif
    return nullptr;
}

QList<DiagramView *> EditorTabs::diagramViews() const
{
    QList<DiagramView *> result;
#ifdef DUET_WITH_DIAGRAMS
    for (int i = 0; i < count(); ++i) {
        if (auto *view = qobject_cast<DiagramView *>(widget(i)))
            result.append(view);
    }
#endif
    return result;
}

QString EditorTabs::filePathOf(QWidget *page) const
{
    if (auto *editor = editorIn(page))
        return editor->filePath();
    if (auto *image = qobject_cast<ImageView *>(page))
        return image->filePath();
#ifdef DUET_WITH_DIAGRAMS
    if (auto *diagram = qobject_cast<DiagramView *>(page))
        return diagram->filePath();
#endif
    return {};
}

QList<QWidget *> EditorTabs::pagesUnder(const QString &path) const
{
    const QString absolutePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QList<QWidget *> pages;
    for (int i = 0; i < count(); ++i) {
        const QString filePath = filePathOf(widget(i));
        if (!filePath.isEmpty()
            && (filePath == absolutePath || filePath.startsWith(absolutePath + QLatin1Char('/'))))
            pages.append(widget(i));
    }
    return pages;
}

bool EditorTabs::hasUnsavedChangesUnder(const QString &path) const
{
    for (QWidget *page : pagesUnder(path)) {
        if (auto *editor = editorIn(page); editor && editor->isModified())
            return true;
#ifdef DUET_WITH_DIAGRAMS
        if (auto *diagram = qobject_cast<DiagramView *>(page); diagram && diagram->isModified())
            return true;
#endif
    }
    return false;
}

void EditorTabs::closeFilesUnder(const QString &path)
{
    for (QWidget *page : pagesUnder(path))
        closeTab(indexOf(page), false);
}

void EditorTabs::followRename(const QString &oldPath, const QString &newPath)
{
    const QString oldAbsolute = QDir::cleanPath(QFileInfo(oldPath).absoluteFilePath());
    const QString newAbsolute = QDir::cleanPath(QFileInfo(newPath).absoluteFilePath());
    QWidget *current = currentWidget();
    QString currentPath;
    for (QWidget *page : pagesUnder(oldAbsolute)) {
        const int index = indexOf(page);
        const QString renamed = newAbsolute + filePathOf(page).mid(oldAbsolute.size());
        int line = 0, column = 0, firstLine = 0;
        auto *editor = editorIn(page);
        if (editor) {
            editor->getCursorPosition(&line, &column);
            firstLine = editor->firstVisibleLine();
        }
        if (page == current)
            currentPath = renamed;
        closeTab(index, false);
        QWidget *reopened = openFile(renamed);
        if (!reopened)
            continue;
        tabBar()->moveTab(indexOf(reopened), index);
        if (auto *reopenedEditor = editorIn(reopened); reopenedEditor && editor) {
            reopenedEditor->setCursorPosition(line, column);
            reopenedEditor->setFirstVisibleLine(firstLine);
        }
    }
    if (!currentPath.isEmpty())
        openFile(currentPath); // selects it
    else if (current && indexOf(current) >= 0)
        setCurrentWidget(current);
}

QStringList EditorTabs::openFilePaths() const
{
    QStringList paths;
    for (int i = 0; i < count(); ++i) {
        if (const QString path = filePathOf(widget(i)); !path.isEmpty())
            paths.append(path);
    }
    return paths;
}

QString EditorTabs::currentFilePath() const
{
    if (auto *editor = editorIn(currentWidget()))
        return editor->filePath();
    if (auto *image = qobject_cast<ImageView *>(currentWidget()))
        return image->filePath();
#ifdef DUET_WITH_DIAGRAMS
    if (auto *diagram = qobject_cast<DiagramView *>(currentWidget()))
        return diagram->filePath();
#endif
    return {};
}

QHash<QString, QString> EditorTabs::unsavedTexts() const
{
    QHash<QString, QString> texts;
    for (EditorWidget *editor : editors()) {
        if (editor->isModified() && !editor->filePath().isEmpty())
            texts.insert(editor->filePath(), editor->text());
    }
    return texts;
}

QList<EditorWidget *> EditorTabs::editors() const
{
    QList<EditorWidget *> result;
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = editorIn(widget(i)))
            result.append(editor);
    }
    return result;
}

bool EditorTabs::saveCurrent()
{
#ifdef DUET_WITH_DIAGRAMS
    if (auto *diagram = qobject_cast<DiagramView *>(currentWidget()))
        return saveDiagram(diagram);
#endif
    EditorWidget *editor = currentEditor();
    return !editor || saveEditor(editor);
}

bool EditorTabs::saveAll()
{
    bool allSaved = true;
    for (EditorWidget *editor : editors()) {
        if (editor->isModified())
            allSaved = saveEditor(editor) && allSaved;
    }
#ifdef DUET_WITH_DIAGRAMS
    for (DiagramView *view : diagramViews()) {
        if (view->isModified())
            allSaved = saveDiagram(view) && allSaved;
    }
#endif
    return allSaved;
}

void EditorTabs::setEditorOptions(const EditorOptions &options)
{
    editorOptions_ = options;
    for (EditorWidget *editor : editors()) {
        editor->setOptions(options);
        if (CompletionController *completion = CompletionController::of(editor))
            completion->setAutoPopup(options.autoCompletion);
    }
}

void EditorTabs::setCompletionService(CompletionService *service)
{
    completion_ = service;
    for (EditorWidget *editor : editors()) {
        if (!editor->isReadOnly())
            attachCompletion(editor);
    }
}

void EditorTabs::attachCompletion(EditorWidget *editor)
{
    if (!completion_ || CompletionController::of(editor))
        return;
    auto *completion = new CompletionController(editor, completion_);
    completion->setAutoPopup(editorOptions_.autoCompletion);
}

void EditorTabs::formatCurrent()
{
    if (EditorWidget *editor = currentEditor())
        formatEditor(editor, false);
}

void EditorTabs::formatEditor(EditorWidget *editor, bool quiet)
{
    if (editor->isReadOnly() || editor->filePath().isEmpty())
        return;
    const QString path = editor->filePath();
    const QString name = QFileInfo(path).fileName();
    const std::optional<CodeFormatter::Command> command = CodeFormatter::find(editor->languageId(), path);
    if (!command) {
        if (quiet)
            return;
        const QString candidates = CodeFormatter::describeCandidates(editor->languageId(), path);
        emit statusMessage(candidates.isEmpty()
                               ? tr("No formatter for %1").arg(name)
                               : tr("Cannot format %1: install %2").arg(name, candidates));
        return;
    }

    QString error;
    const std::optional<QString> formatted =
        CodeFormatter::run(*command, QFileInfo(path).absolutePath(), editor->text(), &error);
    if (!formatted) {
        emit statusMessage(tr("Could not format %1: %2").arg(name, error));
        return;
    }
    const bool changed = editor->applyFormattedText(*formatted);
    if (!quiet)
        emit statusMessage(changed ? tr("%1 formatted").arg(name) : tr("%1 is already formatted").arg(name));
}

bool EditorTabs::saveEditor(EditorWidget *editor)
{
    if (formatOnSave_)
        formatEditor(editor, true);
    QString error;
    if (!editor->save(&error)) {
        QMessageBox::warning(this, tr("Cannot Save File"),
                             tr("Could not save %1:\n%2").arg(editor->filePath(), error));
        return false;
    }
    emit statusMessage(tr("%1 saved").arg(QFileInfo(editor->filePath()).fileName()));
    emit fileSaved(editor->filePath());
    return true;
}

bool EditorTabs::saveDiagram(DiagramView *view)
{
#ifdef DUET_WITH_DIAGRAMS
    QString error;
    if (!view->save(&error)) {
        QMessageBox::warning(this, tr("Cannot Save File"),
                             tr("Could not save %1:\n%2").arg(view->filePath(), error));
        return false;
    }
    emit statusMessage(tr("%1 saved").arg(QFileInfo(view->filePath()).fileName()));
    emit fileSaved(view->filePath());
    return true;
#else
    Q_UNUSED(view);
    return false;
#endif
}

void EditorTabs::closeCurrent()
{
    if (currentIndex() >= 0)
        closeTab(currentIndex());
}

bool EditorTabs::closeTab(int index, bool askToSave)
{
    auto *editor = editorIn(widget(index));
    if (askToSave && editor && editor->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Unsaved Changes"),
            tr("Save changes to %1?").arg(QFileInfo(editor->filePath()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveEditor(editor)))
            return false;
    }
#ifdef DUET_WITH_DIAGRAMS
    if (auto *diagram = qobject_cast<DiagramView *>(widget(index))) {
        if (askToSave && diagram->isModified()) {
            const auto answer = QMessageBox::question(
                this, tr("Unsaved Changes"),
                tr("Save changes to %1?").arg(QFileInfo(diagram->filePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveDiagram(diagram)))
                return false;
        }
        watcher_.unwatch(diagram->filePath());
    }
#endif
    if (editor && !editor->filePath().isEmpty())
        watcher_.unwatch(editor->filePath());
    if (auto *image = qobject_cast<ImageView *>(widget(index)))
        watcher_.unwatch(image->filePath());
    QWidget *page = widget(index);
    removeTab(index);
    page->deleteLater();
    return true;
}

bool EditorTabs::closeAll()
{
    while (count() > 0) {
        if (!closeTab(count() - 1))
            return false;
    }
    return true;
}

bool EditorTabs::closeTabs(const QList<QWidget *> &pages)
{
    // Indexes shift with each close, so work from the pages.
    for (QWidget *page : pages) {
        const int index = indexOf(page);
        if (index >= 0 && !closeTab(index))
            return false;
    }
    return true;
}

void EditorTabs::showTabContextMenu(const QPoint &pos)
{
    const int clicked = tabBar()->tabAt(pos);
    if (clicked < 0)
        return;
    QWidget *clickedPage = widget(clicked);

    QList<QWidget *> left, right, others;
    for (int i = 0; i < count(); ++i) {
        if (i < clicked)
            left.append(widget(i));
        else if (i > clicked)
            right.append(widget(i));
        if (i != clicked)
            others.append(widget(i));
    }

    QMenu menu(this);
    menu.addAction(tr("Close"), this, [this, clickedPage] { closeTabs({clickedPage}); });
    menu.addAction(tr("Close Other Tabs"), this, [this, others] { closeTabs(others); })
        ->setEnabled(!others.isEmpty());
    menu.addSeparator();
    menu.addAction(tr("Close Tabs to the Left"), this, [this, left] { closeTabs(left); })
        ->setEnabled(!left.isEmpty());
    menu.addAction(tr("Close Tabs to the Right"), this, [this, right] { closeTabs(right); })
        ->setEnabled(!right.isEmpty());
    menu.addSeparator();
    menu.addAction(tr("Close All Tabs"), this, [this] { closeAll(); });
    menu.exec(tabBar()->mapToGlobal(pos));
}

void EditorTabs::syncWithDisk(const QString &path)
{
    if (ImageView *image = imageViewFor(path)) {
        // An image is never modified in the editor: just reload it.
        QString error;
        if (QFileInfo::exists(path) && !image->load(image->filePath(), &error))
            emit statusMessage(tr("Could not reload %1: %2").arg(QFileInfo(path).fileName(), error));
        return;
    }

#ifdef DUET_WITH_DIAGRAMS
    if (DiagramView *diagram = diagramViewFor(path)) {
        const QString name = QFileInfo(path).fileName();
        if (pathsBeingResolved_.contains(diagram->filePath()))
            return;
        if (!QFileInfo::exists(path)) {
            emit statusMessage(tr("%1 was deleted from disk").arg(name));
            return;
        }
        if (diagram->matchesDisk())
            return; // our own save, or identical content
        if (diagram->isModified()) {
            pathsBeingResolved_.insert(diagram->filePath());
            const auto answer = QMessageBox::question(
                this, tr("File Modified on Disk"),
                tr("%1 was modified on disk, but you have unsaved changes.\n\n"
                   "Reload the file? Your changes will be lost.")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No);
            pathsBeingResolved_.remove(diagram->filePath());
            if (answer != QMessageBox::Yes)
                return;
        }
        QString error;
        if (diagram->reloadFromDisk(&error))
            emit statusMessage(tr("%1 reloaded from disk").arg(name));
        else
            emit statusMessage(tr("Could not reload %1: %2").arg(name, error));
        return;
    }
#endif

    EditorWidget *editor = editorFor(path);
    if (!editor || pathsBeingResolved_.contains(editor->filePath()))
        return;
    const QString name = QFileInfo(path).fileName();

    if (!QFileInfo::exists(path)) {
        emit statusMessage(tr("%1 was deleted from disk").arg(name));
        return;
    }
    if (editor->matchesDisk())
        return; // our own save, or identical content

    if (editor->isModified()) {
        pathsBeingResolved_.insert(editor->filePath());
        const auto answer = QMessageBox::question(
            this, tr("File Modified on Disk"),
            tr("%1 was modified on disk, but you have unsaved changes.\n\n"
               "Reload the file? Your changes will be lost (Ctrl+Z brings them back).")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No);
        pathsBeingResolved_.remove(editor->filePath());
        if (answer != QMessageBox::Yes)
            return;
    }

    QString error;
    if (editor->reloadFromDisk(&error))
        emit statusMessage(tr("%1 reloaded from disk").arg(name));
    else
        emit statusMessage(tr("Could not reload %1: %2").arg(name, error));
}

void EditorTabs::reloadChangeBases()
{
    for (EditorWidget *editor : editors()) {
        if (!editor->filePath().isEmpty())
            editor->changeGutter()->reloadBase();
    }
}

void EditorTabs::updateTabTitle(EditorWidget *editor)
{
    const int index = indexOf(pageOf(editor));
    if (index < 0)
        return;
    const QString name = QFileInfo(editor->filePath()).fileName();
    setTabText(index, editor->isModified() ? QStringLiteral("● ") + name : name);
}

void EditorTabs::updateDiagramTabTitle(DiagramView *view)
{
#ifdef DUET_WITH_DIAGRAMS
    const int index = indexOf(view);
    if (index < 0)
        return;
    const QString name = QFileInfo(view->filePath()).fileName();
    setTabText(index, view->isModified() ? QStringLiteral("● ") + name : name);
#else
    Q_UNUSED(view);
#endif
}

// The file's type, as in the tree; a diff shows the diff icon.
void EditorTabs::updateTabIcon(QWidget *page)
{
    const int index = indexOf(page);
    if (index < 0)
        return;
    const QString path = filePathOf(page);
    setTabIcon(index, path.isEmpty() ? UiIcons::icon(QStringLiteral("diff"), theme_.mutedText)
                                     : FileIcons::file(QFileInfo(path).fileName(), theme_.isDark));
}

void EditorTabs::applyTheme(const Theme &theme)
{
    theme_ = theme;
    for (int i = 0; i < count(); ++i)
        updateTabIcon(widget(i));
    for (int i = 0; i < count(); ++i) {
        if (auto *markdown = qobject_cast<MarkdownPage *>(widget(i)))
            markdown->applyTheme(theme);
        else if (auto *editor = qobject_cast<EditorWidget *>(widget(i)))
            editor->applyTheme(theme);
        else if (auto *image = qobject_cast<ImageView *>(widget(i)))
            image->applyTheme(theme);
#ifdef DUET_WITH_DIAGRAMS
        else if (auto *diagram = qobject_cast<DiagramView *>(widget(i)))
            diagram->applyTheme(theme);
#endif
    }
}
