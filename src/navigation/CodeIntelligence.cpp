// SPDX-License-Identifier: GPL-3.0-only

#include "CodeIntelligence.h"

#include "CodeNavigator.h"
#include "claude/ClaudeLint.h"
#include "diagnostics/ProblemStore.h"
#include "editor/CodeColorizer.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/MarkdownToolTip.h"
#include "editor/SymbolOccurrences.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"

#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <QToolTip>

#include <algorithm>

using namespace LspProtocol;

namespace {

constexpr int hoverDelayMs = 600;
// After the last keystroke, the text goes to the server, which checks it.
constexpr int problemsDelayMs = 700;
constexpr int maxDocumentationLength = 3000;
constexpr int maxUsagesShown = 300;

bool isCFamily(const QString &languageId)
{
    return languageId == QLatin1String("c") || languageId == QLatin1String("cpp");
}

// The server writes "\n"; files with other line endings keep theirs.
QString withLineEnding(QString text, LineEnding lineEnding)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (lineEnding == LineEnding::CrLf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    else if (lineEnding == LineEnding::Cr)
        text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    return text;
}

// Below the cursor of `editor`, in global coordinates.
QPoint belowCaret(const EditorWidget *editor)
{
    const long caret = editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS);
    const long line = editor->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, caret);
    const QPoint point(int(editor->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0, caret)),
                       int(editor->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0, caret) +
                           editor->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line)));
    return editor->viewport()->mapToGlobal(point);
}

// Where the problem is underlined in `editor`, in bytes.
std::pair<int, int> problemRange(EditorWidget *editor, const Problem &problem)
{
    const auto send = [editor](int message, long argument, long more = 0) {
        return int(editor->SendScintilla(message, argument, more));
    };
    if (problem.column < 0) {
        // A line, without its indentation.
        return {send(QsciScintillaBase::SCI_GETLINEINDENTPOSITION, problem.line),
                send(QsciScintillaBase::SCI_GETLINEENDPOSITION, problem.line)};
    }
    const int start = CodeIntelligence::positionOf(editor, {problem.line, problem.column});
    if (problem.endLine >= 0)
        return {start, std::max(start, CodeIntelligence::positionOf(editor, {problem.endLine, problem.endColumn}))};
    // The word at the column, or its character.
    const int wordEnd = send(QsciScintillaBase::SCI_WORDENDPOSITION, start, 1);
    if (wordEnd > start)
        return {start, wordEnd};
    return {start, std::min(send(QsciScintillaBase::SCI_POSITIONAFTER, start),
                            send(QsciScintillaBase::SCI_GETLINEENDPOSITION, problem.line))};
}

// The characters of the active parameter in the signature's label: offsets
// given by the server, or its text found after the "(".
std::optional<std::pair<int, int>> parameterRange(const QJsonObject &signature, int parameter)
{
    const QJsonArray parameters = signature.value("parameters").toArray();
    if (parameter < 0 || parameter >= parameters.size())
        return std::nullopt;
    const QJsonValue name = parameters.at(parameter).toObject().value("label");
    if (name.isArray()) {
        const int start = name.toArray().at(0).toInt();
        if (start < 0)
            return std::nullopt;
        return std::pair{start, name.toArray().at(1).toInt()};
    }
    const QString text = name.toString();
    if (text.isEmpty())
        return std::nullopt;
    const QString label = signature.value("label").toString();
    const int start = int(label.indexOf(text, std::max<qsizetype>(label.indexOf(QLatin1Char('(')), 0)));
    if (start < 0)
        return std::nullopt;
    return std::pair{start, start + int(text.size())};
}

