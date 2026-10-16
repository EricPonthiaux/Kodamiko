#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "syntax/Theme.h"
#include "syntax/TreeSitterGrammars.h"

#include <Qsci/qsciscintilla.h>

#include <QtTest>

class TestSyntax : public QObject {
    Q_OBJECT

private:
    // Expected Scintilla style for a theme category.
    static int styleFor(const Theme &theme, const char *capture)
    {
        return ScintillaStyles::firstSyntaxStyle + theme.syntaxStyleIndex(QString::fromLatin1(capture));
    }

    // Style set on the first occurrence of `word` in `editor`.
    static int styleOfWord(QsciScintilla &editor, const char *word)
    {
        const int position = editor.text().toUtf8().indexOf(word);
        Q_ASSERT(position >= 0);
        return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position);
    }

    // Loads `code` into an editor highlighted for `languageId`, with the whole text styled.
    static std::unique_ptr<ISyntaxHighlighter> highlight(QsciScintilla &editor, const QString &languageId,
                                                         const char *code, const Theme &theme)
    {
        editor.setUtf8(true);
        editor.setText(QString::fromUtf8(code));
        auto highlighter = HighlighterFactory::create(languageId);
        highlighter->attach(&editor);
        highlighter->applyTheme(theme);
        editor.SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0L, -1L);
        return highlighter;
    }

