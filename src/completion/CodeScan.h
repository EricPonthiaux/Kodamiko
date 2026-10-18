#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Reading C-like code (C, C++, Java, JavaScript, TypeScript) without a
// parser: enough to find the class around the cursor, the function after
// a comment or the declarations of a class body. Positions are bytes.
namespace CodeScan {

bool isIdentifierByte(char c);

// `text` with the inside of comments, strings and character literals
// replaced by spaces (line breaks kept): brackets and semicolons left are
// those of the code. Same size as `text`.
QByteArray blank(const QByteArray &text);

// In blanked text: the bracket closing the one opened at `open` ("(",
// "[", "{" or "<"), or -1.
int closerOf(const QByteArray &blanked, int open);
// The "{" of the innermost block still open at `position`, or -1.
int enclosingBrace(const QByteArray &blanked, int position);
// The "(" still open at `position` within the same statement, or -1.
int enclosingParenthesis(const QByteArray &blanked, int position);
// Start of the declaration or statement that the block opened at `brace`
// belongs to: after the previous ";", "{" or "}" at the same depth.
int headerStart(const QByteArray &blanked, int brace);

// Whether a statement may start at `at`: only blanks before it on the
// line, or after a "{", "}", ";" (and ":" of "public:" when `labels`).
bool startsStatement(const QByteArray &text, int at, bool labels = false);

// Whether `position` is in the module name of a JavaScript or TypeScript
// import: "from './|", "import '|", "require('|", "import('|".
bool inModulePath(const QByteArray &text, int position);

// Splits at the commas outside brackets: "int a, std::map<K, V> b".
QStringList splitTopLevel(const QString &text, QChar separator = QLatin1Char(','));

struct Parameter {
    QString type;         // "const QString &", empty in JavaScript
    QString name;         // empty when the declaration has none
    QString defaultValue; // after "="
    QString text;         // as written, the default value left out
};
// "const QString &name = {}" → type "const QString &", name "name".
Parameter parseParameter(const QString &text, bool typed = true);

// A function declaration or definition header: "virtual int Foo::size(int
// a) const override".
struct Function {
    QString returnType; // specifiers ("virtual", "static", "public"...) left out; empty for constructors
    QString scope;      // "Foo" of "Foo::size"
    QString name;
    QString parameters; // between the parentheses, as written
    QString trailing;   // after them: "const override", "throws IOException"
    QStringList specifiers; // "virtual", "static", "public", "@Override"...
    QString templateParameters; // "typename T, int N" of "template <typename T, int N>"
    bool pure = false;  // "= 0", or an abstract or interface method
    bool deleted = false; // "= default" or "= delete"

    QList<Parameter> parameterList(bool typed = true) const;
    // The qualifiers to repeat in an override or a definition ("const",
    // "noexcept"), without "override", "final" or "= 0".
    QString qualifiers() const;
    bool returnsValue() const;
};
// An empty name when `header` is no function ("if (x)", "int x = f(y)").
Function parseFunction(const QString &header);

// What follows `position`: the header of the next declaration, up to its
// "{" or ";" (comments skipped), "" when none starts there.
QString nextDeclaration(const QByteArray &text, int position);

// Declarations of a block between `open` (its "{") and its "}", or of
// the whole text when `open` is -1: without access labels ("public:")
// nor bodies, in blanked text.
struct Declaration {
    QString text;
    bool hasBody = false;
    int start = 0;
    int body = -1;  // its "{"
    QString access; // the label it is under: "public", "signals"...
};
QList<Declaration> declarationsIn(const QByteArray &blanked, int open);

// The class, struct, interface or enum whose body is opened at `brace`:
// its name and its base classes ("public QWidget", "extends Foo
// implements Bar" give "QWidget", "Foo", "Bar"). An empty name when the
// block is no class.
struct ClassHeader {
    QString keyword; // "class", "struct", "interface"...
    QString name;
    QStringList bases;
};
ClassHeader classAt(const QByteArray &blanked, int brace);

// The body ("{" position) of the class `name` declared in `text`, -1 if
// none.
int classBody(const QByteArray &blanked, const QString &name);

// The blanks starting the line of `at`.
QString lineIndent(const QByteArray &text, int at);
// One indentation step of the file: a tab if its lines start with tabs,
// else the smallest indentation in spaces (4 when nothing tells).
QString indentUnit(const QByteArray &text);
// `body` for its place: each line after the first at `indent`, each tab
// one step of the file.
QString indented(const QString &body, const QString &indent, const QString &unit);

} // namespace CodeScan
