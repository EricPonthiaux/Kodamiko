#include "EditorWidget.h"

#include "ChangeGutter.h"
#include "CodeFormatter.h"
#include "syntax/ScintillaStyles.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/ISyntaxHighlighter.h"
#include "syntax/LanguageRegistry.h"

#include <Qsci/qscicommandset.h>

#include <QFile>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <vector>

namespace {

// Margins, from the left: line numbers, change marks (ChangeGutter), folds.
constexpr int foldMargin = 2;

// Navigation shortcuts (see CodeNavigator) until MainWindow passes the
// menu's shortcuts.
QList<int> reservedKeys = {(Qt::CTRL | Qt::Key_B).toCombined(), (Qt::ALT | Qt::Key_Left).toCombined(),
                           (Qt::ALT | Qt::Key_Right).toCombined()};
std::vector<EditorWidget *> liveEditors;

QsciScintilla::EolMode toEolMode(LineEnding lineEnding)
{
    switch (lineEnding) {
    case LineEnding::CrLf: return QsciScintilla::EolWindows;
    case LineEnding::Cr: return QsciScintilla::EolMac;
    case LineEnding::Lf: break;
    }
    return QsciScintilla::EolUnix;
}

} // namespace

EditorWidget::EditorWidget(const Theme &theme, QWidget *parent)
    : QsciScintilla(parent)
    , theme_(theme)
    , changeGutter_(new ChangeGutter(this))
{
    configureEditing();
    installHighlighter(LanguageRegistry::plainText);
    setOptions(options_);
    connect(this, &QsciScintilla::linesChanged, this, &EditorWidget::updateLineNumberMargin);
    connect(this, &QsciScintilla::cursorPositionChanged, this, &EditorWidget::revealCursorLine);
    // A click on a fold mark can hide the cursor's line: the cursor goes to
    // the fold's header, so that typing never edits hidden text. Queued,
    // because QScintilla folds after this signal.
    connect(this, &QsciScintillaBase::SCN_MARGINCLICK, this, [this](int, int, int margin) {
        if (margin == foldMargin)
            QTimer::singleShot(0, this, &EditorWidget::moveCursorOutOfFolds);
    });
    liveEditors.push_back(this);
}

EditorWidget::~EditorWidget()
{
    std::erase(liveEditors, this);
    // The lexer belongs to the highlighter: QScintilla must not touch it anymore.
    setLexer(nullptr);
}

void EditorWidget::configureEditing()
{
    setUtf8(true);
    setMarginType(0, QsciScintilla::NumberMargin);
    setMarginLineNumbers(0, true);
    setMarginWidth(1, 0);
    setCaretLineVisible(true);
    setBraceMatching(QsciScintilla::SloppyBraceMatch);
    setAutoIndent(true);
    setIndentationsUseTabs(false);
    setTabWidth(4);
    setIndentationGuides(true);
    setBackspaceUnindents(true);
    setScrollWidth(1);
    setScrollWidthTracking(true);
    updateLineNumberMargin();

    for (const QsciCommand *command : standardCommands()->commands())
        defaultCommandKeys_.append({command->key(), command->alternateKey()});
    releaseReservedKeys();
}

void EditorWidget::setReservedKeys(const QList<QKeySequence> &shortcuts)
{
    reservedKeys.clear();
    for (const QKeySequence &shortcut : shortcuts) {
        // For "Ctrl+K, Ctrl+O", QScintilla must leave Ctrl+K.
        if (!shortcut.isEmpty())
            reservedKeys.append(shortcut[0].toCombined());
    }
    for (EditorWidget *editor : liveEditors)
        editor->releaseReservedKeys();
}

void EditorWidget::releaseReservedKeys()
{
    const QList<QsciCommand *> commands = standardCommands()->commands();
    for (int i = 0; i < commands.size() && i < defaultCommandKeys_.size(); ++i) {
        const auto [key, alternateKey] = defaultCommandKeys_[i];
        commands[i]->setKey(reservedKeys.contains(key) ? 0 : key);
        commands[i]->setAlternateKey(reservedKeys.contains(alternateKey) ? 0 : alternateKey);
    }
}

