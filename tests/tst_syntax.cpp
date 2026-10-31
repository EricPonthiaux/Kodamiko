// SPDX-License-Identifier: GPL-3.0-only

#include "scintilla/ScintillaView.h"
#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "syntax/TreeSitterGrammars.h"
#include "ui/MaterialPalette.h"
#include "ui/MaterialThemes.h"
#include "ui/Theme.h"

#include <QtTest>

#include <SciLexer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>

class TestSyntax : public QObject {
    Q_OBJECT

private:
    // Expected Scintilla style for a theme category.
    static int styleFor(const Theme &theme, const char *capture)
    {
        return ScintillaStyles::firstSyntaxStyle + theme.syntaxStyleIndex(QString::fromLatin1(capture));
    }

    // Style set on the first occurrence of `word` in `editor`.
    static int styleOfWord(ScintillaView &editor, const char *word)
    {
        const int position = editor.text().toUtf8().indexOf(word);
        Q_ASSERT(position >= 0);
        return editor.send(SCI_GETSTYLEAT, position);
    }

    // WCAG 2 contrast ratio, from 1:1 to 21:1.
    static qreal contrastRatio(const QColor &first, const QColor &second)
    {
        const auto luminance = [](const QColor &color) {
            const auto channel = [](qreal value) {
                return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
        };
        const qreal firstLuminance = luminance(first);
        const qreal secondLuminance = luminance(second);
        return (std::max(firstLuminance, secondLuminance) + 0.05) / (std::min(firstLuminance, secondLuminance) + 0.05);
    }

    // Loads `code` into an editor highlighted for `languageId`, with the whole text styled.
    static std::unique_ptr<ISyntaxHighlighter> highlight(ScintillaView &editor,
                                                         const QString &languageId,
                                                         const char *code,
                                                         const Theme &theme)
    {
        editor.setText(QString::fromUtf8(code));
        auto highlighter = HighlighterFactory::create(languageId);
        highlighter->attach(&editor);
        highlighter->applyTheme(theme);
        editor.send(SCI_COLOURISE, 0L, -1L);
        return highlighter;
    }

private slots:

    void themesAreListedByName()
    {
        QStringList names;
        for (const Theme &theme : Theme::all())
            names.append(theme.name);
        // Kodamiko's own themes precede the Material ones.
        QCOMPARE(names.size(), 19);
        QCOMPARE(names.first(), QString("kodamiko"));
        QVERIFY(std::ranges::all_of(names.mid(1), [](const QString &name) { return name.startsWith("kodamiko-"); }));
        const Theme violet = Theme::named("kodamiko-violet");
        QCOMPARE(violet.name, QString("kodamiko-violet"));
        QVERIFY(violet.accent.isValid() && violet.window.isValid() && violet.findHighlight.isValid());
        QVERIFY(violet.syntaxStyleIndex(u"function.method") >= 0);
        const Theme pearl = Theme::named("kodamiko-pearl");
        QCOMPARE(pearl.name, QString("kodamiko-pearl"));
        QVERIFY(!pearl.isDark);
        QVERIFY(pearl.background.lightnessF() > pearl.foreground.lightnessF());
        // Every theme has the interface colors the controls' style sheet uses.
        for (const Theme &theme : Theme::all()) {
            for (const QColor &color : {theme.window, theme.button, theme.alternateBase, theme.highlight, theme.accent,
                                        theme.accentText, theme.mutedText, theme.border, theme.line, theme.hover,
                                        theme.currentLineNumber, theme.error, theme.warning})
                QVERIFY2(color.isValid(), qPrintable(theme.name));
        }
        QCOMPARE(Theme::named("unknown").name, QString("kodamiko"));
    }

    void materialThemesTakeOnlyMaterialDesignColors()
    {
        const QList<Theme> themes = MaterialThemes::all();
        QCOMPARE(themes.size(), 8);
        const auto isMaterial = [](const QColor &color) {
            return std::ranges::any_of(MaterialPalette::all,
                                       [&color](const char *swatch) { return QColor(swatch) == color; });
        };
        for (const Theme &theme : themes) {
            QVERIFY2(theme.title.startsWith("Kodamiko-"), qPrintable(theme.title));
            for (const QColor &color : {theme.background,
                                        theme.foreground,
                                        theme.caret,
                                        theme.caretLine,
                                        theme.selection,
                                        theme.marginBackground,
                                        theme.marginForeground,
                                        theme.currentLineNumber,
                                        theme.braceMatch,
                                        theme.findHighlight,
                                        theme.error,
                                        theme.warning,
                                        theme.window,
                                        theme.button,
                                        theme.alternateBase,
                                        theme.highlight,
                                        theme.accent,
                                        theme.accentText,
                                        theme.mutedText,
                                        theme.border,
                                        theme.line,
                                        theme.hover})
                QVERIFY2(isMaterial(color), qPrintable(theme.name + ": " + color.name()));
            for (const auto &[capture, style] : theme.syntaxStyles)
                QVERIFY2(isMaterial(style.foreground), qPrintable(theme.name + ": " + capture));
        }
    }

    void everyThemeReadsItsCodeAtAbout7To1AndItsCommentsAt5_5To1()
    {
        constexpr qreal codeContrast = 6.9; // 7:1 or about
        constexpr qreal commentContrast = 5.5;
        constexpr qreal accentContrast = 4.5;
        for (const Theme &theme : Theme::all()) {
            for (const auto &[capture, style] : theme.syntaxStyles) {
                const qreal minimum = capture == "comment" ? commentContrast : codeContrast;
                QVERIFY2(contrastRatio(style.foreground, theme.background) >= minimum,
                         qPrintable(theme.name + ": " + capture));
            }
            QVERIFY2(contrastRatio(theme.onAccent, theme.accent) >= accentContrast, qPrintable(theme.name));
        }
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
        QFETCH(const QString, path);
        QFETCH(const QString, language);
        QCOMPARE(LanguageRegistry::languageForFile(path), language);
    }

    // Before everyGrammarQueryCompiles: the grammars are not loaded yet.
    void grammarsLoadFromSeveralThreadsAtOnce()
    {
        const QStringList languages = {"c", "cpp", "java", "javascript", "typescript", "tsx"};
        constexpr int threadCount = 4;
        std::array<QList<const TreeSitterGrammar *>, threadCount> loaded;
        std::vector<std::thread> threads;
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([&languages, &grammars = loaded[std::size_t(i)]] {
                for (const QString &language : languages)
                    grammars.append(TreeSitterGrammars::forLanguage(language));
            });
        }
        for (std::thread &thread : threads)
            thread.join();

        QVERIFY(!loaded[0].contains(nullptr));
        for (const QList<const TreeSitterGrammar *> &grammars : loaded)
            QCOMPARE(grammars, loaded[0]); // one grammar per language, whichever thread asked
    }

