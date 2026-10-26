// SPDX-License-Identifier: GPL-3.0-only

#include "EditorTabs.h"

#include "ChangeGutter.h"
#include "ClangFormatStyle.h"
#include "CodeFormatter.h"
#include "EditorWidget.h"
#include "PreviewPage.h"
#include "ui/BrandMark.h"
#include "ui/ContextMenus.h"
#include "ui/FileIcons.h"
#include "ui/HoverCloseButtons.h"
#include "ui/TabBarBaseLine.h"
#include "ui/UiIcons.h"
#include "viewers/ImageView.h"
#include "workspace/ChoicePopup.h"
#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramView.h"
#endif

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QTabBar>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

// The mark shown when no file is open: a fraction of the area's short side,
// within bounds so it stays a discreet watermark.
constexpr qreal emptyMarkShare = 0.22;
constexpr qreal emptyMarkMinimumSize = 48;
constexpr qreal emptyMarkMaximumSize = 160;
constexpr qreal emptyMarkOpacity = 0.35;

// The bytes of `filePath`; empty if it cannot be read.
QByteArray readFile(const QString &filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Asks whether to save the changes to `filePath` before its tab closes.
// Returns false if the user cancels, or if the requested save fails.
bool confirmClose(QWidget *parent, const QString &filePath, const std::function<bool()> &save)
{
    enum class Answer {
        Cancel,
        Save,
        Discard
    };
    Answer answer = Answer::Cancel;
    auto *popup = new ChoicePopup({.title = EditorTabs::tr("Unsaved Changes"),
                                   .location = QDir::toNativeSeparators(filePath),
                                   .message = EditorTabs::tr("Save changes to %1?").arg(QFileInfo(filePath).fileName()),
                                   .details = {}},
                                  {{EditorTabs::tr("Save"), [&answer] { answer = Answer::Save; }},
                                   {EditorTabs::tr("Discard"), [&answer] { answer = Answer::Discard; }}},
                                  parent);
    popup->exec();
    switch (answer) {
    case Answer::Cancel:
        return false;
    case Answer::Save:
        return save();
    case Answer::Discard:
        return true;
    }
    return false;
}

} // namespace

EditorTabs::EditorTabs(Theme theme, QWidget *parent)
    : QTabWidget(parent)
    , theme_(std::move(theme))
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    tabBar()->setExpanding(false);
    tabBar()->setElideMode(Qt::ElideRight);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QWidget::customContextMenuRequested, this, &EditorTabs::showTabContextMenu);
    connect(&watcher_, &FileWatcher::fileChanged, this, &EditorTabs::syncWithDisk);
    new HoverCloseButtons(tabBar());
    baseLine_ = new TabBarBaseLine(tabBar());
    baseLine_->setColor(theme_.line);
}

void EditorTabs::paintEvent(QPaintEvent *event)
{
    QTabWidget::paintEvent(event);
    if (count() == 0)
        paintEmptyMark();
}

void EditorTabs::paintEmptyMark()
{
    const qreal shortSide = std::min(width(), height());
    const qreal size = std::clamp(shortSide * emptyMarkShare, emptyMarkMinimumSize, emptyMarkMaximumSize);
    QRectF area(0, 0, size, size);
    area.moveCenter(QRectF(rect()).center());

    // A watermark: the caret muted, unlike the other marks' text color.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(emptyMarkOpacity);
    BrandMark::paint(&painter, area, {.caret = theme_.mutedText, .block = theme_.accent});
}

void EditorTabs::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    emit openFilesChanged();
}

void EditorTabs::tabRemoved(int index)
{
    QTabWidget::tabRemoved(index);
    preview_.tabRemoved();
    update(); // the last tab gone, the mark appears
    emit openFilesChanged();
}

int EditorTabs::addPage(QWidget *page, const QString &title)
{
    return insertTab(preview_.insertionIndex(), page, title);
}

QWidget *EditorTabs::pageFor(const QString &absolutePath) const
{
    for (int i = 0; i < count(); ++i) {
        if (filePathOf(widget(i)) == absolutePath)
            return widget(i);
    }
    return nullptr;
}

