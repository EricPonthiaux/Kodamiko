#include "EditorWidget.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/ISyntaxHighlighter.h"
#include "syntax/LanguageRegistry.h"

#include <QFile>

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
{
    configureEditing();
    installHighlighter(LanguageRegistry::plainText);
    connect(this, &QsciScintilla::linesChanged, this, &EditorWidget::updateLineNumberMargin);
}

EditorWidget::~EditorWidget()
{
    // Le lexer appartient au highlighter : QScintilla ne doit plus y toucher.
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

    format_ = decoded->format;
    // Remplacement (et non setText) pour que Ctrl+Z ramène le texte précédent.
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

void EditorWidget::applyTheme(const Theme &theme)
{
    theme_ = theme;
    highlighter_->applyTheme(theme);

    // Après le highlighter : un lexer QScintilla réinitialise ces couleurs.
    setCaretForegroundColor(theme.caret);
    setCaretLineBackgroundColor(theme.caretLine);
    setSelectionBackgroundColor(theme.selection);
    SendScintilla(SCI_SETSELFORE, false, 0L); // la sélection garde les couleurs syntaxiques
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    setMatchedBraceBackgroundColor(theme.braceMatch);
    setMatchedBraceForegroundColor(theme.foreground);
    setUnmatchedBraceForegroundColor(QColor("#e06c75"));
    setIndentationGuidesBackgroundColor(theme.background);
    setIndentationGuidesForegroundColor(theme.caretLine);
    updateLineNumberMargin();
}

void EditorWidget::updateLineNumberMargin()
{
    const QString widest = QString(QString::number(lines()).size() + 1, QLatin1Char('9'));
    setMarginWidth(0, widest);
}
