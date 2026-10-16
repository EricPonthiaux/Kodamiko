#include "CompletionController.h"

#include "CompletionPopup.h"
#include "CompletionService.h"
#include "LocalCompletion.h"
#include "Snippet.h"
#include "editor/EditorWidget.h"
#include "syntax/ScintillaStyles.h"

#include <Qsci/qscilexer.h>

#include <QKeyEvent>

namespace {

// Pause after a keystroke before the automatic popup: none while typing
// fast enough to know the word.
constexpr int autoDelayMs = 60;

bool isIdentifierByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || static_cast<unsigned char>(c) >= 0x80;
}

} // namespace

CompletionController::CompletionController(EditorWidget *editor, CompletionService *service)
    : QObject(editor)
    , editor_(editor)
    , service_(service)
{
    setObjectName(QStringLiteral("completionController"));
    editor_->installEventFilter(this);
    autoTimer_.setSingleShot(true);
    autoTimer_.setInterval(autoDelayMs);
    connect(&autoTimer_, &QTimer::timeout, this, [this] { start(false, {}); });
    connect(editor_, &QsciScintillaBase::SCN_CHARADDED, this, &CompletionController::onCharacterAdded);
    // The cursor signal comes with the next repaint. A typed character is
    // followed in onCharacterAdded; a deletion, once the cursor has moved
    // (Scintilla reports it before).
    const auto follow = [this] {
        if (active_)
            refilter();
    };
    connect(editor_, &QsciScintilla::textChanged, this, follow, Qt::QueuedConnection);
    connect(editor_, &QsciScintilla::cursorPositionChanged, this, follow);
}

CompletionController::~CompletionController()
{
    if (service_ && active_)
        service_->cancel();
    delete popup_;
}

CompletionController *CompletionController::of(const EditorWidget *editor)
{
    return editor ? editor->findChild<CompletionController *>(QStringLiteral("completionController"),
                                                               Qt::FindDirectChildrenOnly)
                  : nullptr;
}

CompletionPopup *CompletionController::popup() const
{
    return popup_;
}

bool CompletionController::isActive() const
{
    return active_ && popup_ && popup_->isVisible();
}

void CompletionController::complete()
{
    start(true, {});
}

QString CompletionController::textBetween(int start, int end) const
{
    return QString::fromUtf8(editor_->utf8Range(start, end));
}

int CompletionController::identifierStart(int position) const
{
    int start = position;
    while (start > 0
           && isIdentifierByte(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, start - 1))))
        --start;
    return start;
}

bool CompletionController::insideCommentOrString(int position) const
{
    if (position <= 0)
        return false;
    // The text just typed may not be styled yet.
    const long lineStart = editor_->SendScintilla(
        QsciScintillaBase::SCI_POSITIONFROMLINE, editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    editor_->SendScintilla(QsciScintillaBase::SCI_COLOURISE, lineStart, static_cast<long>(position));
    const int style = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position - 1));
    QString name;
    if (const QsciLexer *lexer = editor_->lexer()) {
        name = lexer->description(style).toLower();
    } else {
        const int index = style - ScintillaStyles::firstSyntaxStyle;
        if (index >= 0 && index < editor_->theme().syntaxStyles.size())
            name = editor_->theme().syntaxStyles[index].first;
    }
    return name.contains(QLatin1String("comment")) || name.contains(QLatin1String("string"));
}

void CompletionController::onCharacterAdded(int character)
{
    if (active_) {
        refilter();
        if (active_)
            return;
        // Closed by a character that ends the identifier: it may open the
        // next completion ("object." after "object").
    }
    if (editor_->isReadOnly())
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const QChar typed(character);
    const QChar previous = position >= 2
                               ? QChar(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT,
                                                                                position - 2)))
                               : QChar();
    if (service_->isTrigger(editor_->languageId(), typed, previous)) {
        if (!insideCommentOrString(position))
            start(false, typed);
        return;
    }
    if (!editor_->options().autoCompletion || !isIdentifierCharacter(typed))
        return;
    // An identifier, not a number.
    const int start = identifierStart(position);
    const char first = static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, start));
    if (first >= '0' && first <= '9')
        return;
    if (!insideCommentOrString(position))
        autoTimer_.start();
}