// Shows `text` as the call tip of `editor`, its characters `highlight`
// highlighted.
void showCallTip(const EditorWidget *editor, const QString &text, std::optional<std::pair<int, int>> highlight)
{
    const Theme &theme = editor->theme();
    editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPSETBACK, theme.alternateBase);
    editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPSETFORE, theme.foreground);
    editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPSETFOREHLT, theme.accent);
    const QByteArray bytes = text.toUtf8();
    editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPSHOW,
                          uintptr_t(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS)), bytes.constData());
    if (!highlight || highlight->second <= highlight->first)
        return;
    const auto byteOffset = [&text](int characters) { return long(text.left(characters).toUtf8().size()); };
    editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPSETHLT,
                          static_cast<unsigned long>(byteOffset(highlight->first)), byteOffset(highlight->second));
}

// A Command, or a CodeAction carrying one; empty if there is none.
QJsonObject commandOf(const QJsonObject &action)
{
    const QJsonValue command = action.value("command");
    if (command.isObject())
        return command.toObject();
    if (command.isString())
        return action;
    return {};
}

} // namespace

CodeIntelligence::CodeIntelligence(const Services &services, QObject *parent)
    : QObject(parent)
    , tabs_(services.tabs)
    , servers_(services.servers)
    , navigator_(services.navigator)
    , problems_(services.problems)
{
    // A quick fix or a refactoring carried out by the server comes back as
    // an edit to apply.
    servers_->setRequestHandler(QStringLiteral("workspace/applyEdit"), [this](const QJsonValue &params) {
        applyWorkspaceEdit(parseWorkspaceEdit(params.toObject().value("edit").toObject()));
        return QJsonValue(QJsonObject{{"applied", true}});
    });
    connect(problems_, &ProblemStore::changed, this, &CodeIntelligence::showProblems);
}

CodeIntelligence::~CodeIntelligence() = default;

void CodeIntelligence::showProblems(const QString &path)
{
    EditorWidget *editor = tabs_->editorFor(path);
    // Claude's configuration files have their own checker.
    if (!editor || ClaudeLint::kindOf(path) != ClaudeLint::Kind::None)
        return;
    Diagnostics shown;
    for (const Problem &problem : problems_->problems(path)) {
        if (problem.line >= editor->lines())
            continue; // a build's, for a text that has changed since
        const auto [start, end] = problemRange(editor, problem);
        shown.append({problem.severity, start, end, problem.message});
    }
    editor->setDiagnostics(shown);
}

void CodeIntelligence::attach(EditorWidget *editor)
{
    // The running server follows the text as it is typed, and reports its
    // problems, shown in the editor.
    auto *sync = new QTimer(editor);
    sync->setSingleShot(true);
    sync->setInterval(problemsDelayMs);
    connect(editor, &QsciScintilla::textChanged, sync, qOverload<>(&QTimer::start));
    connect(sync, &QTimer::timeout, this, [this, editor] {
        if (LspClient *client = servers_->runningClientFor(editor->languageId()))
            servers_->syncDocument(client, editor->filePath(), editor->languageId(), editor->utf8Bytes().toByteArray());
    });
    showProblems(editor->filePath());

    editor->SendScintilla(QsciScintillaBase::SCI_SETMOUSEDWELLTIME, hoverDelayMs);
    connect(editor, &QsciScintillaBase::SCN_DWELLSTART, this, [this, editor](int position, int x, int y) {
        if (position >= 0 && editor->isActiveWindow() && !editor->areToolTipsSuspended())
            showHover(editor, position, editor->viewport()->mapToGlobal(QPoint(x, y + 12)), false);
    });
    connect(editor, &QsciScintillaBase::SCN_DWELLEND, this, [this] {
        ++hoverGeneration_;
        QToolTip::hideText();
    });
    connect(editor, &QsciScintillaBase::SCN_CHARADDED, this, [this, editor](int character) {
        if (character == '(' || character == ',') {
            requestSignatureHelp(editor, false);
        } else if (character == ')') {
            ++signatureGeneration_;
            editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPCANCEL);
        }
    });
    connect(editor->symbolOccurrences(), &SymbolOccurrences::nameSettled, this,
            [this, editor](int position) { requestOccurrences(editor, position); });
    // The parameters only show on the line of the call.
    connect(editor, &QsciScintilla::cursorPositionChanged, this, [editor](int line, int) {
        if (!editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPACTIVE))
            return;
        const long start = editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPPOSSTART);
        if (editor->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, start) != line)
            editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPCANCEL);
    });
}

