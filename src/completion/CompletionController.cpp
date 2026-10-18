#include "CompletionController.h"

#include "CodeScan.h"
#include "CompletionPopup.h"
#include "CompletionService.h"
#include "DocComments.h"
#include "ExpectedType.h"
#include "FullLine.h"
#include "LocalCompletion.h"
#include "Snippet.h"
#include "SnippetSession.h"
#include "editor/EditorWidget.h"
#include "syntax/ScintillaStyles.h"

#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <Qsci/qscilexer.h>

namespace {

// Pause after a keystroke before the automatic popup: none while typing
// fast enough to know the word (Eager), a real pause (Calm).
constexpr int eagerDelayMs = 60;
constexpr int calmDelayMs = 400;
// Letters of a name before the Calm popup.
constexpr int calmMinimumLetters = 3;
// A candidate erased within this time after being inserted was a wrong one.
constexpr int rejectionWindowMs = 3000;
// Pause at the end of a line before asking the Full Line model.
constexpr int fullLineDelayMs = 300;
// Code sent to the model around the cursor.
constexpr int fullLineBeforeBytes = 6000;
constexpr int fullLineAfterBytes = 2000;
// Text of the other open files read by extended completion.
constexpr qsizetype maxOtherTextBytes = 4 * 1024 * 1024;
// Scintilla indicator of the expression to choose (SnippetSession uses
// 10 and 11).
constexpr int choiceIndicator = 12;

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

    void show(const QString &text,
              const QString &key,
              const QFont &font,
              const QColor &color,
              QPoint topLeft,
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
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
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
    fullLineTimer_.setSingleShot(true);
    connect(&fullLineTimer_, &QTimer::timeout, this, &CompletionController::requestFullLine);
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
    // The Full Line suggestion only stays where it was made.
    connect(editor_, &QsciScintilla::cursorPositionChanged, this, [this] {
        if (fullLine_.isEmpty())
            return;
        if (editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS) != fullLinePosition_)
            clearFullLine();
    });
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
        if (choosing_)
            return;
        // A second time: more candidates; a third, the documentation.
        if (mode_ == Mode::Basic)
            start(true, trigger_, Mode::Extended);
        else
            toggleDocumentation();
        return;
    }
    start(true, {});
}

void CompletionController::completeSmart()
{
    if (choosing_)
        close();
    start(true, {}, Mode::Smart);
}

void CompletionController::toggleDocumentation()
{
    if (isActive())
        popup_->setDocumentationVisible(!popup_->isDocumentationVisible());
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
    while (start > 0 &&
           isIdentifierByte(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, start - 1))))
        --start;
    return start;
}

bool CompletionController::insideCommentOrString(int position) const
{
    if (position <= 0)
        return false;
    // The text just typed may not be styled yet.
    const long lineStart =
        editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE,
                               editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
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

bool CompletionController::insideDocComment(int position) const
{
    if (!insideCommentOrString(position))
        return false;
    // The comment's start, and after it the declaration documented.
    const QByteArray text = editor_->utf8Bytes().toByteArray();
    return DocComments::commentStart(text, position, editor_->languageId()) >= 0;
}

bool CompletionController::inModulePath(int position) const
{
    static const QStringList languages = {"javascript", "typescript", "tsx"};
    return languages.contains(editor_->languageId()) &&
           CodeScan::inModulePath(editor_->utf8Range(std::max(0, position - 1000), position), std::min(position, 1000));
}

QChar CompletionController::docCommentTrigger(QChar typed, int position) const
{
    const bool doxygen = editor_->languageId() == QLatin1String("c") || editor_->languageId() == QLatin1String("cpp");
    if (typed == u'@' || (typed == u'\\' && doxygen))
        return typed;
    if (typed == u' ') {
        // "@param |": the parameters.
        const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
        const int lineStart = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line));
        static const QRegularExpression paramTag(
            QStringLiteral("[@\\\\](?:param|arg|argument|tparam|template)(?:\\[[^\\]]*\\])?\\s+(?:\\{[^}]*\\}\\s*)?$"));
        if (paramTag.match(textBetween(lineStart, position)).hasMatch())
            return typed;
        return {};
    }
    if (isIdentifierCharacter(typed)) {
        // "@pa|": a tag being typed.
        const int start = identifierStart(position);
        if (start > 0) {
            const char before = static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, start - 1));
            if (before == '@' || (before == '\\' && doxygen))
                return {before};
        }
    }
    return {};
}