void EditorWidget::setOptions(const EditorOptions &options)
{
    if (!options.codeFolding && options_.codeFolding)
        foldEverything(false); // no hidden lines without the margin to show them
    options_ = options;
    setFolding(options.codeFolding ? QsciScintilla::BoxedTreeFoldStyle : QsciScintilla::NoFoldStyle, foldMargin);
    setWrapMode(options.wrapLines ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
    setWrapIndentMode(QsciScintilla::WrapIndentIndented);
    setWhitespaceVisibility(options.showWhitespace ? QsciScintilla::WsVisible : QsciScintilla::WsInvisible);
    applyFoldColors();
}

void EditorWidget::applyFoldColors()
{
    // QScintilla's fold marks are black and white: those of the margin
    // instead, the boxes filled with the margin's background.
    setFoldMarginColors(theme_.marginBackground, theme_.marginBackground);
    for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker) {
        SendScintilla(SCI_MARKERSETFORE, marker, theme_.marginBackground);
        SendScintilla(SCI_MARKERSETBACK, marker, theme_.marginForeground);
    }
    setWhitespaceForegroundColor(theme_.marginForeground);
}

void EditorWidget::foldBlock(bool fold)
{
    if (!options_.codeFolding)
        return;
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    const bool isHeader = SendScintilla(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG;

    if (!fold) {
        if (isHeader && !SendScintilla(SCI_GETFOLDEXPANDED, line))
            SendScintilla(SCI_FOLDLINE, line, SC_FOLDACTION_EXPAND);
        return;
    }
    // The block that starts on the cursor's line, else the one around it;
    // when it is already folded, the one around that.
    int header = isHeader ? line : static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, line));
    if (header >= 0 && !SendScintilla(SCI_GETFOLDEXPANDED, header))
        header = static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, header));
    if (header < 0)
        return;
    SendScintilla(SCI_FOLDLINE, header, SC_FOLDACTION_CONTRACT);
    moveCursorOutOfFolds();
}

void EditorWidget::foldEverything(bool fold)
{
    SendScintilla(SCI_FOLDALL, fold ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
    moveCursorOutOfFolds();
}

void EditorWidget::moveCursorOutOfFolds()
{
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    int visible = line;
    while (visible > 0 && !SendScintilla(SCI_GETLINEVISIBLE, visible)) {
        const int parent = static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, visible));
        visible = parent >= 0 ? parent : visible - 1;
    }
    if (visible != line)
        SendScintilla(SCI_GOTOPOS, SendScintilla(SCI_GETLINEENDPOSITION, visible));
}

void EditorWidget::revealCursorLine(int line)
{
    if (!SendScintilla(SCI_GETLINEVISIBLE, line))
        SendScintilla(SCI_ENSUREVISIBLE, line);
}

void EditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
        const QPoint point = event->position().toPoint();
        const long position = SendScintilla(SCI_POSITIONFROMPOINTCLOSE, point.x(), point.y());
        if (position >= 0) {
            SendScintilla(SCI_GOTOPOS, position);
            emit definitionRequested();
            return;
        }
    }
    QsciScintilla::mousePressEvent(event);
}

void EditorWidget::installHighlighter(const QString &languageId)
{
    setLexer(nullptr);
    highlighter_ = HighlighterFactory::create(languageId);
    highlighter_->attach(this);
    applyTheme(theme_);
    if (languageId != languageId_) {
        languageId_ = languageId;
        emit languageChanged();
    }
}

bool EditorWidget::load(const QString &path, QString *error)
{
    const std::optional<DecodedText> decoded = TextFile::load(path, error);
    if (!decoded)
        return false;

    filePath_ = path;
    format_ = decoded->format;
    setText(decoded->text);
    setEolMode(toEolMode(format_.lineEnding));
    SendScintilla(SCI_EMPTYUNDOBUFFER);
    setModified(false);
    installHighlighter(LanguageRegistry::languageForFile(path));
    changeGutter_->reloadBase();
    emit formatChanged();
    return true;
}

bool EditorWidget::save(QString *error)
{
    if (filePath_.isEmpty() || isReadOnly())
        return true;
    if (!TextFile::save(filePath_, text(), format_, error))
        return false;
    setModified(false);
    return true;
}

bool EditorWidget::reloadFromDisk(QString *error)
{
    const std::optional<DecodedText> decoded = TextFile::load(filePath_, error);
    if (!decoded)
        return false;

    const int topLine = firstVisibleLine();
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);

    if (decoded->format.lineEnding != format_.lineEnding)
        setEolMode(toEolMode(decoded->format.lineEnding));
    format_ = decoded->format;
    emit formatChanged();
    // Replace (rather than setText) so that Ctrl+Z restores the previous text.
    selectAll();
    replaceSelectedText(decoded->text);
    setModified(false);

    setCursorPosition(std::min(line, lines() - 1), index);
    setFirstVisibleLine(topLine);
    return true;
}

