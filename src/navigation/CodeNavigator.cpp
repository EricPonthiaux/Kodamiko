// SPDX-License-Identifier: GPL-3.0-only

#include "CodeNavigator.h"

#include "Definitions.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "ui/ContextMenus.h"

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QRegularExpression>

#include <algorithm>

namespace {

// The symbols of `matches` in `file`. When the file only declares them (a
// header), their definitions elsewhere too: a link to a symbol means its code,
// as Ctrl+click in the editor does, and Definitions::resolve puts them first.
QList<SymbolLocation> inLinkedFile(const QList<SymbolLocation> &matches, const QString &file)
{
    QList<SymbolLocation> linked;
    for (const SymbolLocation &symbol : matches) {
        if (QDir::cleanPath(symbol.filePath) == file)
            linked.append(symbol);
    }
    const bool onlyDeclares = !linked.isEmpty() && std::ranges::all_of(linked, [](const SymbolLocation &symbol) {
        return symbol.declarationOnly;
    });
    if (!onlyDeclares)
        return linked;
    for (const SymbolLocation &symbol : matches) {
        if (!symbol.declarationOnly)
            linked.append(symbol);
    }
    return linked;
}

QString describe(const SymbolLocation &target, const QString &folder)
{
    const QString name =
        target.container.isEmpty() ? target.name : target.container + QLatin1String("::") + target.name;
    QString path = QDir(folder).relativeFilePath(target.filePath);
    if (path.startsWith(QLatin1String("..")))
        path = target.filePath;
    QString label = QStringLiteral("%1 — %2:%3").arg(name, path).arg(target.startLine + 1);
    if (target.declarationOnly)
        label += QObject::tr(" (declaration)");
    return label;
}

} // namespace

CodeNavigator::CodeNavigator(EditorTabs *tabs, SymbolIndex *index, QObject *parent)
    : QObject(parent)
    , tabs_(tabs)
    , index_(index)
{
}

void CodeNavigator::goToDefinition()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty())
        return;

    const int caret = static_cast<int>(editor->send(SCI_GETCURRENTPOS));
    const int start = static_cast<int>(editor->send(SCI_WORDSTARTPOSITION, caret, true));
    const int end = static_cast<int>(editor->send(SCI_WORDENDPOSITION, caret, true));
    if (start == end)
        return;

    Definitions::Request request;
    request.word = editor->text(start, end);
    request.filePath = editor->filePath();
    request.languageId = editor->languageId();
    request.position = start;
    request.line = static_cast<int>(editor->send(SCI_LINEFROMPOSITION, start));

    // "Foo::bar", "x.bar", "x->bar": what precedes the word on the line.
    const int lineStart = static_cast<int>(editor->send(SCI_POSITIONFROMLINE, request.line));
    const QString before = editor->text(lineStart, request.position);
    static const QRegularExpression qualified(QStringLiteral("(\\w+)\\s*::\\s*$"));
    static const QRegularExpression member(QStringLiteral("(\\.|->)\\s*$"));
    if (const QRegularExpressionMatch match = qualified.match(before); match.hasMatch())
        request.qualifier = match.captured(1);
    request.memberAccess = member.match(before).hasMatch();

    // The current file may have changed since the last indexing.
    index_->updateFile(request.filePath, editor->text().toUtf8());
    const Definitions::Result result =
        Definitions::resolve(request, index_->lookup(request.word), index_->symbolsIn(request.filePath));

    if (result.targets.isEmpty()) {
        emit statusMessage(notFoundMessage(request.word));
        return;
    }
    if (result.ambiguous)
        chooseTarget(editor, result.targets);
    else
        jumpTo(result.targets.first());
}

void CodeNavigator::goToSymbol(const QString &name, const QString &container, const QString &filePath)
{
    const QString file =
        filePath.isEmpty() ? QString() : QDir::cleanPath(QDir(index_->folder()).absoluteFilePath(filePath));
    QList<SymbolLocation> matches = name.isEmpty() ? QList<SymbolLocation>() : index_->lookup(name);
    if (!file.isEmpty())
        matches = inLinkedFile(matches, file);
    Definitions::Request request;
    request.word = name;
    request.qualifier = container;
    const Definitions::Result result = Definitions::resolve(request, std::move(matches), {});

    if (const QString rootFile = file.isEmpty() ? rootFileNamed(container, name) : QString();
        result.targets.isEmpty() && !rootFile.isEmpty()) {
        goToLine(rootFile, 0);
        return;
    }
    if (result.targets.isEmpty()) {
        showLinkedFile(file, container.isEmpty() ? name : container + QLatin1String("::") + name);
        return;
    }
    // The diagram was just clicked: the menu opens under the pointer.
    if (result.ambiguous)
        chooseTargetAt(*tabs_, QCursor::pos(), result.targets);
    else
        jumpTo(result.targets.first());
}