int CodeIntelligence::positionOf(EditorWidget *editor, const Position &position)
{
    if (position.line >= editor->lines())
        return int(editor->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    const int lineStart =
        int(editor->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, std::max(position.line, 0)));
    const int lineEnd =
        int(editor->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, std::max(position.line, 0)));
    const QString line = QString::fromUtf8(editor->utf8Range(lineStart, lineEnd));
    return lineStart + int(line.left(std::max(position.character, 0)).toUtf8().size());
}

Position CodeIntelligence::lspPosition(EditorWidget *editor, int bytePosition)
{
    const int line = int(editor->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, bytePosition));
    const int lineStart = int(editor->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line));
    return {line, int(QString::fromUtf8(editor->utf8Range(lineStart, bytePosition)).size())};
}

LspClient *CodeIntelligence::serverFor(EditorWidget *editor, bool quiet)
{
    if (!editor || editor->filePath().isEmpty())
        return nullptr;
    LspClient *client = servers_->clientFor(editor->languageId());
    if (!client) {
        if (!quiet) {
            const QString failure = servers_->failure(editor->languageId());
            emit statusMessage(failure.isEmpty()
                                   ? tr("No language server for %1").arg(QFileInfo(editor->filePath()).fileName())
                                   : failure);
        }
        return nullptr;
    }
    servers_->syncDocument(client, editor->filePath(), editor->languageId(), editor->utf8Bytes().toByteArray());
    return client;
}

QJsonObject CodeIntelligence::positionParams(EditorWidget *editor, int bytePosition) const
{
    return QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf(editor->filePath())}}},
                       {"position", toJson(lspPosition(editor, bytePosition))}};
}

void CodeIntelligence::syncOpenEditors(LspClient *client, const QString &languageId)
{
    const std::optional<LanguageServerSpec> spec = servers_->specFor(languageId);
    if (!spec)
        return;
    for (const QString &path : tabs_->openFilePaths()) {
        const EditorWidget *editor = tabs_->editorFor(path);
        if (!editor || !spec->languages.contains(editor->languageId()))
            continue;
        servers_->syncDocument(client, path, editor->languageId(), editor->utf8Bytes().toByteArray());
    }
}

void CodeIntelligence::requestOccurrences(EditorWidget *editor, int bytePosition)
{
    // Never starts a server: highlighting follows every click.
    LspClient *client = servers_->runningClientFor(editor->languageId());
    if (!client || !client->isReady() || editor->filePath().isEmpty())
        return;
    // true or an options object.
    const QJsonValue provider = client->capabilities().value("documentHighlightProvider");
    const bool highlightsSymbols = provider.isObject() || provider.toBool();
    if (!highlightsSymbols)
        return;
    servers_->syncDocument(client, editor->filePath(), editor->languageId(), editor->utf8Bytes().toByteArray());
    client->request(
        QStringLiteral("textDocument/documentHighlight"), positionParams(editor, bytePosition),
        [guard = QPointer<EditorWidget>(editor), bytePosition](const QJsonValue &result, const QString &error) {
            // A server that fails keeps the text's occurrences.
            if (!guard || !error.isEmpty())
                return;
            QList<SymbolOccurrences::Range> ranges;
            for (const QJsonValue &highlight : result.toArray()) {
                const Range range = rangeFrom(highlight.toObject().value("range"));
                ranges.append({positionOf(guard, range.start), positionOf(guard, range.end)});
            }
            std::ranges::sort(ranges, {}, &SymbolOccurrences::Range::start);
            guard->symbolOccurrences()->showServerOccurrences(bytePosition, ranges);
        });
}

// Documentation