bool EditorWidget::matchesDisk() const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.readAll() == TextFile::encode(text(), format_);
}

bool EditorWidget::applyFormattedText(QString formatted)
{
    // Formatters may write "\n" whatever the file uses.
    formatted.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (format_.lineEnding == LineEnding::CrLf)
        formatted.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    else if (format_.lineEnding == LineEnding::Cr)
        formatted.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    const QByteArray before = text().toUtf8();
    const QByteArray after = formatted.toUtf8();
    if (before == after)
        return false;

    const int caret = static_cast<int>(SendScintilla(SCI_GETCURRENTPOS));
    const int anchor = static_cast<int>(SendScintilla(SCI_GETANCHOR));
    const int caretLine = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, caret));
    const int linesAboveCaret = caretLine - firstVisibleLine();

    // Only the part that differs is replaced: the lines around it keep their
    // markers and the undo step stays small.
    qsizetype prefix = 0;
    const qsizetype shorter = std::min(before.size(), after.size());
    while (prefix < shorter && before[prefix] == after[prefix])
        ++prefix;
    while (prefix > 0 && (static_cast<unsigned char>(before[prefix]) & 0xC0) == 0x80)
        --prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    while (suffix > 0 && (static_cast<unsigned char>(before[before.size() - suffix]) & 0xC0) == 0x80)
        --suffix;

    const QByteArray replacement = after.mid(prefix, after.size() - prefix - suffix);
    SendScintilla(SCI_BEGINUNDOACTION);
    SendScintilla(SCI_SETTARGETRANGE, static_cast<unsigned long>(prefix), static_cast<long>(before.size() - suffix));
    SendScintilla(SCI_REPLACETARGET, static_cast<unsigned long>(replacement.size()), replacement.constData());
    SendScintilla(SCI_ENDUNDOACTION);

    SendScintilla(SCI_SETSEL, static_cast<unsigned long>(CodeFormatter::mapPosition(before, anchor, after)),
                  static_cast<long>(CodeFormatter::mapPosition(before, caret, after)));
    const int newCaretLine = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, SendScintilla(SCI_GETCURRENTPOS)));
    setFirstVisibleLine(std::max(0, newCaretLine - linesAboveCaret));
    return true;
}

void EditorWidget::showReadOnlyText(const QString &text, const QString &languageId)
{
    setReadOnly(false);
    setText(text);
    setReadOnly(true);
    setModified(false);
    installHighlighter(languageId);
}

void EditorWidget::setLineEnding(LineEnding lineEnding)
{
    format_.lineEnding = lineEnding;
    setEolMode(toEolMode(lineEnding));
    if (!isReadOnly())
        convertEols(toEolMode(lineEnding));
    emit formatChanged();
}

void EditorWidget::setIndentationStyle(bool useTabs, int width)
{
    setIndentationsUseTabs(useTabs);
    setTabWidth(width);
    emit formatChanged();
}

void EditorWidget::applyTheme(const Theme &theme)
{
    theme_ = theme;
    highlighter_->applyTheme(theme);
    changeGutter_->applyTheme(theme);

    // After the highlighter: a QScintilla lexer resets these colors.
    setCaretForegroundColor(theme.caret);
    setCaretLineBackgroundColor(theme.caretLine);
    setSelectionBackgroundColor(theme.selection);
    SendScintilla(SCI_SETSELFORE, false, 0L); // the selection keeps the syntax colors
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    setMatchedBraceBackgroundColor(theme.braceMatch);
    setMatchedBraceForegroundColor(theme.foreground);
    setUnmatchedBraceForegroundColor(QColor("#e06c75"));
    setIndentationGuidesBackgroundColor(theme.background);
    setIndentationGuidesForegroundColor(theme.caretLine);
    ScintillaStyles::applyTypography(*this, theme.font);
    applyFoldColors();
    updateLineNumberMargin();
}

void EditorWidget::updateLineNumberMargin()
{
    const QString widest = QString(QString::number(lines()).size() + 1, QLatin1Char('9'));
    setMarginWidth(0, widest);
}
