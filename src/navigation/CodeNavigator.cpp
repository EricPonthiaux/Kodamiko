#include "CodeNavigator.h"

#include "Definitions.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QRegularExpression>

namespace {

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

    const int caret = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int start = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, true));
    const int end = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION, caret, true));
    if (start == end)
        return;

    Definitions::Request request;
    request.word = editor->text(start, end);
    request.filePath = editor->filePath();
    request.languageId = editor->languageId();
    request.position = start;
    request.line = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, start));

    // "Foo::bar", "x.bar", "x->bar": what precedes the word on the line.
    const int lineStart =
        static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, request.line));
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
        emit statusMessage(index_->isIndexing()
                               ? tr("No definition found for \"%1\" (indexing in progress)").arg(request.word)
                               : tr("No definition found for \"%1\"").arg(request.word));
        return;
    }
    if (result.ambiguous)
        chooseTarget(editor, result.targets);
    else
        jumpTo(result.targets.first());
}

// Several plausible targets: menu under the word.
void CodeNavigator::chooseTarget(EditorWidget *editor, const QList<SymbolLocation> &targets)
{
    QMenu menu(editor);
    for (const SymbolLocation &target : targets)
        menu.addAction(describe(target, index_->folder()), this, [this, target] { jumpTo(target); });

    const int caret = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int line = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, caret));
    const QPoint point(static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0, caret)),
                       static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0, caret)) +
                           static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line)));
    menu.exec(editor->viewport()->mapToGlobal(point));
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
    const long lineStart = editor->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
    editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, lineStart + start, lineStart + end);
    editor->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
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
    const EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty())
        return std::nullopt;
    return NavigationHistory::Location{editor->filePath(),
                                       static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))};
}

void CodeNavigator::showLocation(const NavigationHistory::Location &location)
{
    if (location.filePath.isEmpty() || !QFileInfo::exists(location.filePath))
        return;
    auto *editor = EditorTabs::editorIn(tabs_->openFile(location.filePath));
    if (!editor)
        return;
    tabs_->revealEditor(editor);
    editor->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, location.position);
    editor->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
    editor->setFocus();
}