void CompletionController::start(bool explicitRequest, QChar triggerCharacter)
{
    autoTimer_.stop();
    if (editor_->isReadOnly())
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    // Typing went on past the identifier since the timer started.
    if (!explicitRequest && triggerCharacter.isNull() && identifierStart(position) == position)
        return;

    // A name typed after "object." (or "->", "::") is a member, whoever
    // opened the list: only the language server knows those.
    const int nameStart = identifierStart(position);
    if (triggerCharacter.isNull() && nameStart > 0) {
        const auto charAt = [this](int at) {
            return at >= 0 ? QChar(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, at)))
                           : QChar();
        };
        const QChar before = charAt(nameStart - 1);
        if (service_->isTrigger(editor_->languageId(), before, charAt(nameStart - 2)))
            triggerCharacter = before;
    }

    CompletionContext context;
    context.filePath = editor_->filePath();
    context.languageId = editor_->languageId();
    context.text = editor_->utf8Bytes().toByteArray(); // one copy; text() makes three
    context.position = position;
    context.prefixStart = identifierStart(position);
    context.prefix = textBetween(context.prefixStart, position);
    context.line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    const long lineStart = editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, context.line);
    context.column = int(textBetween(int(lineStart), position).size());
    context.triggerCharacter = triggerCharacter;
    context.explicitRequest = explicitRequest;

    const bool restarting = active_ && prefixStart_ == context.prefixStart; // the same identifier, typed further
    active_ = true;
    prefixStart_ = context.prefixStart;
    line_ = context.line;
    if (!restarting)
        items_.clear(); // else, the current list stays until the answers
    listStale_ = true;
    incomplete_ = false;
    requestedPrefix_ = context.prefix;
    service_->request(context, [this](const QList<CompletionItem> &items, bool incomplete) {
        if (!active_)
            return;
        items_ = items;
        listStale_ = true;
        incomplete_ = incomplete;
        refilter();
    });
}

void CompletionController::refilter()
{
    if (!active_)
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    // The cursor left the identifier: completion is over.
    if (line != line_ || position < prefixStart_ || identifierStart(position) != prefixStart_) {
        close();
        return;
    }
    if (!popup_) {
        popup_ = new CompletionPopup(editor_->window());
        popup_->applyTheme(editor_->theme());
        connect(popup_, &CompletionPopup::activated, this, [this] { accept(); });
    }
    const QString prefix = textBetween(prefixStart_, position);
    if (incomplete_ && prefix != requestedPrefix_ && prefix.startsWith(requestedPrefix_)) {
        start(false, {}); // the server's next part; the list stays meanwhile
        return;
    }
    // Typing a character signals it several times (character added, text
    // and cursor changed): the list is filtered once per change.
    if (!listStale_ && prefix == listedPrefix_)
        return;
    listStale_ = false;
    listedPrefix_ = prefix;
    if (popup_->setItems(items_, prefix) == 0) {
        popup_->hide(); // nothing yet: answers may still come
        return;
    }
    showPopup();
}

void CompletionController::showPopup()
{
    QWidget *viewport = editor_->viewport();
    const int x = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0L,
                                                          static_cast<long>(prefixStart_)));
    const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0L,
                                                          static_cast<long>(prefixStart_)));
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line_));
    QWidget *parent = popup_->parentWidget();
    // Aligned with the identifier's text: the badge's width to the left.
    const QPoint below = viewport->mapTo(parent, QPoint(x - 34, y + lineHeight + 2));
    const QPoint top = viewport->mapTo(parent, QPoint(x, y - 2));
    popup_->applyTheme(editor_->theme());
    popup_->showAt(below, top.y());
}

void CompletionController::close()
{
    autoTimer_.stop();
    if (!active_)
        return;
    active_ = false;
    items_.clear();
    listStale_ = true;
    service_->cancel();
    if (popup_)
        popup_->hide();
}

