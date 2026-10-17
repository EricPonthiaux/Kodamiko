#include "CompletionController.h"

#include "CompletionPopup.h"
#include "CompletionService.h"
#include "LocalCompletion.h"
#include "Snippet.h"
#include "SnippetSession.h"
#include "editor/EditorWidget.h"
#include "syntax/ScintillaStyles.h"

#include <Qsci/qscilexer.h>

#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>

namespace {

// Pause after a keystroke before the automatic popup: none while typing
// fast enough to know the word (Eager), a real pause (Calm).
constexpr int eagerDelayMs = 60;
constexpr int calmDelayMs = 400;
// Letters of a name before the Calm popup.
constexpr int calmMinimumLetters = 3;
// A candidate erased within this time after being inserted was a wrong one.
constexpr int rejectionWindowMs = 3000;

// The gray rest of the selected candidate after the cursor, with the key
// that inserts it. Drawn over the editor: Scintilla has no inline virtual
// text, so it only shows where nothing follows the cursor on the line.
class InlinePreview : public QWidget {
public:
    explicit InlinePreview(QWidget *viewport)
        : QWidget(viewport)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    void show(const QString &text, const QString &key, const QFont &font, const QColor &color, QPoint topLeft,
              int lineHeight)
    {
        text_ = text;
        key_ = key;
        font_ = font;
        color_ = color;
        lineHeight_ = lineHeight;
        QFont keyFont = font;
        keyFont.setPointSizeF(font.pointSizeF() * 0.75);
        keyWidth_ = QFontMetrics(keyFont).horizontalAdvance(key_) + 10;
        const int width = QFontMetrics(font_).horizontalAdvance(text_) + 8 + keyWidth_ + 2;
        setGeometry(topLeft.x(), topLeft.y(), width, lineHeight);
        QWidget::show();
        raise();
        update();
    }
    QString text() const { return isVisible() ? text_ : QString(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics metrics(font_);
        const int baseline = (lineHeight_ - metrics.height()) / 2 + metrics.ascent();
        painter.setFont(font_);
        painter.setPen(color_);
        painter.drawText(0, baseline, text_);
        // The key, in a small cap: how to take the suggestion.
        QFont keyFont = font_;
        keyFont.setPointSizeF(font_.pointSizeF() * 0.75);
        const QRectF cap(metrics.horizontalAdvance(text_) + 8, 2, keyWidth_, lineHeight_ - 4);
        QColor border = color_;
        border.setAlpha(140);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        painter.setFont(keyFont);
        painter.setPen(color_);
        painter.drawText(cap, Qt::AlignCenter, key_);
    }

private:
    QString text_;
    QString key_;
    QFont font_;
    QColor color_;
    int lineHeight_ = 0;
    int keyWidth_ = 0;
};

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
    connect(&autoTimer_, &QTimer::timeout, this, [this] { start(false, pendingTrigger_); });
    preview_ = new InlinePreview(editor_->viewport());
    snippet_ = new SnippetSession(editor_);
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, &CompletionController::updatePreview);
    connect(editor_->horizontalScrollBar(), &QScrollBar::valueChanged, this, &CompletionController::updatePreview);
    connect(editor_, &QsciScintillaBase::SCN_MODIFIED, this,
            [this](int position, int modificationType, const char *, int length) {
                onModified(position, modificationType, length);
            });
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
    if (isActive()) {
        popup_->setDocumentationVisible(!popup_->isDocumentationVisible());
        return;
    }
    start(true, {});
}

QString CompletionController::previewText() const
{
    return static_cast<InlinePreview *>(preview_)->text();
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
    const AutoCompletion mode = editor_->options().autoCompletion;
    if (mode == AutoCompletion::Off)
        return;
    if (service_->isTrigger(editor_->languageId(), typed, previous)) {
        if (insideCommentOrString(position))
            return;
        if (mode == AutoCompletion::Eager) {
            start(false, typed);
        } else {
            pendingTrigger_ = typed;
            autoTimer_.start(calmDelayMs);
        }
        return;
    }
    if (!isIdentifierCharacter(typed))
        return;
    // An identifier, not a number.
    const int start = identifierStart(position);
    const char first = static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, start));
    if (first >= '0' && first <= '9')
        return;
    if (mode == AutoCompletion::Calm && textBetween(start, position).size() < calmMinimumLetters)
        return;
    if (!insideCommentOrString(position)) {
        pendingTrigger_ = QChar();
        autoTimer_.start(mode == AutoCompletion::Eager ? eagerDelayMs : calmDelayMs);
    }
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
    // The trigger character was erased or typed past while waiting (Calm).
    if (!triggerCharacter.isNull() && !explicitRequest
        && (position < 1 || QChar(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, position - 1)))
                                != triggerCharacter))
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
        applyHistory(items_);
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
        connect(popup_, &CompletionPopup::currentChanged, this, &CompletionController::onCurrentChanged);
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
        updatePreview();
        return;
    }
    const bool opening = !popup_->isVisible();
    showPopup();
    updatePreview();
    if (opening) {
        announcedLabel_ = popup_->current() ? popup_->current()->label : QString();
        CompletionPopup::announce(tr("%n suggestions", nullptr, popup_->count()) + QStringLiteral(". ")
                                  + popup_->describeCurrent());
    }
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
    popup_->setKeyHint(editor_->options().completionAcceptsEnter
                           ? tr("↵ insert   ⇥ replace word   Ctrl+→ next word   Ctrl+Space details   Esc close")
                           : tr("⇥ insert   Ctrl+→ next word   Ctrl+Space details   Esc close"));
    popup_->showAt(below, top.y());
}