void CompletionController::onCharacterAdded(int character)
{
    if (active_) {
        if (choosing_) {
            close(); // typing gives the choice up
        } else {
            refilter();
            if (active_)
                return;
        }
        // Closed by a character that ends the identifier: it may open the
        // next completion ("object." after "object").
    }
    if (editor_->isReadOnly())
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const QChar typed(character);
    followFullLine(typed, position);
    if ((typed == u'\n' || typed == u'\r') && writeDocComment(position))
        return;
    scheduleFullLine(position);
    const QChar previous =
        position >= 2 ? QChar(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, position - 2)))
                      : QChar();
    const AutoCompletion mode = editor_->options().autoCompletion;
    if (mode == AutoCompletion::Off)
        return;
    const auto open = [this, mode](QChar trigger) {
        if (mode == AutoCompletion::Eager && !trigger.isNull()) {
            start(false, trigger);
        } else {
            pendingTrigger_ = trigger;
            autoTimer_.start(mode == AutoCompletion::Eager ? eagerDelayMs : calmDelayMs);
        }
    };
    if (insideCommentOrString(position)) {
        // "from './|'": the modules and files, from the language server.
        if (inModulePath(position)) {
            if (typed == u'\'' || typed == u'"' || typed == u'/')
                open(typed);
            else if (isIdentifierCharacter(typed))
                open({});
            return;
        }
        // In a documentation comment: its tags and parameters.
        if (!insideDocComment(position))
            return;
        const QChar trigger = docCommentTrigger(typed, position);
        if (trigger.isNull())
            return;
        // "@pa": the identifier typed after the "@".
        open(isIdentifierCharacter(typed) ? QChar() : trigger);
        return;
    }
    if (service_->isTrigger(editor_->languageId(), typed, previous)) {
        open(typed);
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
    open({});
}

void CompletionController::start(bool explicitRequest, QChar triggerCharacter, Mode mode)
{
    autoTimer_.stop();
    clearFullLine();
    if (editor_->isReadOnly())
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    // Typing went on past the identifier since the timer started.
    if (!explicitRequest && triggerCharacter.isNull() && identifierStart(position) == position)
        return;
    // The trigger character was erased or typed past while waiting (Calm).
    if (!triggerCharacter.isNull() && !explicitRequest &&
        (position < 1 || QChar(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT,
                                                                        position - 1))) != triggerCharacter))
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
    context.mode = mode;
    const bool inText = insideCommentOrString(position);
    context.inDocComment = inText && DocComments::commentStart(context.text, position, context.languageId) >= 0;
    context.inModulePath = inText && inModulePath(position);
    if (context.inModulePath && triggerCharacter.isNull() && context.prefixStart > 0) {
        // Right after the quote or a "/": the server lists what comes next.
        const char before = context.text[context.prefixStart - 1];
        if (before == '\'' || before == '"' || before == '/')
            context.triggerCharacter = QChar(before);
    }
    if (mode == Mode::Smart)
        context.expectedType = ExpectedType::at(context.text, context.prefixStart, context.languageId);
    if (mode == Mode::Extended) {
        // The words of the other open files of the language.
        qsizetype total = 0;
        for (const EditorWidget *other : editor_->window()->findChildren<EditorWidget *>()) {
            if (other == editor_ || other->languageId() != context.languageId)
                continue;
            QByteArray text = other->utf8Bytes().toByteArray();
            total += text.size();
            if (total > maxOtherTextBytes)
                break;
            context.otherTexts.append(std::move(text));
        }
    }

    const bool restarting = active_ && prefixStart_ == context.prefixStart; // the same identifier, typed further
    if (choosing_)
        close();
    active_ = true;
    mode_ = mode;
    trigger_ = triggerCharacter;
    expectedType_ = context.expectedType;
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
    if (choosing_) {
        if (prefix != listedPrefix_)
            close();
        return;
    }
    if (incomplete_ && prefix != requestedPrefix_ && prefix.startsWith(requestedPrefix_)) {
        start(false, {}, mode_); // the server's next part; the list stays meanwhile
        return;
    }
    // Typing a character signals it several times (character added, text
    // and cursor changed): the list is filtered once per change.
    if (!listStale_ && prefix == listedPrefix_)
        return;
    listStale_ = false;
    listedPrefix_ = prefix;
    if (popup_->setItems(items_, prefix, mode_ == Mode::Extended) == 0) {
        popup_->hide(); // nothing yet: answers may still come
        updatePreview();
        return;
    }
    const bool opening = !popup_->isVisible();
    showPopup();
    updatePreview();
    if (opening) {
        announcedLabel_ = popup_->current() ? popup_->current()->label : QString();
        CompletionPopup::announce(tr("%n suggestions", nullptr, popup_->count()) + QStringLiteral(". ") +
                                  popup_->describeCurrent());
    }
}