QWidget *EditorTabs::openPreview(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (QWidget *open = pageFor(absolutePath)) {
        setCurrentWidget(open);
        return open;
    }
    const QPointer<QWidget> previous = preview_.page();
    const QPointer<QWidget> focused = QApplication::focusWidget();
    QWidget *page = openFile(absolutePath);
    if (!page)
        return nullptr;
    // Closed once the new page is current: closing it first would make its
    // neighbor current for a moment, and the file tree, which follows the
    // current tab, would jump to that neighbor's file.
    if (previous)
        closeTab(indexOf(previous)); // kept as soon as edited: nothing to save
    preview_.setPage(page);
    // Edited: kept open.
    if (const EditorWidget *editor = editorIn(page))
        connect(editor, &EditorWidget::modificationChanged, this, [this, page] { preview_.keep(page); });
#ifdef DUET_WITH_DIAGRAMS
    if (const auto *diagram = qobject_cast<DiagramView *>(page))
        connect(diagram, &DiagramView::modificationChanged, this, [this, page] { preview_.keep(page); });
#endif
    // Unless it was in the previous preview, now closed.
    if (focused && focused->isVisible())
        focused->setFocus();
    return page;
}

QWidget *EditorTabs::openFile(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (QWidget *open = pageFor(absolutePath)) {
        // Opened for good: a preview is kept, and takes the focus as a new tab.
        const bool wasPreview = open == preview_.page();
        preview_.keep(open);
        setCurrentWidget(open);
        if (wasPreview)
            open->setFocus();
        return open;
    }
    if (ImageView::isImageFile(absolutePath))
        return openImage(absolutePath);
#ifdef DUET_WITH_DIAGRAMS
    if (DiagramView::isDiagramFile(absolutePath)) {
        // A diagram Duet does not read (a Mermaid pie chart, a source with
        // errors) is still a text file.
        QString error;
        if (QWidget *view = openDiagram(absolutePath, &error))
            return view;
        emit statusMessage(tr("%1 opened as text: %2").arg(QFileInfo(absolutePath).fileName(), error));
    }
#endif

    auto *editor = new EditorWidget(theme_, this);
    editor->setOptions(editorOptions_);
    QString error;
    if (!editor->load(absolutePath, &error)) {
        delete editor;
        showFileError(tr("Cannot Open File"), absolutePath, error);
        return nullptr;
    }
    applyFormatterStyle(editor);
    emit editorOpened(editor);

    // A Markdown or HTML file opens with its preview.
    QWidget *page = editor;
    if (PreviewPage::hasPreview(absolutePath)) {
        auto *previewed = new PreviewPage(editor, theme_, projectFolder_, this);
        connect(previewed, &PreviewPage::openFileRequested, this,
                [this](const QString &linkedPath) { openFile(linkedPath); });
        page = previewed;
    }
    const int index = addPage(page, QFileInfo(absolutePath).fileName());
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
        showFileError(tr("Cannot Open File"), absolutePath, error);
        return nullptr;
    }

    const int index = addPage(view, QFileInfo(absolutePath).fileName());
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

QWidget *EditorTabs::openDiagram(const QString &absolutePath, QString *error)
{
#ifdef DUET_WITH_DIAGRAMS
    auto *view = new DiagramView(theme_, this);
    if (!view->load(absolutePath, error)) {
        delete view;
        return nullptr;
    }

    const int index = addPage(view, QFileInfo(absolutePath).fileName());
    updateTabIcon(view);
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(view, &DiagramView::modificationChanged, this, [this, view] { updateDiagramTabTitle(view); });
    watcher_.watch(absolutePath);
    view->setFocus();
    return view;
#else
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    return nullptr;
#endif
}

void EditorTabs::showDiff(const QString &title, const QString &diffText)
{
    auto *view = new EditorWidget(theme_, this);
    view->setOptions(editorOptions_);
    view->showReadOnlyText(diffText.isEmpty() ? tr("(no textual differences)") : diffText, QStringLiteral("diff"));
    const int index = addPage(view, title);
    updateTabIcon(view);
    setCurrentIndex(index);
}

void EditorTabs::showPage(QWidget *page, const QString &title, const QString &iconName)
{
    page->setProperty("iconName", iconName);
    const int index = addPage(page, title);
    updateTabIcon(page);
    setCurrentIndex(index);
}