    void everyGrammarQueryCompiles_data()
    {
        QTest::addColumn<QString>("language");
        for (const char *language : {"c", "cpp", "java", "javascript", "typescript", "tsx"})
            QTest::newRow(language) << language;
    }

    void everyGrammarQueryCompiles()
    {
        QFETCH(const QString, language);
        QVERIFY(TreeSitterGrammars::forLanguage(language) != nullptr);
    }

    void highlightsC()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "c", "int main(void) {\n  // note\n  return puts(\"hi\");\n}\n", theme);
        QCOMPARE(styleOfWord(editor, "int"), styleFor(theme, "type"));
        QCOMPARE(styleOfWord(editor, "main"), styleFor(theme, "function"));
        QCOMPARE(styleOfWord(editor, "// note"), styleFor(theme, "comment"));
        QCOMPARE(styleOfWord(editor, "return"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "\"hi\""), styleFor(theme, "string"));
    }

    void highlightsCppOverC()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "cpp", "namespace app { class Foo {}; }\n", theme);
        QCOMPARE(styleOfWord(editor, "namespace"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "class"), styleFor(theme, "keyword"));
    }

    void highlightsTemplateTypes()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(
            editor, "cpp", "// éléments\ntemplate <typename T>\nT sum(T a) { T total{}; return total; }\n", theme);
        const QByteArray text = editor.text().toUtf8();
        for (int i = 0; i < text.size(); ++i)
            if (text[i] == 'T')
                QCOMPARE(int(editor.send(SCI_GETSTYLEAT, i)), styleFor(theme, "type"));
    }

    void highlightsJava()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "java", "public class App { static final int MAX = 3; }\n", theme);
        QCOMPARE(styleOfWord(editor, "public"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "MAX"), styleFor(theme, "constant"));
        QCOMPARE(styleOfWord(editor, "3"), styleFor(theme, "number"));
    }

    void highlightsJavaScript()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "javascript", "const greet = (name) => console.log(`hi ${name}`);\n", theme);
        QCOMPARE(styleOfWord(editor, "const"), styleFor(theme, "keyword"));
        QCOMPARE(styleOfWord(editor, "greet"), styleFor(theme, "function"));
        QCOMPARE(styleOfWord(editor, "console"), styleFor(theme, "variable.builtin"));
    }

    void restylesAfterEdit()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "c", "int x;\n", theme);
        QCOMPARE(styleOfWord(editor, "x"), styleFor(theme, "variable"));

        editor.insertAt(QStringLiteral("/* "), 0, 0);
        editor.append(QStringLiteral("*/\n"));
        editor.send(SCI_COLOURISE, 0L, -1L);
        QCOMPARE(styleOfWord(editor, "int"), styleFor(theme, "comment"));
    }

    void fallsBackToLexerForJson()
    {
        const Theme theme = Theme::kodamiko();
        ScintillaView editor;
        const auto h = highlight(editor, "json", "{\"key\": \"value\"}\n", theme);
        QCOMPARE(editor.send(SCI_GETLEXER), SCLEX_JSON);
    }

    void foldsBlocksCommentsAndConditionals()
    {
        ScintillaView editor;
        const auto h = highlight(editor, "cpp",
                                 "int f(int x) {\n"    // 0
                                 "    if (x) {\n"      // 1
                                 "        return 1;\n" // 2
                                 "    } else {\n"      // 3
                                 "        return 2;\n" // 4
                                 "    }\n"             // 5
                                 "}\n"                 // 6
                                 "/* a\n"              // 7
                                 "   b */\n"           // 8
                                 "#if A\n"             // 9
                                 "int y;\n"            // 10
                                 "#endif\n"            // 11
                                 "int z(int a,\n"      // 12: two lines, nothing to hide
                                 "      int b);\n",
                                 Theme::kodamiko());
        const auto depth = [&editor](int line) {
            return static_cast<int>((editor.send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE);
        };
        const auto isHeader = [&editor](int line) {
            return (editor.send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG) != 0;
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
        editor.send(SCI_COLOURISE, 0L, -1L);
        QTRY_VERIFY(!isHeader(3)); // once typing pauses
        QCOMPARE(depth(4), 1);
        QVERIFY(isHeader(6)); // the comment, one line up
    }
};

QTEST_MAIN(TestSyntax)
#include "tst_syntax.moc"