void CompletionController::applyHistory(QList<CompletionItem> &items) const
{
    const CompletionHistory *history = service_->history();
    const QString languageId = editor_->languageId();
    for (CompletionItem &item : items)
        item.priority += history->boost(languageId, item.label);
}

// Read to a screen reader when another candidate is selected, and shown in
// gray after the cursor.
void CompletionController::onCurrentChanged()
{
    updatePreview();
    if (!isActive() || !popup_->current())
        return;
    const QString label = popup_->current()->label;
    if (label == announcedLabel_)
        return; // the same candidate, the list refiltered
    announcedLabel_ = label;
    CompletionPopup::announce(popup_->describeCurrent());
}

void CompletionController::updatePreview()
{
    auto *preview = static_cast<InlinePreview *>(preview_);
    const CompletionItem *item = isActive() ? popup_->current() : nullptr;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const QString prefix = item ? textBetween(prefixStart_, position) : QString();
    // The rest of the name, as it will be: same case as typed, nothing
    // after the cursor on the line to hide.
    const bool fits = item && item->label.size() > prefix.size() && item->label.startsWith(prefix)
                      && editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line_) == position;
    if (!fits) {
        preview->hide();
        return;
    }
    const int x = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0L, long(position)));
    const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0L, long(position)));
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line_));
    const Theme &theme = editor_->theme();
    preview->show(item->label.mid(prefix.size()), editor_->options().completionAcceptsEnter ? QStringLiteral("↵")
                                                                                             : QStringLiteral("Tab"),
                  theme.font, theme.mutedText, QPoint(x, y), lineHeight);
}

void CompletionController::onModified(int position, int modificationType, int length)
{
    if (!inserted_)
        return;
    if (inserted_->age.elapsed() > rejectionWindowMs) {
        inserted_.reset();
        return;
    }
    Inserted &inserted = *inserted_;
    if (modificationType & QsciScintillaBase::SC_MOD_INSERTTEXT) {
        if (position <= inserted.start)
            inserted.start += length;
        return;
    }
    if (!(modificationType & QsciScintillaBase::SC_MOD_DELETETEXT))
        return;
    if (position + length <= inserted.start) {
        inserted.start -= length;
        return;
    }
    // The whole name gone (Ctrl+Z, erased): not what the user wanted.
    if (position <= inserted.start && position + length >= inserted.start + inserted.length) {
        service_->history()->recordRejected(editor_->languageId(), inserted.label);
        inserted_.reset();
    }
}

void CompletionController::acceptNextWord()
{
    if (!isActive() || !popup_->current())
        return;
    const QString label = popup_->current()->label;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const QString prefix = textBetween(prefixStart_, position);
    if (!label.startsWith(prefix, Qt::CaseInsensitive) || label.size() <= prefix.size())
        return;
    // To the next word start of the name: "get|Selected|Value", "max_|value".
    qsizetype end = prefix.size() + 1;
    while (end < label.size()) {
        const QChar c = label[end];
        if (c.isUpper() && label[end - 1].isLower())
            break;
        if (label[end - 1] == QLatin1Char('_') && c != QLatin1Char('_'))
            break;
        ++end;
    }
    if (end >= label.size()) {
        accept();
        return;
    }
    const QByteArray text = label.left(end).toUtf8();
    editor_->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(prefixStart_),
                           static_cast<long>(position));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(text.size()),
                           text.constData());
    editor_->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, static_cast<unsigned long>(prefixStart_ + text.size()));
    refilter();
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
    preview_->hide();
    announcedLabel_.clear();
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
    const int start = item.replaceStart >= 0 ? std::min(item.replaceStart, prefixStart_) : prefixStart_;
    close();
    service_->history()->recordAccepted(editor_->languageId(), item.label);
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
    // Watched for a few seconds: erased at once, it was the wrong one. From
    // here, so that an import added above moves it along.
    inserted_.reset();
    if (item.replaceStart < 0 && text.startsWith(item.label.toUtf8())) {
        inserted_ = Inserted{item.label, start, int(item.label.toUtf8().size()), {}};
        inserted_->age.start();
    }
    // Its placeholders, followed before an import moves them.
    snippet_->start(expanded, start);
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
    const auto *key = static_cast<QKeyEvent *>(event);
    if (!isActive()) {
        if (!snippet_->isActive())
            return false;
        // The snippet's placeholders: Tab, Shift+Tab, Escape.
        const bool shift = key->modifiers() == Qt::ShiftModifier;
        const bool plain = key->modifiers() == Qt::NoModifier;
        const bool ours = (key->key() == Qt::Key_Tab && plain) || key->key() == Qt::Key_Backtab
                       || (key->key() == Qt::Key_Tab && shift) || (key->key() == Qt::Key_Escape && plain);
        if (!ours)
            return false;
        if (event->type() == QEvent::KeyPress) {
            if (key->key() == Qt::Key_Escape)
                snippet_->stop();
            else if (key->key() == Qt::Key_Tab && plain)
                snippet_->next();
            else
                snippet_->previous();
        }
        event->accept();
        return true;
    }

    // Ctrl+Right: the next word of the candidate.
    if (key->key() == Qt::Key_Right && key->modifiers() == Qt::ControlModifier) {
        if (event->type() == QEvent::KeyPress)
            acceptNextWord();
        event->accept();
        return true;
    }
    // Enter starts a new line when only Tab inserts.
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !editor_->options().completionAcceptsEnter) {
        if (event->type() == QEvent::KeyPress)
            close();
        return false;
    }
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