void CompletionController::showPopup()
{
    const QWidget *viewport = editor_->viewport();
    const int x = static_cast<int>(
        editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0L, static_cast<long>(prefixStart_)));
    const int y = static_cast<int>(
        editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0L, static_cast<long>(prefixStart_)));
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line_));
    const QWidget *parent = popup_->parentWidget();
    // Aligned with the identifier's text: the badge's width to the left.
    const QPoint below = viewport->mapTo(parent, QPoint(x - 34, y + lineHeight + 2));
    const QPoint top = viewport->mapTo(parent, QPoint(x, y - 2));
    popup_->applyTheme(editor_->theme());
    updateKeyHint();
    popup_->showAt(below, top.y());
}

void CompletionController::updateKeyHint()
{
    if (!popup_)
        return;
    if (choosing_) {
        popup_->setKeyHint(
            tr("Expression to wrap   %1 choose   Esc cancel")
                .arg(editor_->options().completionAcceptsEnter ? QStringLiteral("↵") : QStringLiteral("⇥")));
        return;
    }
    QString keys = editor_->options().completionAcceptsEnter ? tr("↵ insert   ⇥ replace word   Ctrl+→ next word")
                                                             : tr("⇥ insert   Ctrl+→ next word");
    switch (mode_) {
    case Mode::Basic:
        keys += tr("   Ctrl+Space more");
        break;
    case Mode::Extended:
        keys = tr("All suggestions   ") + keys;
        break;
    case Mode::Smart:
        keys = (expectedType_.isEmpty() ? tr("Smart") : tr("Expected %1").arg(expectedType_.simplified())) +
               QStringLiteral("   ") + keys;
        break;
    }
    popup_->setKeyHint(keys + tr("   Ctrl+Q details   Esc close"));
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
    if (choosing_)
        highlightChoice();
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
    const bool fits = item && !choosing_ && item->label.size() > prefix.size() && item->label.startsWith(prefix) &&
                      editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line_) == position;
    if (!fits) {
        if (!isActive() && !fullLine_.isEmpty())
            showFullLine();
        else
            preview->hide();
        return;
    }
    const int x =
        static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0L, long(position)));
    const int y =
        static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0L, long(position)));
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line_));
    const Theme &theme = editor_->theme();
    preview->show(item->label.mid(prefix.size()),
                  editor_->options().completionAcceptsEnter ? QStringLiteral("↵") : QStringLiteral("Tab"), theme.font,
                  theme.mutedText, QPoint(x, y), lineHeight);
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
    if (choosing_) {
        choosing_ = false;
        choosingItem_ = {};
        editor_->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, choiceIndicator);
        editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORCLEARRANGE, 0L,
                               editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    }
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
    CompletionItem item = *popup_->current(); // close() clears the list
    if (choosing_) {
        // The expression chosen: the item's insertion for it.
        const CompletionItem::Choice choice = choosingItem_.choices.value(item.order);
        item = choosingItem_;
        item.insertText = choice.insertText;
        item.replaceStart = choice.replaceStart;
        item.choices.clear();
    } else if (item.choices.size() > 1) {
        beginChoice(item);
        return;
    }
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
        while (end < length &&
               isIdentifierByte(static_cast<char>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, end))))
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
        const QPointer<CompletionController> self(this);
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
    // A call: its parameters.
    const bool callable = item.kind == CompletionItem::Kind::Function || item.kind == CompletionItem::Kind::Method;
    if (callable && expanded.text.contains(QLatin1Char('(')) && !expanded.text.endsWith(QLatin1String("()")))
        emit callInserted();
}

