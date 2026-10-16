#include "EditorWidget.h"

#include "ChangeGutter.h"
#include "syntax/ScintillaStyles.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/ISyntaxHighlighter.h"
#include "syntax/LanguageRegistry.h"

#include <Qsci/qscicommandset.h>

#include <QFile>
#include <QMouseEvent>

namespace {

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
    connect(this, &QsciScintilla::linesChanged, this, &EditorWidget::updateLineNumberMargin);
}

EditorWidget::~EditorWidget()
{
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
    setWrapMode(QsciScintilla::WrapNone);
    updateLineNumberMargin();

    // Editor navigation shortcuts (see CodeNavigator), which QScintilla
    // would intercept otherwise.
    const QList<int> reservedKeys = {(Qt::CTRL | Qt::Key_B).toCombined(), (Qt::ALT | Qt::Key_Left).toCombined(),
                                     (Qt::ALT | Qt::Key_Right).toCombined()};
    for (QsciCommand *command : standardCommands()->commands()) {
        if (reservedKeys.contains(command->key()))
            command->setKey(0);
        if (reservedKeys.contains(command->alternateKey()))
            command->setAlternateKey(0);
    }
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
    updateLineNumberMargin();
}

void EditorWidget::updateLineNumberMargin()
{
    const QString widest = QString(QString::number(lines()).size() + 1, QLatin1Char('9'));
    setMarginWidth(0, widest);
}