private slots:
    void themesAreListedByName()
    {
        QStringList names;
        for (const Theme &theme : Theme::all())
            names.append(theme.name);
        QCOMPARE(names, QStringList({"dark", "purple", "light"}));
        const Theme purple = Theme::named("purple");
        QVERIFY(purple.isDark);
        QVERIFY(purple.accent.isValid() && purple.window.isValid() && purple.findHighlight.isValid());
        QVERIFY(purple.syntaxStyleIndex(u"function.method") >= 0);
        QVERIFY(!Theme::named("light").isDark);
        // Every theme has the interface colors the controls' style sheet uses.
        for (const Theme &theme : Theme::all()) {
            for (const QColor &color : {theme.window, theme.button, theme.alternateBase, theme.highlight, theme.accent,
                                        theme.accentText, theme.mutedText, theme.border, theme.line, theme.hover,
                                        theme.gradientStart, theme.gradientEnd})
                QVERIFY2(color.isValid(), qPrintable(theme.name));
        }
        QCOMPARE(Theme::named("unknown").name, QString("dark"));
    }

    void languageForFile_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("language");
        QTest::newRow("c") << "src/main.c" << "c";
        QTest::newRow("header") << "include/foo.h" << "cpp";
        QTest::newRow("cpp") << "Foo.CPP" << "cpp";
        QTest::newRow("java") << "App.java" << "java";
        QTest::newRow("js") << "index.mjs" << "javascript";
        QTest::newRow("tsx") << "View.tsx" << "tsx";
        QTest::newRow("cmake") << "CMakeLists.txt" << "cmake";
        QTest::newRow("makefile") << "Makefile" << "makefile";
        QTest::newRow("unknown") << "notes.xyz" << "text";
    }

    void languageForFile()
    {
        QFETCH(QString, path);
        QFETCH(QString, language);
        QCOMPARE(LanguageRegistry::languageForFile(path), language);
    }

    void everyGrammarQueryCompiles_data()
    {
        QTest::addColumn<QString>("language");
        for (const char *language : {"c", "cpp", "java", "javascript", "typescript", "tsx"})
            QTest::newRow(language) << language;
    }

    void everyGrammarQueryCompiles()
    {
        QFETCH(QString, language);
        QVERIFY(TreeSitterGrammars::forLanguage(language) != nullptr);
    }

    void highlightsC()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "c", "int main(void) {\n  // note\n  return puts(\"hi\");\n}\n", theme);
        QCOMPARE(styleOfWord(editor, "int"), styleFor(theme, "type"));
        QCOMPARE(styleOfWord(editor, "main"), styleFor(theme, "function"));
        QCOMPARE(styleOfWord(editor, "// note"), styleFor(theme, "comment"));
        QCOMPARE(styleOfWord(editor, "return"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "\"hi\""), styleFor(theme, "string"));
    }

    void highlightsCppOverC()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "cpp", "namespace app { class Foo {}; }\n", theme);
        QCOMPARE(styleOfWord(editor, "namespace"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "class"), styleFor(theme, "keyword"));
    }

    void highlightsTemplateTypes()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "cpp", "// éléments\ntemplate <typename T>\nT sum(T a) { T total{}; return total; }\n", theme);
        const QByteArray text = editor.text().toUtf8();
        for (int i = 0; i < text.size(); ++i)
            if (text[i] == 'T')
                QCOMPARE(int(editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, i)), styleFor(theme, "type"));
    }

    void highlightsJava()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "java", "public class App { static final int MAX = 3; }\n", theme);
        QCOMPARE(styleOfWord(editor, "public"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "MAX"), styleFor(theme, "constant"));
        QCOMPARE(styleOfWord(editor, "3"), styleFor(theme, "number"));
    }

    void highlightsJavaScript()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "javascript", "const greet = (name) => console.log(`hi ${name}`);\n", theme);
        QCOMPARE(styleOfWord(editor, "const"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "greet"), styleFor(theme, "function"));
        QCOMPARE(styleOfWord(editor, "console"), styleFor(theme, "variable.builtin"));
    }

    void restylesAfterEdit()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "c", "int x;\n", theme);
        QCOMPARE(styleOfWord(editor, "x"), styleFor(theme, "variable"));

        editor.insertAt(QStringLiteral("/* "), 0, 0);
        editor.append(QStringLiteral("*/\n"));
        editor.SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0L, -1L);
        QCOMPARE(styleOfWord(editor, "int"), styleFor(theme, "comment"));
    }

    void fallsBackToLexerForJson()
    {
        const Theme theme = Theme::dark();
        QsciScintilla editor;
        auto h = highlight(editor, "json", "{\"key\": \"value\"}\n", theme);
        QVERIFY(editor.lexer() != nullptr);
    }

    void foldsBlocksCommentsAndConditionals()
    {
        QsciScintilla editor;
        auto h = highlight(editor, "cpp",
                           "int f(int x) {\n"   // 0
                           "    if (x) {\n"     // 1
                           "        return 1;\n" // 2
                           "    } else {\n"     // 3
                           "        return 2;\n" // 4
                           "    }\n"            // 5
                           "}\n"                // 6
                           "/* a\n"             // 7
                           "   b */\n"          // 8
                           "#if A\n"            // 9
                           "int y;\n"           // 10
                           "#endif\n"           // 11
                           "int z(int a,\n"     // 12: two lines, nothing to hide
                           "      int b);\n",
                           Theme::dark());
        const auto depth = [&editor](int line) {
            return static_cast<int>((editor.SendScintilla(QsciScintillaBase::SCI_GETFOLDLEVEL, line)
                                     & QsciScintillaBase::SC_FOLDLEVELNUMBERMASK)
                                    - QsciScintillaBase::SC_FOLDLEVELBASE);
        };
        const auto isHeader = [&editor](int line) {
            return (editor.SendScintilla(QsciScintillaBase::SCI_GETFOLDLEVEL, line)
                    & QsciScintillaBase::SC_FOLDLEVELHEADERFLAG) != 0;
        };
        const QList<int> depths = {0, 1, 2, 1, 2, 1, 0, 0, 1, 0, 1, 0, 0, 0};
        const QList<int> headers = {0, 1, 3, 7, 9};
        for (int line = 0; line < depths.size(); ++line) {
            QVERIFY2(depth(line) == depths[line], qPrintable(QString("line %1: depth %2").arg(line).arg(depth(line))));
            QVERIFY2(isHeader(line) == headers.contains(line), qPrintable(QString("line %1").arg(line)));
        }

        // The levels follow the edits.
        editor.setSelection(4, 0, 5, 0); // "return 2;": the else block has nothing left to hide
        editor.removeSelectedText();
        editor.SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0L, -1L);
        QVERIFY(!isHeader(3));
        QCOMPARE(depth(4), 1);
        QVERIFY(isHeader(6)); // the comment, one line up
    }
};

QTEST_MAIN(TestSyntax)
#include "tst_syntax.moc"