void CompletionController::beginChoice(const CompletionItem &item)
{
    choosing_ = true;
    choosingItem_ = item;
    QList<CompletionItem> rows;
    for (int i = 0; i < item.choices.size(); ++i) {
        CompletionItem row;
        row.label = item.choices[i].label;
        row.kind = CompletionItem::Kind::Snippet;
        row.documentation = Snippet::expand(item.choices[i].insertText).text;
        row.order = i; // nearest first
        rows.append(row);
    }
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    listedPrefix_ = textBetween(prefixStart_, position);
    listStale_ = false;
    popup_->setItems(rows, QString());
    showPopup();
    highlightChoice();
    updatePreview();
    announcedLabel_ = popup_->current() ? popup_->current()->label : QString();
    CompletionPopup::announce(tr("Choose the expression") + QStringLiteral(". ") + popup_->describeCurrent());
}

void CompletionController::highlightChoice()
{
    const CompletionItem *current = popup_ ? popup_->current() : nullptr;
    editor_->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, choiceIndicator);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORCLEARRANGE, 0L,
                           editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    if (!choosing_ || !current)
        return;
    const CompletionItem::Choice choice = choosingItem_.choices.value(current->order);
    // Up to the dot ("?." in JavaScript).
    int end = prefixStart_ - 1;
    if (end > 0 && editor_->SendScintilla(QsciScintillaBase::SCI_GETCHARAT, end - 1) == '?')
        --end;
    if (choice.replaceStart < 0 || choice.replaceStart >= end)
        return;
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETSTYLE, choiceIndicator, QsciScintillaBase::INDIC_STRAIGHTBOX);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETFORE, choiceIndicator, editor_->theme().accent);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETALPHA, choiceIndicator, 50);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETOUTLINEALPHA, choiceIndicator, 200);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETUNDER, choiceIndicator, true);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORFILLRANGE, static_cast<unsigned long>(choice.replaceStart),
                           static_cast<long>(end - choice.replaceStart));
}

void CompletionController::insertSnippet(int start, int end, const QString &snippet)
{
    const ExpandedSnippet expanded = Snippet::expand(snippet);
    const QByteArray text = expanded.text.toUtf8();
    editor_->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(start),
                           static_cast<long>(end));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(text.size()),
                           text.constData());
    const int selectionStart = start + int(expanded.text.left(expanded.selectionStart).toUtf8().size());
    const int selectionEnd =
        selectionStart + int(expanded.text.mid(expanded.selectionStart, expanded.selectionLength).toUtf8().size());
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSEL, static_cast<unsigned long>(selectionStart),
                           static_cast<long>(selectionEnd));
    snippet_->start(expanded, start);
    editor_->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
}