void EditorTabs::replacePage(QWidget *page, const QString &path)
{
    const int index = indexOf(page);
    const QWidget *opened = openFile(path);
    closePage(page);
    if (opened && index >= 0)
        tabBar()->moveTab(indexOf(opened), std::min(index, count() - 1));
}

void EditorTabs::closePage(QWidget *page)
{
    if (const int index = indexOf(page); index >= 0)
        closeTab(index, false);
}

EditorWidget *EditorTabs::showText(const QString &title,
                                   const QString &text,
                                   const QString &languageId,
                                   const QString &iconName)
{
    EditorWidget *view = textView(title);
    if (view) {
        const int line = view->firstVisibleLine();
        view->showReadOnlyText(text, languageId);
        view->setFirstVisibleLine(line);
    } else {
        view = new EditorWidget(theme_, this);
        view->setOptions(editorOptions_);
        view->setProperty("textTitle", title);
        view->setProperty("iconName", iconName);
        view->showReadOnlyText(text, languageId);
        addPage(view, title);
        updateTabIcon(view);
    }
    setCurrentWidget(view);
    return view;
}

EditorWidget *EditorTabs::textView(const QString &title) const
{
    for (int i = 0; i < count(); ++i) {
        if (widget(i)->property("textTitle").toString() == title)
            return qobject_cast<EditorWidget *>(widget(i));
    }
    return nullptr;
}

EditorWidget *EditorTabs::currentEditor() const
{
    return editorIn(currentWidget());
}

EditorWidget *EditorTabs::editorIn(QWidget *page)
{
    if (const auto *previewed = qobject_cast<PreviewPage *>(page))
        return previewed->editor();
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

void EditorTabs::revealEditor(EditorWidget *editor) const
{
    if (auto *previewed = qobject_cast<PreviewPage *>(pageOf(editor)))
        previewed->revealEditor();
}

void EditorTabs::scrollPreviewToCaret(EditorWidget *editor) const
{
    if (auto *previewed = qobject_cast<PreviewPage *>(pageOf(editor)))
        previewed->scrollPreviewToCaret();
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
    if (const auto *editor = editorIn(page))
        return editor->filePath();
    if (const auto *image = qobject_cast<ImageView *>(page))
        return image->filePath();
#ifdef DUET_WITH_DIAGRAMS
    if (const auto *diagram = qobject_cast<DiagramView *>(page))
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
        if (!filePath.isEmpty() && (filePath == absolutePath || filePath.startsWith(absolutePath + QLatin1Char('/'))))
            pages.append(widget(i));
    }
    return pages;
}

bool EditorTabs::hasUnsavedChangesUnder(const QString &path) const
{
    return std::ranges::any_of(pagesUnder(path), [](QWidget *page) {
        if (const auto *editor = editorIn(page); editor && editor->isModified())
            return true;
#ifdef DUET_WITH_DIAGRAMS
        if (const auto *diagram = qobject_cast<DiagramView *>(page); diagram && diagram->isModified())
            return true;
#endif
        return false;
    });
}

void EditorTabs::closeFilesUnder(const QString &path)
{
    for (const QWidget *page : withCurrentLast(pagesUnder(path)))
        closeTab(indexOf(page), false);
}

QList<QWidget *> EditorTabs::withCurrentLast(QList<QWidget *> pages) const
{
    if (pages.removeOne(currentWidget()))
        pages.append(currentWidget());
    return pages;
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
        int line = 0;
        int column = 0;
        int firstLine = 0;
        const auto *editor = editorIn(page);
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
    if (const auto *editor = editorIn(currentWidget()))
        return editor->filePath();
    if (const auto *image = qobject_cast<ImageView *>(currentWidget()))
        return image->filePath();
#ifdef DUET_WITH_DIAGRAMS
    if (const auto *diagram = qobject_cast<DiagramView *>(currentWidget()))
        return diagram->filePath();
#endif
    return {};
}

