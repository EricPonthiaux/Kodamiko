#include "editor/EditorWidget.h"
#include "outline/OutlineProviders.h"
#include "outline/OutlineView.h"
#include "syntax/TreeSitterGrammars.h"
#include "ui/Theme.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {

// Outline in readable form: "Foo{bar,baz}"; a symbol's kind via kindOf().
QString describe(const std::vector<OutlineSymbol> &symbols)
{
    QStringList parts;
    for (const OutlineSymbol &symbol : symbols) {
        QString part = symbol.name;
        if (!symbol.children.empty())
            part += QLatin1Char('{') + describe(symbol.children) + QLatin1Char('}');
        parts << part;
    }
    return parts.join(QLatin1Char(','));
}

std::vector<OutlineSymbol> outline(const QString &languageId, const char *code)
{
    auto provider = OutlineProviders::create(languageId);
    Q_ASSERT(provider);
    return provider->symbols(QByteArray(code));
}

const OutlineSymbol *find(const std::vector<OutlineSymbol> &symbols, const QString &name)
{
    for (const OutlineSymbol &symbol : symbols) {
        if (symbol.name == name)
            return &symbol;
        if (const OutlineSymbol *found = find(symbol.children, name))
            return found;
    }
    return nullptr;
}

} // namespace

class TestOutline : public QObject {
    Q_OBJECT

private slots:

    void everyOutlineQueryCompiles_data()
    {
        QTest::addColumn<QString>("language");
        for (const char *language : {"c", "cpp", "java", "javascript", "typescript", "tsx"})
            QTest::newRow(language) << language;
    }

    void everyOutlineQueryCompiles()
    {
        QFETCH(QString, language);
        const TreeSitterGrammar *grammar = TreeSitterGrammars::forLanguage(language);
        QVERIFY(grammar != nullptr);
        QVERIFY(grammar->outline != nullptr);
    }

    void noProviderForPlainText()
    {
        QVERIFY(OutlineProviders::create(QStringLiteral("text")) == nullptr);
        QVERIFY(OutlineProviders::create(QStringLiteral("diff")) == nullptr);
    }

    void c()
    {
        const auto symbols = outline("c", "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
                                          "struct point { int x; int *y; };\n"
                                          "int area(int w);\n"
                                          "static char *name(void) { int local(int); return 0; }\n"
                                          "int area(int w) { return w; }\n");
        QCOMPARE(describe(symbols), QStringLiteral("MAX,point{x,y},area,name,area"));
        QCOMPARE(find(symbols, "point")->kind, SymbolKind::Struct);
        QCOMPARE(find(symbols, "x")->kind, SymbolKind::Field);
        QCOMPARE(find(symbols, "MAX")->kind, SymbolKind::Macro);
    }

    void cppNesting()
    {
        const char *code = "namespace app {\n"
                           "class Foo : public Base {\n"
                           "public:\n"
                           "    Foo();\n"
                           "    int size() const { return n_; }\n"
                           "    void run();\n"
                           "private:\n"
                           "    int n_ = 0;\n"
                           "};\n"
                           "void Foo::run() { QFile file(path); }\n"
                           "namespace {\n"
                           "int helper() { return 1; }\n"
                           "}\n"
                           "using Id = int;\n"
                           "}\n";
        const auto symbols = outline("cpp", code);
        QCOMPARE(describe(symbols), QStringLiteral("app{Foo{Foo,size,run,n_},Foo::run,(anonymous){helper},Id}"));
        QCOMPARE(find(symbols, "size")->kind, SymbolKind::Method);
        QCOMPARE(find(symbols, "Foo::run")->kind, SymbolKind::Method);
        QCOMPARE(find(symbols, "helper")->kind, SymbolKind::Function);
        QCOMPARE(find(symbols, "Id")->kind, SymbolKind::Type);

        const OutlineSymbol *foo = find(symbols, "Foo");
        QCOMPARE(foo->startLine, 1);
        QCOMPARE(foo->endLine, 8);
        QCOMPARE(foo->position, QByteArray(code).indexOf("Foo"));
    }

    void cppQtClass()
    {
        const auto symbols = outline("cpp", "class View : public QWidget {\n"
                                            "    Q_OBJECT\n"
                                            "\n"
                                            "public:\n"
                                            "    explicit View(QWidget *parent = nullptr);\n"
                                            "signals:\n"
                                            "    void changed();\n"
                                            "private slots:\n"
                                            "    void update();\n"
                                            "};\n");
        QCOMPARE(describe(symbols), QStringLiteral("View{View,changed,update}"));
    }

    void java()
    {
        const auto symbols = outline("java", "public class App {\n"
                                             "    private int count, total;\n"
                                             "    public App() {}\n"
                                             "    void run() {}\n"
                                             "    interface Listener { void changed(); }\n"
                                             "    enum Mode { A, B }\n"
                                             "}\n");
        QCOMPARE(describe(symbols), QStringLiteral("App{count,total,App,run,Listener{changed},Mode}"));
        QCOMPARE(find(symbols, "Listener")->kind, SymbolKind::Interface);
    }