bool CompletionController::writeDocComment(int position)
{
    const QString languageId = editor_->languageId();
    static const QStringList languages = {"c", "cpp", "java", "javascript", "typescript", "tsx"};
    if (!languages.contains(languageId))
        return false;
    const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    if (line == 0)
        return false;
    // Enter at the end of a line holding only "/**".
    const int previousStart =
        static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line - 1));
    const int previousEnd =
        static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line - 1));
    const QString opener = textBetween(previousStart, previousEnd).trimmed();
    if (opener != QLatin1String("/**") && opener != QLatin1String("/*!"))
        return false;
    const int lineEnd = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line));
    if (!textBetween(position, lineEnd).trimmed().isEmpty())
        return false;
    // A comment already closed: "/**" typed above an old one.
    const QByteArray text = editor_->utf8Bytes().toByteArray();
    const int closing = int(text.indexOf("*/", position));
    const int opening = int(text.indexOf("/*", position));
    if (closing >= 0 && (opening < 0 || closing < opening))
        return false;
    const int openerEnd = previousStart + int(text.mid(previousStart, previousEnd - previousStart).indexOf("/*")) + 3;
    const QString indent = CodeScan::lineIndent(text, previousStart);
    insertSnippet(openerEnd, lineEnd, DocComments::stub(text, lineEnd, languageId, indent));
    return true;
}

// Full Line completion

void CompletionController::followFullLine(QChar typed, int position)
{
    if (fullLine_.isEmpty())
        return;
    // Typed as shown: the rest stays.
    if (position == fullLinePosition_ + int(QString(typed).toUtf8().size()) && fullLine_.startsWith(typed)) {
        fullLine_.remove(0, 1);
        fullLinePosition_ = position;
        if (fullLine_.trimmed().isEmpty())
            clearFullLine();
        else
            showFullLine();
        return;
    }
    clearFullLine();
}

void CompletionController::scheduleFullLine(int position)
{
    if (!service_->fullLineModel() || active_ || snippet_->isActive() || !fullLine_.isEmpty())
        return;
    const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    // Where the gray text fits: at the end of a line.
    if (editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line) != position)
        return;
    fullLineTimer_.start(fullLineDelayMs);
}

void CompletionController::requestFullLine()
{
    FullLineModel *model = service_->fullLineModel();
    if (!model || active_ || snippet_->isActive() || editor_->isReadOnly() || !editor_->hasFocus())
        return;
    const int position = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    const int lineStart = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line));
    if (editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line) != position ||
        textBetween(lineStart, position).trimmed().isEmpty() || insideCommentOrString(position))
        return;
    const QByteArray text = editor_->utf8Bytes().toByteArray();
    // Whole lines before, so that no character is cut.
    int from = std::max(0, position - fullLineBeforeBytes);
    if (from > 0)
        from = std::max(int(text.indexOf('\n', from)) + 1, from);
    int to = std::min<int>(int(text.size()), position + fullLineAfterBytes);
    if (to < text.size())
        to = std::max(int(text.lastIndexOf('\n', to)), position);
    const int generation = ++fullLineGeneration_;
    const QPointer<CompletionController> self(this);
    model->suggest(QString::fromUtf8(text.mid(from, position - from)),
                   QString::fromUtf8(text.mid(position, to - position)), editor_->languageId(),
                   [self, generation, position](const QString &suggestion) {
                       if (!self || generation != self->fullLineGeneration_ || self->active_ || suggestion.isEmpty())
                           return;
                       if (self->editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS) != position)
                           return;
                       self->fullLine_ = suggestion;
                       self->fullLinePosition_ = position;
                       self->showFullLine();
                   });
}

void CompletionController::showFullLine()
{
    if (fullLine_.isEmpty() || isActive())
        return;
    auto *preview = static_cast<InlinePreview *>(preview_);
    const long position = fullLinePosition_;
    const int line = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position));
    const int x = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0L, position));
    const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0L, position));
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line));
    const Theme &theme = editor_->theme();
    preview->show(fullLine_, QStringLiteral("Tab"), theme.font, theme.mutedText, QPoint(x, y), lineHeight);
}