void CodeNavigator::showLinkedFile(const QString &file, const QString &qualifiedName)
{
    if (file.isEmpty()) {
        emit statusMessage(notFoundMessage(qualifiedName));
        return;
    }
    const QString shownPath = QDir(index_->folder()).relativeFilePath(file);
    if (!QFileInfo::exists(file)) {
        emit statusMessage(tr("The linked file \"%1\" does not exist").arg(shownPath));
        return;
    }
    goToLine(file, 0);
    if (!qualifiedName.isEmpty())
        emit statusMessage(tr("No definition found for \"%1\" in %2").arg(qualifiedName, shownPath));
}

QString CodeNavigator::rootFileNamed(const QString &container, const QString &name) const
{
    if (container.isEmpty())
        return {};
    const QString path = QDir(index_->folder()).filePath(container + QLatin1Char('.') + name);
    return QFileInfo(path).isFile() ? QDir::cleanPath(path) : QString();
}

QString CodeNavigator::notFoundMessage(const QString &name) const
{
    return index_->isIndexing() ? tr("No definition found for \"%1\" (indexing in progress)").arg(name)
                                : tr("No definition found for \"%1\"").arg(name);
}

// Several plausible targets: menu under the word.
void CodeNavigator::chooseTarget(EditorWidget *editor, const QList<SymbolLocation> &targets)
{
    const int caret = static_cast<int>(editor->send(SCI_GETCURRENTPOS));
    const int line = static_cast<int>(editor->send(SCI_LINEFROMPOSITION, caret));
    const QPoint point(static_cast<int>(editor->send(SCI_POINTXFROMPOSITION, 0, caret)),
                       static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, caret)) +
                           static_cast<int>(editor->send(SCI_TEXTHEIGHT, line)));
    chooseTargetAt(*editor, editor->viewport()->mapToGlobal(point), targets);
}

void CodeNavigator::chooseTargetAt(QWidget &clicked, QPoint globalPoint, const QList<SymbolLocation> &targets)
{
    QMenu menu(&clicked);
    for (const SymbolLocation &target : targets)
        menu.addAction(describe(target, index_->folder()), this, [this, target] { jumpTo(target); });
    ContextMenus::exec(menu, clicked, globalPoint);
}

void CodeNavigator::jumpTo(const SymbolLocation &target)
{
    if (const auto from = currentLocation())
        history_.recordJump(*from);
    showLocation({target.filePath, target.position});
}

void CodeNavigator::goTo(const QString &filePath, int position)
{
    if (const auto from = currentLocation())
        history_.recordJump(*from);
    showLocation({filePath, position});
}

void CodeNavigator::goToLine(const QString &filePath, int line, int column, int length)
{
    if (const auto from = currentLocation())
        history_.recordJump(*from);
    if (!QFileInfo::exists(filePath))
        return;
    auto *editor = EditorTabs::editorIn(tabs_->openFile(filePath));
    if (!editor)
        return;
    tabs_->revealEditor(editor);
    line = std::clamp(line, 0, std::max(editor->lines() - 1, 0));
    // Columns in characters → byte offsets in the line.
    const QString text = editor->text(line);
    const int start = static_cast<int>(text.left(column).toUtf8().size());
    const int end = static_cast<int>(text.left(column + length).toUtf8().size());
    const long lineStart = editor->send(SCI_POSITIONFROMLINE, line);
    editor->send(SCI_SETSEL, lineStart + start, lineStart + end);
    editor->send(SCI_VERTICALCENTRECARET);
    editor->setFocus();
}

void CodeNavigator::goBack()
{
    const auto current = currentLocation();
    if (const auto location = history_.back(current.value_or(NavigationHistory::Location{})))
        showLocation(*location);
}

void CodeNavigator::goForward()
{
    const auto current = currentLocation();
    if (const auto location = history_.forward(current.value_or(NavigationHistory::Location{})))
        showLocation(*location);
}

std::optional<NavigationHistory::Location> CodeNavigator::currentLocation() const
{
    if (const EditorWidget *editor = tabs_->currentEditor(); editor && !editor->filePath().isEmpty())
        return NavigationHistory::Location{.filePath = editor->filePath(),
                                           .position = static_cast<int>(editor->send(SCI_GETCURRENTPOS))};
    if (const std::optional<EditorTabs::DiagramPlace> diagram = tabs_->currentDiagramPlace())
        return NavigationHistory::Location{.filePath = diagram->filePath, .element = diagram->element};
    return std::nullopt;
}

void CodeNavigator::showLocation(const NavigationHistory::Location &location)
{
    if (location.filePath.isEmpty() || !QFileInfo::exists(location.filePath))
        return;
    QWidget *page = tabs_->openFile(location.filePath);
    auto *editor = EditorTabs::editorIn(page);
    if (!editor) {
        tabs_->revealDiagramElement(page, location.element);
        return;
    }
    tabs_->revealEditor(editor);
    editor->send(SCI_GOTOPOS, location.position);
    editor->send(SCI_VERTICALCENTRECARET);
    editor->setFocus();
}