void CodeIntelligence::showHover(EditorWidget *editor, int bytePosition, const QPoint &where, bool explicitRequest)
{
    LspClient *client = serverFor(editor, !explicitRequest);
    // The mouse alone does not wait for a server that is still starting.
    if (!client || (!explicitRequest && !client->isReady()))
        return;
    const int generation = ++hoverGeneration_;
    client->request(QStringLiteral("textDocument/hover"), positionParams(editor, bytePosition),
                    [this, generation, guard = QPointer<EditorWidget>(editor), where,
                     explicitRequest](const QJsonValue &result, const QString &) {
                        // The completion list may have opened while the server answered.
                        if (generation != hoverGeneration_ || !guard || guard->areToolTipsSuspended())
                            return;
                        QString text = hoverMarkdown(result.toObject().value("contents")).trimmed();
                        if (text.isEmpty()) {
                            if (explicitRequest)
                                emit statusMessage(tr("No documentation here"));
                            return;
                        }
                        if (text.size() > maxDocumentationLength)
                            text = text.left(maxDocumentationLength) + QStringLiteral("…");
                        QToolTip::showText(where, documentationHtml(text, *guard), guard->viewport());
                    });
}

QString CodeIntelligence::documentationHtml(const QString &markdown, const EditorWidget &editor)
{
    if (!colorizer_)
        colorizer_ = std::make_unique<CodeColorizer>();
    return MarkdownToolTip::html(markdown, {editor.theme(), colorizer_.get(), editor.languageId()});
}

void CodeIntelligence::showDocumentation()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor)
        return;
    showHover(editor, int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS)), belowCaret(editor), true);
}

// Parameters

void CodeIntelligence::showParameterInfo()
{
    if (EditorWidget *editor = tabs_->currentEditor())
        requestSignatureHelp(editor, true);
}

void CodeIntelligence::requestParameterInfo(EditorWidget *editor)
{
    requestSignatureHelp(editor, false);
}

void CodeIntelligence::requestSignatureHelp(EditorWidget *editor, bool explicitRequest)
{
    LspClient *client = serverFor(editor, !explicitRequest);
    if (!client || (!explicitRequest && !client->isReady()))
        return;
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    QJsonObject params = positionParams(editor, caret);
    params.insert("context",
                  QJsonObject{{"triggerKind", explicitRequest ? 1 : 2},
                              {"isRetrigger", editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPACTIVE) != 0}});
    const int generation = ++signatureGeneration_;
    client->request(QStringLiteral("textDocument/signatureHelp"), params,
                    [this, generation, guard = QPointer<EditorWidget>(editor),
                     explicitRequest](const QJsonValue &result, const QString &) {
                        if (generation != signatureGeneration_ || !guard)
                            return;
                        showSignatureHelp(guard, result.toObject(), explicitRequest);
                    });
}

void CodeIntelligence::showSignatureHelp(const EditorWidget *editor, const QJsonObject &help, bool explicitRequest)
{
    const QJsonArray signatures = help.value("signatures").toArray();
    if (signatures.isEmpty()) {
        editor->SendScintilla(QsciScintillaBase::SCI_CALLTIPCANCEL);
        if (explicitRequest)
            emit statusMessage(tr("No call here"));
        return;
    }
    const int active = std::clamp(help.value("activeSignature").toInt(), 0, int(signatures.size()) - 1);
    const QJsonObject signature = signatures.at(active).toObject();
    const int parameter = signature.contains("activeParameter") ? signature.value("activeParameter").toInt()
                                                                : help.value("activeParameter").toInt();
    std::optional<std::pair<int, int>> highlight = parameterRange(signature, parameter);

    QString shown = signature.value("label").toString();
    if (signatures.size() > 1) {
        const QString counter = QStringLiteral("%1/%2  ").arg(active + 1).arg(signatures.size());
        shown.prepend(counter);
        if (highlight) {
            highlight->first += int(counter.size());
            highlight->second += int(counter.size());
        }
    }
    showCallTip(editor, shown, highlight);
}

// Definitions and usages