void CompletionController::accept(bool replaceWord)
{
    if (!isActive() || !popup_->current())
        return;
    const CompletionItem item = *popup_->current(); // close() clears the list
    ExpandedSnippet expanded;
    if (item.isSnippet) {
        expanded = Snippet::expand(item.textToInsert());
    } else {
        expanded.text = item.textToInsert();
        expanded.selectionStart = int(expanded.text.size());
    }
    const QByteArray text = expanded.text.toUtf8();
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    int end = position;
    if (replaceWord) {
        const int length = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
        while (end < length
               && isIdentifierByte(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, end))))
            ++end;
    }
    const int start = prefixStart_;
    close();
    editor_->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(start),
                           static_cast<long>(end));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(text.size()),
                           text.constData());
    // The cursor in the parentheses, the first argument selected:
    // typing replaces it.
    const int selectionStart = start + int(expanded.text.left(expanded.selectionStart).toUtf8().size());
    const int selectionEnd =
        selectionStart + int(expanded.text.mid(expanded.selectionStart, expanded.selectionLength).toUtf8().size());
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSEL, static_cast<unsigned long>(selectionStart),
                           static_cast<long>(selectionEnd));
    // An import that comes with it, in the same undo step.
    applyEdits(item.additionalEdits, start, int(text.size()), end - start);
    editor_->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);

    if (item.additionalEdits.isEmpty() && item.resolveEdits) {
        // Known once chosen (jdtls): applied when the server answers.
        QPointer<CompletionController> self(this);
        const int inserted = int(text.size());
        const int replaced = end - start;
        item.resolveEdits([self, start, inserted, replaced](const QList<TextEdit> &edits) {
            if (!self || edits.isEmpty())
                return;
            self->editor_->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
            self->applyEdits(edits, start, inserted, replaced);
            self->editor_->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
        });
    }
}

int CompletionController::positionOf(int line, int column) const
{
    const int lines = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETLINECOUNT));
    if (line >= lines)
        return static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    const int lineStart = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line));
    // Columns count UTF-16 units: the line's text says how many bytes.
    return lineStart + int(editor_->text(line).left(column).toUtf8().size());
}

void CompletionController::applyEdits(const QList<TextEdit> &edits, int insertStart, int insertedBytes,
                                      int replacedBytes)
{
    struct Range {
        int start;
        int end;
        QByteArray text;
    };
    QList<Range> ranges;
    for (const TextEdit &edit : edits) {
        // Positions of the text before the insertion: those after it move.
        const auto shifted = [&](int position) {
            return position > insertStart ? position + insertedBytes - replacedBytes : position;
        };
        ranges.append({shifted(positionOf(edit.startLine, edit.startColumn)),
                       shifted(positionOf(edit.endLine, edit.endColumn)), edit.newText.toUtf8()});
    }
    // From the end, so that each edit leaves the next positions valid.
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start > b.start; });
    for (const Range &range : ranges) {
        editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(range.start),
                               static_cast<long>(range.end));
        editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(range.text.size()),
                               range.text.constData());
    }
}

bool CompletionController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != editor_)
        return false;
    if (event->type() == QEvent::FocusOut) {
        close();
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return false;
    if (!isActive())
        return false;

    const auto *key = static_cast<QKeyEvent *>(event);
    const bool plain = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    bool handled = plain;
    switch (key->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Tab:
        break;
    default:
        handled = false;
    }
    if (!handled)
        return false;
    // Ours, not a menu shortcut (F-keys apart, these have none).
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    switch (key->key()) {
    case Qt::Key_Escape: close(); break;
    case Qt::Key_Up: popup_->moveSelection(-1); break;
    case Qt::Key_Down: popup_->moveSelection(1); break;
    case Qt::Key_PageUp: popup_->moveSelection(-popup_->pageRows()); break;
    case Qt::Key_PageDown: popup_->moveSelection(popup_->pageRows()); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: accept(false); break;
    case Qt::Key_Tab: accept(true); break;
    }
    return true;
}