    void javaScript()
    {
        const auto symbols = outline("javascript", "class View {\n"
                                                   "  count = 0;\n"
                                                   "  render() {}\n"
                                                   "}\n"
                                                   "function main() { const inner = () => 1; }\n"
                                                   "export const greet = (name) => name;\n"
                                                   "const config = { a: 1 };\n");
        QCOMPARE(describe(symbols), QStringLiteral("View{count,render},main{inner},greet"));
    }

    void typeScript()
    {
        const auto symbols =
            outline("typescript", "namespace Shapes {\n"
                                  "  export interface Shape { area(): number; name: string; }\n"
                                  "  export type Id = string;\n"
                                  "  export enum Kind { Circle }\n"
                                  "  export abstract class Base { private id: Id = ''; abstract area(): number; }\n"
                                  "}\n"
                                  "function compute(x: number): number;\n");
        QCOMPARE(describe(symbols), QStringLiteral("Shapes{Shape{area,name},Id,Kind,Base{id,area}},compute"));
    }

    void tsx()
    {
        const auto symbols = outline("tsx", "export function App(): JSX.Element { return <div/>; }\n");
        QCOMPARE(describe(symbols), QStringLiteral("App"));
    }

    void markdown()
    {
        const char *code = "# Duet\n"
                           "Intro\n"
                           "## Building\n"
                           "```sh\n"
                           "# not a heading\n"
                           "```\n"
                           "Usage\n"
                           "-----\n"
                           "### Shortcuts ###\n"
                           "- list\n"
                           "---\n"
                           "# License\n";
        const auto symbols = outline("markdown", code);
        QCOMPARE(describe(symbols), QStringLiteral("Duet{Building,Usage{Shortcuts}},License"));
        const OutlineSymbol *compilation = find(symbols, "Building");
        QCOMPARE(compilation->startLine, 2);
        QCOMPARE(compilation->endLine, 5);
        QCOMPARE(compilation->position, QByteArray(code).indexOf("Building"));
        QCOMPARE(find(symbols, "Usage")->endLine, 10);
    }

    void cmake()
    {
        const auto symbols = outline("cmake", "project(Demo)\n"
                                              "function(add_demo name)\n"
                                              "    add_executable(${name} main.cpp)\n"
                                              "endfunction()\n"
                                              "add_library(core STATIC a.cpp)\n"
                                              "qt_add_executable(app WIN32 main.cpp)\n");
        QCOMPARE(describe(symbols), QStringLiteral("add_demo{${name}},core,app"));
        QCOMPARE(find(symbols, "add_demo")->endLine, 3);
        QCOMPARE(find(symbols, "core")->kind, SymbolKind::Target);
    }

    void viewFollowsEditor()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("main.cpp"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int first() { return 1; }\n\nint second() {\n  return 2;\n}\n");
        file.close();

        EditorWidget editor(Theme::dark());
        QString error;
        QVERIFY(editor.load(path, &error));
        editor.show(); // QScintilla reports the cursor move when shown

        OutlineView view;
        view.show();
        view.setEditor(&editor);
        QTreeWidget *tree = view.tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("second"));

        // The cursor in the body of second() selects second.
        editor.setCursorPosition(3, 2);
        QTRY_COMPARE(tree->currentItem(), tree->topLevelItem(1));

        // A click on first places the cursor on its name.
        emit tree->itemClicked(tree->topLevelItem(0), 0);
        QCOMPARE(int(editor.SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS)), 4);

        // An edit is taken into account after the delay.
        editor.append(QStringLiteral("int third() { return 3; }\n"));
        QTRY_COMPARE(tree->topLevelItemCount(), 3);

        view.setEditor(nullptr);
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void viewScrollsLongNamesHorizontally()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("main.cpp"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int " + QByteArray(120, 'n') + "() { return 1; }\nint short_() { return 2; }\n");
        file.close();

        EditorWidget editor(Theme::dark());
        QString error;
        QVERIFY(editor.load(path, &error));

        OutlineView view;
        view.resize(200, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setEditor(&editor);
        const QTreeWidget *tree = view.tree();
        QCOMPARE(tree->topLevelItemCount(), 2);

        // The long name is not truncated: the column overflows, the scrollbar appears.
        QTRY_VERIFY(tree->header()->sectionSize(0) > tree->viewport()->width());
        QTRY_VERIFY(tree->horizontalScrollBar()->maximum() > 0);

        // Filtered out, it no longer counts: the column goes back to the view's width.
        view.findChild<QLineEdit *>()->setText(QStringLiteral("short"));
        QTRY_COMPARE(tree->header()->sectionSize(0), tree->viewport()->width());
        QTRY_COMPARE(tree->horizontalScrollBar()->maximum(), 0);
    }
};

QTEST_MAIN(TestOutline)
#include "tst_outline.moc"