void CodeIntelligence::goToDefinition()
{
    EditorWidget *editor = tabs_->currentEditor();
    LspClient *client = serverFor(editor, true);
    // The project's index answers at once; a server still starting would not.
    if (!client || !client->isReady()) {
        navigator_->goToDefinition();
        return;
    }
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    client->request(QStringLiteral("textDocument/definition"), positionParams(editor, caret),
                    [this, guard = QPointer<EditorWidget>(editor)](const QJsonValue &result, const QString &error) {
                        if (!guard)
                            return;
                        const QList<Location> locations = parseLocations(result);
                        if (!error.isEmpty() || locations.isEmpty()) {
                            navigator_->goToDefinition(); // macros, files without compile commands...
                            return;
                        }
                        if (locations.size() == 1) {
                            jumpTo(locations.first());
                            return;
                        }
                        QHash<QString, QStringList> lines;
                        QStringList entries;
                        for (const Location &location : locations)
                            entries.append(describe(location, lines));
                        if (const int chosen = chooseFrom(guard, entries); chosen >= 0)
                            jumpTo(locations.at(chosen));
                    });
}

void CodeIntelligence::findUsages()
{
    EditorWidget *editor = tabs_->currentEditor();
    LspClient *client = serverFor(editor);
    if (!client)
        return;
    syncOpenEditors(client, editor->languageId());
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int start = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, true));
    const int end = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION, caret, true));
    const QString word = editor->text(start, end);
    QJsonObject params = positionParams(editor, caret);
    params.insert("context", QJsonObject{{"includeDeclaration", true}});
    emit statusMessage(tr("Looking for the usages of “%1”…").arg(word));
    client->request(
        QStringLiteral("textDocument/references"), params,
        [this, guard = QPointer<EditorWidget>(editor), word](const QJsonValue &result, const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty()) {
                emit statusMessage(tr("Find Usages failed: %1").arg(error));
                return;
            }
            showUsages(guard, word, parseLocations(result));
        });
}

void CodeIntelligence::showUsages(EditorWidget *editor, const QString &word, QList<Location> locations)
{
    std::sort(locations.begin(), locations.end(), [](const Location &a, const Location &b) {
        return a.filePath != b.filePath ? a.filePath < b.filePath : a.range.start < b.range.start;
    });
    if (locations.isEmpty()) {
        emit statusMessage(tr("No usages of “%1”").arg(word));
        return;
    }
    emit statusMessage(locations.size() == 1 ? tr("1 usage of “%1”").arg(word)
                                             : tr("%1 usages of “%2”").arg(locations.size()).arg(word));
    if (locations.size() > maxUsagesShown)
        locations.resize(maxUsagesShown);
    QHash<QString, QStringList> lines;
    QStringList entries;
    for (const Location &location : std::as_const(locations))
        entries.append(describe(location, lines));
    if (const int chosen = chooseFrom(editor, entries); chosen >= 0)
        jumpTo(locations.at(chosen));
}

void CodeIntelligence::jumpTo(const Location &location)
{
    navigator_->goToLine(location.filePath, location.range.start.line, location.range.start.character);
}

// "src/a.cpp:12   int total = count(items);"
QString CodeIntelligence::describe(const Location &location, QHash<QString, QStringList> &lines) const
{
    auto it = lines.find(location.filePath);
    if (it == lines.end()) {
        QString text;
        QString error;
        if (const EditorWidget *editor = tabs_->editorFor(location.filePath))
            text = editor->text();
        else if (const std::optional<DecodedText> decoded = TextFile::load(location.filePath, &error))
            text = decoded->text;
        it = lines.insert(location.filePath, text.split(QLatin1Char('\n')));
    }
    QString code = it->value(location.range.start.line).trimmed();
    if (code.size() > 80)
        code = code.left(79) + QStringLiteral("…");
    QString path = QDir(servers_->folder()).relativeFilePath(location.filePath);
    if (path.startsWith(QLatin1String("..")))
        path = location.filePath;
    return QStringLiteral("%1:%2   %3")
        .arg(QDir::toNativeSeparators(path))
        .arg(location.range.start.line + 1)
        .arg(code);
}

