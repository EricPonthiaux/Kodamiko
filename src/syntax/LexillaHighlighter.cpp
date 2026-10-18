#include "LexillaHighlighter.h"

#include "ScintillaStyles.h"
#include "ui/Theme.h"

#include <Qsci/qscilexer.h>
#include <Qsci/qsciscintilla.h>

#include <algorithm>
#include <vector>

namespace {

struct CategoryRule {
    std::vector<const char *> words;
    const char *category;
};

// Checked in order: the first rule whose words the description mentions wins.
const std::vector<CategoryRule> &categoryRules()
{
    static const std::vector<CategoryRule> rules{
        {{"comment"}, "comment"},
        {{"added", "inserted"}, "diff.plus"},
        {{"removed", "deleted"}, "diff.minus"},
        {{"header", "heading", "position"}, "heading"},
        {{"string", "quot", "character", "heredoc", "literal", "cdata"}, "string"},
        {{"number", "numeric"}, "number"},
        {{"keyword", "reserved", "preprocessor", "directive", "command", "instruction"}, "keyword"},
        {{"operator"}, "operator"},
        {{"attribute"}, "attribute"},
        {{"tag", "element", "entity"}, "tag"},
        {{"function", "target"}, "function"},
        {{"class", "type"}, "type"},
        {{"variable", "parameter", "property", "key"}, "property"},
        {{"error"}, "variable.builtin"},
        {{"emphasis", "strong", "link", "url"}, "function"},
    };
    return rules;
}

// QScintilla lexers each have their own style numbers but describe every
// style in English ("Double-quoted string", "Comment line"...). We infer the
// theme category to use from that, which avoids a mapping table per lexer.
QString categoryForLexerStyle(const QString &description)
{
    const QString text = description.toLower();
    const auto mentionsAny = [&text](const CategoryRule &rule) {
        return std::ranges::any_of(rule.words,
                                   [&text](const char *word) { return text.contains(QLatin1String(word)); });
    };
    const auto rule = std::ranges::find_if(categoryRules(), mentionsAny);
    if (rule == categoryRules().end())
        return {};
    return QString::fromLatin1(rule->category);
}

// The editor font in its three variants a syntax style can ask for.
struct StyleFonts {
    QFont regular;
    QFont italic;
    QFont bold;

    const QFont &forStyle(const TextStyle &style) const
    {
        if (style.bold)
            return bold;
        if (style.italic)
            return italic;
        return regular;
    }
};

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

    StyleFonts fonts{font, font, font};
    fonts.italic.setItalic(true);
    fonts.bold.setWeight(QFont::Weight(ScintillaStyles::styleWeight(theme.font, true)));

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
        lexer_->setFont(fonts.forStyle(textStyle), style);
    }

    // QsciScintilla only rereads the lexer's styles on setLexer().
    editor_->setLexer(lexer_.get());
}
