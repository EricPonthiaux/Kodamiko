#include "LexillaHighlighter.h"

#include "ScintillaStyles.h"
#include "ui/Theme.h"

#include <Qsci/qscilexer.h>
#include <Qsci/qsciscintilla.h>

namespace {

// QScintilla lexers each have their own style numbers but describe every
// style in English ("Double-quoted string", "Comment line"...). We infer the
// theme category to use from that, which avoids a mapping table per lexer.
QString categoryForLexerStyle(const QString &description)
{
    const QString text = description.toLower();
    const auto mentions = [&text](std::initializer_list<const char *> words) {
        for (const char *word : words) {
            if (text.contains(QLatin1String(word)))
                return true;
        }
        return false;
    };

    if (mentions({"comment"}))
        return QStringLiteral("comment");
    if (mentions({"added", "inserted"}))
        return QStringLiteral("diff.plus");
    if (mentions({"removed", "deleted"}))
        return QStringLiteral("diff.minus");
    if (mentions({"header", "heading", "position"}))
        return QStringLiteral("heading");
    if (mentions({"string", "quot", "character", "heredoc", "literal", "cdata"}))
        return QStringLiteral("string");
    if (mentions({"number", "numeric"}))
        return QStringLiteral("number");
    if (mentions({"keyword", "reserved", "preprocessor", "directive", "command", "instruction"}))
        return QStringLiteral("keyword");
    if (mentions({"operator"}))
        return QStringLiteral("operator");
    if (mentions({"attribute"}))
        return QStringLiteral("attribute");
    if (mentions({"tag", "element", "entity"}))
        return QStringLiteral("tag");
    if (mentions({"function", "target"}))
        return QStringLiteral("function");
    if (mentions({"class", "type"}))
        return QStringLiteral("type");
    if (mentions({"variable", "parameter", "property", "key"}))
        return QStringLiteral("property");
    if (mentions({"error"}))
        return QStringLiteral("variable.builtin");
    if (mentions({"emphasis", "strong", "link", "url"}))
        return QStringLiteral("function");
    return QString();
}

} // namespace

LexillaHighlighter::LexillaHighlighter(std::unique_ptr<QsciLexer> lexer)
    : lexer_(std::move(lexer))
{
}

LexillaHighlighter::~LexillaHighlighter()
{
    if (editor_ && editor_->lexer() == lexer_.get())
        editor_->setLexer(nullptr);
}

void LexillaHighlighter::attach(QsciScintilla *editor)
{
    editor_ = editor;
    editor_->setLexer(lexer_.get());
}

void LexillaHighlighter::applyTheme(const Theme &theme)
{
    // The family Scintilla is given carries the ligatures' choice (see
    // AppFonts::scintillaFamily).
    QFont font = theme.font;
    font.setFamily(QString::fromUtf8(AppFonts::scintillaFamily(theme.font, theme.editorLigatures)));
    lexer_->setDefaultFont(font);
    lexer_->setDefaultColor(theme.foreground);
    lexer_->setDefaultPaper(theme.background);

    QFont italicFont = font;
    italicFont.setItalic(true);
    QFont boldFont = font;
    boldFont.setWeight(QFont::Weight(ScintillaStyles::styleWeight(theme.font, true)));

    for (int style = 0; style < 128; ++style) {
        const QString description = lexer_->description(style);
        if (description.isEmpty())
            continue;

        lexer_->setPaper(theme.background, style);
        const int themeIndex = theme.syntaxStyleIndex(categoryForLexerStyle(description));
        if (themeIndex < 0) {
            lexer_->setColor(theme.foreground, style);
            lexer_->setFont(font, style);
            continue;
        }
        const TextStyle &textStyle = theme.syntaxStyles[themeIndex].second;
        lexer_->setColor(textStyle.foreground, style);
        lexer_->setFont(textStyle.bold ? boldFont : textStyle.italic ? italicFont : font, style);
    }

    // QsciScintilla only rereads the lexer's styles on setLexer().
    editor_->setLexer(lexer_.get());
}