int CodeIntelligence::chooseFrom(EditorWidget *editor, const QStringList &entries) const
{
    QMenu menu(editor);
    for (int i = 0; i < entries.size(); ++i)
        menu.addAction(entries[i])->setData(i);
    const QAction *chosen = menu.exec(belowCaret(editor));
    return chosen ? chosen->data().toInt() : -1;
}

// Rename and quick fixes

void CodeIntelligence::rename()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->isReadOnly())
        return;
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int start = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, true));
    const int end = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION, caret, true));
    const QString word = editor->text(start, end);
    if (word.isEmpty()) {
        emit statusMessage(tr("Place the cursor on a name to rename it"));
        return;
    }
    LspClient *client = serverFor(editor);
    if (!client)
        return;
    bool accepted = false;
    const QString newName =
        QInputDialog::getText(editor, tr("Rename"), tr("Rename “%1” to:").arg(word), QLineEdit::Normal, word, &accepted)
            .trimmed();
    if (!accepted || newName.isEmpty() || newName == word)
        return;
    syncOpenEditors(client, editor->languageId());
    QJsonObject params = positionParams(editor, start);
    params.insert("newName", newName);
    client->request(QStringLiteral("textDocument/rename"), params,
                    [this, word, newName](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Cannot rename “%1”: %2").arg(word, error));
                            return;
                        }
                        const WorkspaceEdit edit = parseWorkspaceEdit(result.toObject());
                        if (edit.isEmpty()) {
                            emit statusMessage(tr("Nothing to rename here"));
                            return;
                        }
                        const int files = applyWorkspaceEdit(edit);
                        emit statusMessage(files == 1
                                               ? tr("Renamed “%1” to “%2”").arg(word, newName)
                                               : tr("Renamed “%1” to “%2” in %3 files").arg(word, newName).arg(files));
                    });
}

void CodeIntelligence::showQuickFixes()
{
    EditorWidget *editor = tabs_->currentEditor();
    LspClient *client = serverFor(editor);
    if (!client || editor->isReadOnly())
        return;
    const int from = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART));
    const int to = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND));
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(editor->filePath())}}},
        {"range", toJson(Range{lspPosition(editor, from), lspPosition(editor, to)})},
        {"context", QJsonObject{{"diagnostics", QJsonArray{}}}},
    };
    client->request(QStringLiteral("textDocument/codeAction"), params,
                    [this, guard = QPointer<EditorWidget>(editor),
                     server = QPointer<LspClient>(client)](const QJsonValue &result, const QString &error) {
                        if (!guard)
                            return;
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Quick fixes failed: %1").arg(error));
                            return;
                        }
                        chooseQuickFix(guard, server, result.toArray());
                    });
}

void CodeIntelligence::chooseQuickFix(EditorWidget *editor, LspClient *server, const QJsonArray &codeActions)
{
    QList<QJsonObject> actions;
    QStringList titles;
    for (const QJsonValue &item : codeActions) {
        const QJsonObject action = item.toObject();
        if (action.contains("disabled") || action.value("title").toString().isEmpty())
            continue;
        actions.append(action);
        titles.append(action.value("title").toString());
    }
    if (actions.isEmpty()) {
        emit statusMessage(tr("No quick fixes here"));
        return;
    }
    const int chosen = chooseFrom(editor, titles);
    if (chosen < 0)
        return;
    const QJsonObject action = actions.at(chosen);
    if (action.contains("edit"))
        applyWorkspaceEdit(parseWorkspaceEdit(action.value("edit").toObject()));
    // The server carries the command out and sends back the edit
    // ("workspace/applyEdit").
    const QJsonObject command = commandOf(action);
    if (command.isEmpty() || !server)
        return;
    server->request(
        QStringLiteral("workspace/executeCommand"),
        QJsonObject{{"command", command.value("command")}, {"arguments", command.value("arguments").toArray()}},
        [this](const QJsonValue &, const QString &error) {
            if (!error.isEmpty())
                emit statusMessage(tr("The quick fix failed: %1").arg(error));
        });
}