void CompletionController::clearFullLine()
{
    fullLineTimer_.stop();
    ++fullLineGeneration_;
    const bool shown = !fullLine_.isEmpty();
    fullLine_.clear();
    fullLinePosition_ = -1;
    if (FullLineModel *model = service_->fullLineModel())
        model->cancel();
    if (shown && !isActive())
        preview_->hide();
}

void CompletionController::acceptFullLine(bool nextWordOnly)
{
    if (fullLine_.isEmpty())
        return;
    QString text = fullLine_;
    if (nextWordOnly) {
        // Blanks, then a word or a run of punctuation.
        qsizetype end = 0;
        while (end < text.size() && text[end].isSpace())
            ++end;
        if (end < text.size() && isIdentifierCharacter(text[end])) {
            while (end < text.size() && isIdentifierCharacter(text[end]))
                ++end;
        } else {
            while (end < text.size() && !text[end].isSpace() && !isIdentifierCharacter(text[end]))
                ++end;
        }
        text = text.left(end);
    }
    const QString rest = fullLine_.mid(text.size());
    const int position = fullLinePosition_;
    const QByteArray bytes = text.toUtf8();
    clearFullLine();
    editor_->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(position),
                           static_cast<long>(position));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(bytes.size()),
                           bytes.constData());
    editor_->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
    editor_->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, static_cast<unsigned long>(position + bytes.size()));
    if (!rest.trimmed().isEmpty()) {
        fullLine_ = rest;
        fullLinePosition_ = position + int(bytes.size());
        showFullLine();
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

void CompletionController::applyEdits(const QList<TextEdit> &edits,
                                      int insertStart,
                                      int insertedBytes,
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
        clearFullLine();
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (!isActive() && !fullLine_.isEmpty()) {
        // The Full Line suggestion: Tab, Ctrl+Right, Escape.
        const bool plain = key->modifiers() == Qt::NoModifier;
        const bool tab = key->key() == Qt::Key_Tab && plain;
        const bool word = key->key() == Qt::Key_Right && key->modifiers() == Qt::ControlModifier;
        const bool escape = key->key() == Qt::Key_Escape && plain;
        if (tab || word || escape) {
            if (event->type() == QEvent::KeyPress) {
                if (escape)
                    clearFullLine();
                else
                    acceptFullLine(word);
            }
            event->accept();
            return true;
        }
    }
    if (!isActive()) {
        if (!snippet_->isActive())
            return false;
        // The snippet's placeholders: Tab, Shift+Tab, Escape.
        const bool shift = key->modifiers() == Qt::ShiftModifier;
        const bool plain = key->modifiers() == Qt::NoModifier;
        const bool ours = (key->key() == Qt::Key_Tab && plain) || key->key() == Qt::Key_Backtab ||
                          (key->key() == Qt::Key_Tab && shift) || (key->key() == Qt::Key_Escape && plain);
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
        if (event->type() == QEvent::KeyPress && !choosing_)
            acceptNextWord();
        event->accept();
        return true;
    }
    // Ctrl+Q: the documentation beside the list, not the editor's.
    if (key->key() == Qt::Key_Q && key->modifiers() == Qt::ControlModifier) {
        if (event->type() == QEvent::KeyPress)
            toggleDocumentation();
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
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Up:
        popup_->moveSelection(-1);
        break;
    case Qt::Key_Down:
        popup_->moveSelection(1);
        break;
    case Qt::Key_PageUp:
        popup_->moveSelection(-popup_->pageRows());
        break;
    case Qt::Key_PageDown:
        popup_->moveSelection(popup_->pageRows());
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        accept(false);
        break;
    case Qt::Key_Tab:
        accept(true);
        break;
    }
    return true;
}