QHash<QString, QString> EditorTabs::unsavedTexts() const
{
    QHash<QString, QString> texts;
    for (const EditorWidget *editor : editors()) {
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
    return !editor || startSaving(editor);
}

bool EditorTabs::saveAll()
{
    return saveModified([this](EditorWidget *editor) { return startSaving(editor); });
}

bool EditorTabs::saveAllNow()
{
    return saveModified([this](EditorWidget *editor) { return saveEditor(editor); });
}

bool EditorTabs::saveModified(const std::function<bool(EditorWidget *)> &save)
{
    bool allSaved = true;
    for (EditorWidget *editor : editors()) {
        if (editor->isModified())
            allSaved = save(editor) && allSaved;
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
    for (EditorWidget *editor : editors())
        editor->setOptions(options);
}

void EditorTabs::formatCurrent()
{
    if (EditorWidget *editor = currentEditor())
        formatEditor(editor, false);
}

std::optional<CodeFormatter::Command> EditorTabs::formatterFor(EditorWidget *editor, bool quiet)
{
    if (editor->isReadOnly() || editor->filePath().isEmpty())
        return std::nullopt;
    const QString path = editor->filePath();
    std::optional<CodeFormatter::Command> command = CodeFormatter::find(editor->languageId(), path);
    if (!command && !quiet) {
        const QString name = QFileInfo(path).fileName();
        const QString candidates = CodeFormatter::describeCandidates(editor->languageId(), path);
        emit statusMessage(candidates.isEmpty() ? tr("No formatter for %1").arg(name)
                                                : tr("Cannot format %1: install %2").arg(name, candidates));
    }
    return command;
}

void EditorTabs::applyFormatted(EditorWidget *editor,
                                const std::optional<QString> &formatted,
                                const QString &error,
                                bool quiet)
{
    const QString name = QFileInfo(editor->filePath()).fileName();
    if (!formatted) {
        emit statusMessage(tr("Could not format %1: %2").arg(name, error));
        return;
    }
    const bool changed = editor->applyFormattedText(*formatted);
    if (!quiet)
        emit statusMessage(changed ? tr("%1 formatted").arg(name) : tr("%1 is already formatted").arg(name));
}

// The formatter (clang-format, prettier on Node...) takes from tens of
// milliseconds to seconds: it runs while the editor stays responsive.
void EditorTabs::formatEditor(EditorWidget *editor, bool quiet, bool saveAfter)
{
    if (const auto running = formatting_.find(editor); running != formatting_.end()) {
        *running = *running || saveAfter; // the running formatter saves when it ends
        return;
    }
    const std::optional<CodeFormatter::Command> command = formatterFor(editor, quiet);
    if (!command) {
        if (saveAfter)
            writeEditor(editor);
        return;
    }

    formatting_.insert(editor, saveAfter);
    const QString text = editor->text();
    const CodeFormatter::Job job{
        .command = *command, .workDir = QFileInfo(editor->filePath()).absolutePath(), .text = text};
    CodeFormatter::start(job, this,
                         [this, editor, guard = QPointer<EditorWidget>(editor), text,
                          quiet](const std::optional<QString> &formatted, const QString &error) {
                             const bool save = formatting_.take(editor);
                             if (!guard)
                                 return; // tab closed meanwhile
                             // Typed while the formatter ran: the result no longer applies.
                             if (editor->text() == text)
                                 applyFormatted(editor, formatted, error, quiet);
                             if (save)
                                 writeEditor(editor);
                         });
}

bool EditorTabs::saveEditor(EditorWidget *editor)
{
    if (formatOnSave_) {
        if (const std::optional<CodeFormatter::Command> command = formatterFor(editor, true)) {
            QString error;
            const std::optional<QString> formatted =
                CodeFormatter::run(*command, QFileInfo(editor->filePath()).absolutePath(), editor->text(), &error);
            applyFormatted(editor, formatted, error, true);
        }
    }
    return writeEditor(editor);
}

bool EditorTabs::startSaving(EditorWidget *editor)
{
    if (!formatOnSave_)
        return writeEditor(editor);
    formatEditor(editor, true, true);
    return true;
}

void EditorTabs::applyFormatterStyle(EditorWidget *editor)
{
    if (editor->filePath().isEmpty() || !ClangFormatStyle::appliesTo(editor->languageId()))
        return;
    // The editor receives the answer: closed meanwhile, it gets none.
    ClangFormatStyle::load(editor->filePath(), editor, [editor](std::optional<ClangFormatStyle::Style> style) {
        if (!style)
            return; // clang-format missing: the default indentation stays
        editor->setIndentationStyle(style->useTabs, style->useTabs ? style->tabWidth : style->indentWidth);
        editor->showColumnLimit(style->columnLimit);
    });
}

bool EditorTabs::writeEditor(EditorWidget *editor)
{
    QString error;
    if (!editor->save(&error)) {
        showFileError(tr("Cannot Save File"), editor->filePath(), error);
        return false;
    }
    const QString fileName = QFileInfo(editor->filePath()).fileName();
    emit statusMessage(tr("%1 saved").arg(fileName));
    const bool isClangFormatConfig =
        fileName == QLatin1String(".clang-format") || fileName == QLatin1String("_clang-format");
    if (isClangFormatConfig) {
        for (EditorWidget *other : editors())
            applyFormatterStyle(other);
    }
    emit fileSaved(editor->filePath());
    return true;
}

bool EditorTabs::saveDiagram(DiagramView *view)
{
#ifdef DUET_WITH_DIAGRAMS
    QString error;
    if (!view->save(&error)) {
        showFileError(tr("Cannot Save File"), view->filePath(), error);
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
    // The question's event loop can close tabs (a file deleted on disk):
    // the page is found again after it, never by its old index.
    const QPointer<QWidget> page = widget(index);
    if (askToSave && !askToSaveBeforeClosing(page))
        return false;
    if (!page || indexOf(page) < 0)
        return true; // closed meanwhile
    if (const QString path = filePathOf(page); !path.isEmpty()) {
        watcher_.unwatch(path);
        keptOverDisk_.remove(path);
    }
    removeTab(indexOf(page));
    page->deleteLater();
    return true;
}

bool EditorTabs::askToSaveBeforeClosing(QWidget *page)
{
    // Closed while the question is open: nothing left to save.
    if (const QPointer<EditorWidget> editor = editorIn(page); editor && editor->isModified())
        return confirmClose(this, editor->filePath(), [this, editor] { return !editor || saveEditor(editor); });
#ifdef DUET_WITH_DIAGRAMS
    if (const QPointer<DiagramView> diagram = qobject_cast<DiagramView *>(page); diagram && diagram->isModified())
        return confirmClose(this, diagram->filePath(), [this, diagram] { return !diagram || saveDiagram(diagram); });
#endif
    return true;
}

bool EditorTabs::closeAll()
{
    QList<QWidget *> pages;
    for (int i = 0; i < count(); ++i)
        pages.append(widget(i));
    return closeTabs(pages);
}

bool EditorTabs::closeTabs(const QList<QWidget *> &pages)
{
    // Indexes shift with each close, so work from the pages.
    return std::ranges::all_of(withCurrentLast(pages), [this](const QWidget *page) {
        const int index = indexOf(page);
        return index < 0 || closeTab(index);
    });
}

void EditorTabs::showTabContextMenu(const QPoint &pos)
{
    const int clicked = tabBar()->tabAt(pos);
    if (clicked < 0)
        return;
    QWidget *clickedPage = widget(clicked);

    QList<QWidget *> left;
    QList<QWidget *> right;
    QList<QWidget *> others;
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
    menu.addAction(tr("Close Other Tabs"), this, [this, others] { closeTabs(others); })->setEnabled(!others.isEmpty());
    menu.addSeparator();
    menu.addAction(tr("Close Tabs to the Left"), this, [this, left] { closeTabs(left); })->setEnabled(!left.isEmpty());
    menu.addAction(tr("Close Tabs to the Right"), this, [this, right] { closeTabs(right); })
        ->setEnabled(!right.isEmpty());
    menu.addSeparator();
    menu.addAction(tr("Close All Tabs"), this, [this] { closeAll(); });
    ContextMenus::exec(menu, *tabBar(), tabBar()->mapToGlobal(pos));
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
        syncDiagramWithDisk(diagram, path);
        return;
    }
#endif
    if (EditorWidget *editor = editorFor(path))
        syncEditorWithDisk(editor, path);
}

void EditorTabs::syncWithDiskLater(const QStringList &paths)
{
    QTimer::singleShot(0, this, [this, paths] {
        for (const QString &path : paths)
            syncWithDisk(path);
    });
}

void EditorTabs::syncEditorWithDisk(EditorWidget *editor, const QString &path)
{
    if (pathsBeingResolved_.contains(editor->filePath()))
        return;
    const QString name = QFileInfo(path).fileName();
    if (!QFileInfo::exists(path)) {
        emit statusMessage(tr("%1 was deleted from disk").arg(name));
        return;
    }
    if (editor->matchesDisk()) {
        keptOverDisk_.remove(editor->filePath());
        return; // our own save, or identical content
    }

    const QString question = tr("%1 was modified on disk, but you have unsaved changes.\n\n"
                                "Reload the file? Your changes will be lost (Ctrl+Z brings them back).")
                                 .arg(name);
    const QPointer<EditorWidget> guard = editor;
    if (editor->isModified() && !confirmReload(editor->filePath(), question))
        return;
    if (!guard)
        return; // tab closed while the question was open
    QString error;
    if (editor->reloadFromDisk(&error))
        emit statusMessage(tr("%1 reloaded from disk").arg(name));
    else
        emit statusMessage(tr("Could not reload %1: %2").arg(name, error));
}

#ifdef DUET_WITH_DIAGRAMS
void EditorTabs::syncDiagramWithDisk(DiagramView *diagram, const QString &path)
{
    if (pathsBeingResolved_.contains(diagram->filePath()))
        return;
    const QString name = QFileInfo(path).fileName();
    if (!QFileInfo::exists(path)) {
        emit statusMessage(tr("%1 was deleted from disk").arg(name));
        return;
    }
    if (diagram->matchesDisk()) {
        keptOverDisk_.remove(diagram->filePath());
        return; // our own save, or identical content
    }

    const QString question = tr("%1 was modified on disk, but you have unsaved changes.\n\n"
                                "Reload the file? Your changes will be lost.")
                                 .arg(name);
    const QPointer<DiagramView> guard = diagram;
    if (diagram->isModified() && !confirmReload(diagram->filePath(), question))
        return;
    if (!guard)
        return; // tab closed while the question was open
    QString error;
    if (diagram->reloadFromDisk(&error))
        emit statusMessage(tr("%1 reloaded from disk").arg(name));
    else
        emit statusMessage(tr("Could not reload %1: %2").arg(name, error));
}
#endif

void EditorTabs::showFileError(const QString &title, const QString &filePath, const QString &error)
{
    showNotice({.title = title, .location = QDir::toNativeSeparators(filePath), .message = error, .details = {}}, this);
}

bool EditorTabs::confirmReload(const QString &filePath, const QString &question)
{
    // Asked once per version on disk: an agent's command syncs every open
    // file again.
    const QByteArray onDisk = readFile(filePath);
    if (const auto kept = keptOverDisk_.constFind(filePath); kept != keptOverDisk_.cend() && *kept == onDisk)
        return false;

    // The question's event loop can bring the same change again: no second
    // question meanwhile.
    pathsBeingResolved_.insert(filePath);
    bool reload = false;
    auto *popup = new ChoicePopup({.title = tr("File Modified on Disk"),
                                   .location = QDir::toNativeSeparators(filePath),
                                   .message = question,
                                   .details = {}},
                                  {{tr("Reload"), [&reload] { reload = true; }}}, this);
    popup->setCancelText(tr("Keep My Changes"));
    popup->focusCancel();
    popup->exec();
    pathsBeingResolved_.remove(filePath);
    if (reload)
        keptOverDisk_.remove(filePath);
    else
        keptOverDisk_.insert(filePath, onDisk);
    return reload;
}

void EditorTabs::reloadChangeBases() const
{
    for (const EditorWidget *editor : editors()) {
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
    const QString iconName = page->property("iconName").toString();
    setTabIcon(index, !path.isEmpty()
                          ? FileIcons::file(QFileInfo(path).fileName(), theme_.isDark)
                          : UiIcons::icon(iconName.isEmpty() ? QStringLiteral("diff") : iconName, theme_.mutedText));
}

void EditorTabs::applyTheme(const Theme &theme)
{
    theme_ = theme;
    baseLine_->setColor(theme.line);
    update();
    for (int i = 0; i < count(); ++i)
        updateTabIcon(widget(i));
    for (int i = 0; i < count(); ++i) {
        if (auto *previewed = qobject_cast<PreviewPage *>(widget(i)))
            previewed->applyTheme(theme);
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