int CodeIntelligence::applyWorkspaceEdit(const WorkspaceEdit &edit)
{
    int changed = 0;
    for (auto it = edit.cbegin(); it != edit.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        if (EditorWidget *editor = tabs_->editorFor(it.key())) {
            // In the editor, as one undo step; from the end, so that the
            // positions still to change do not move.
            editor->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
            for (const TextEdit &textEdit : applicationOrder(it.value())) {
                const int start = positionOf(editor, textEdit.range.start);
                const int end = std::max(start, positionOf(editor, textEdit.range.end));
                const QByteArray bytes = withLineEnding(textEdit.newText, editor->format().lineEnding).toUtf8();
                editor->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(start),
                                      long(end));
                editor->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<uintptr_t>(bytes.size()),
                                      bytes.constData());
            }
            editor->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
            ++changed;
            continue;
        }
        // A file that is not open: changed on disk.
        QString error;
        const std::optional<DecodedText> decoded = TextFile::load(it.key(), &error);
        if (!decoded) {
            emit statusMessage(tr("Could not change %1: %2").arg(QFileInfo(it.key()).fileName(), error));
            continue;
        }
        QList<TextEdit> edits = it.value();
        for (TextEdit &textEdit : edits)
            textEdit.newText = withLineEnding(textEdit.newText, decoded->format.lineEnding);
        if (TextFile::save(it.key(), applyEdits(decoded->text, edits), decoded->format, &error))
            ++changed;
        else
            emit statusMessage(tr("Could not change %1: %2").arg(QFileInfo(it.key()).fileName(), error));
    }
    return changed;
}

// Header and source

void CodeIntelligence::switchHeaderSource()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty())
        return;
    const QString path = editor->filePath();
    const auto open = [this, path](const QString &target) {
        if (target.isEmpty() || !QFileInfo::exists(target)) {
            emit statusMessage(tr("No header or source file for %1").arg(QFileInfo(path).fileName()));
            return;
        }
        if (EditorWidget *opened = EditorTabs::editorIn(tabs_->openFile(target))) {
            tabs_->revealEditor(opened);
            opened->setFocus();
        }
    };
    if (!isCFamily(editor->languageId())) {
        open(counterpartOf(path));
        return;
    }
    // clangd knows the includes; the names next to the file are the fallback.
    LspClient *client = serverFor(editor, true);
    if (!client || !client->isReady()) {
        open(counterpartOf(path));
        return;
    }
    client->request(QStringLiteral("textDocument/switchSourceHeader"), QJsonObject{{"uri", uriOf(path)}},
                    [open, path](const QJsonValue &result, const QString &) {
                        const QString target = pathOf(result.toString());
                        open(target.isEmpty() ? counterpartOf(path) : target);
                    });
}

QString CodeIntelligence::counterpartOf(const QString &filePath)
{
    static const QStringList headers = {QStringLiteral("h"),   QStringLiteral("hh"),  QStringLiteral("hpp"),
                                        QStringLiteral("hxx"), QStringLiteral("h++"), QStringLiteral("inl")};
    static const QStringList sources = {QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
                                        QStringLiteral("c++"), QStringLiteral("c"),  QStringLiteral("m"),
                                        QStringLiteral("mm")};
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    const QStringList *wanted = nullptr;
    if (headers.contains(suffix))
        wanted = &sources;
    else if (sources.contains(suffix))
        wanted = &headers;
    else
        return {};
    const QDir folder = info.dir();
    const QStringList folders = {folder.path(), folder.filePath(QStringLiteral("../include")),
                                 folder.filePath(QStringLiteral("../src")),
                                 folder.filePath(QStringLiteral("../source"))};
    for (const QString &candidateFolder : folders) {
        for (const QString &extension : *wanted) {
            const QString candidate = QDir::cleanPath(candidateFolder + QLatin1Char('/') + info.completeBaseName() +
                                                      QLatin1Char('.') + extension);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }
    return {};
}
