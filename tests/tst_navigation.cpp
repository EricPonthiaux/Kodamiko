// SPDX-License-Identifier: GPL-3.0-only

#include "git/GitProcess.h"
#include "navigation/Definitions.h"
#include "navigation/GoToPopup.h"
#include "navigation/HierarchyView.h"
#include "navigation/NavigationHistory.h"
#include "navigation/RecentFiles.h"
#include "navigation/SymbolIndex.h"
#include "navigation/UsageFinder.h"
#include "navigation/UsagesView.h"
#include "outline/OutlineProviders.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

#include <algorithm>

namespace {

constexpr const char *header = R"(
namespace app {
class Widget {
public:
    Widget();
    void load();
    int size_;
};
} // namespace app
)";

constexpr const char *source = R"(
namespace app {
Widget::Widget() {}
void Widget::load() { size_ = 0; }
} // namespace app

struct Other {
    void load() {}
};
)";

QList<SymbolLocation> symbolsOf(const QString &path, const char *code)
{
    auto provider = OutlineProviders::create(QStringLiteral("cpp"));
    Q_ASSERT(provider);
    return SymbolIndex::flatten(provider->symbols(QByteArray(code)), path, QStringLiteral("cpp"));
}

QList<SymbolLocation> project()
{
    return symbolsOf(QStringLiteral("/p/Widget.h"), header) + symbolsOf(QStringLiteral("/p/Widget.cpp"), source);
}

QList<SymbolLocation> named(const QList<SymbolLocation> &symbols, const QString &name)
{
    QList<SymbolLocation> result;
    for (const SymbolLocation &symbol : symbols) {
        if (symbol.name == name)
            result.append(symbol);
    }
    return result;
}

const SymbolLocation &find(const QList<SymbolLocation> &symbols,
                           const QString &name,
                           const QString &file,
                           bool declarationOnly)
{
    for (const SymbolLocation &symbol : symbols) {
        if (symbol.name == name && symbol.filePath.endsWith(file) && symbol.declarationOnly == declarationOnly)
            return symbol;
    }
    qFatal("symbol not found");
}

Definitions::Request request(const QString &word, const QString &file, int position = -1, int line = 0)
{
    Definitions::Request request;
    request.word = word;
    request.filePath = file;
    request.languageId = QStringLiteral("cpp");
    request.position = position;
    request.line = line;
    return request;
}

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class TestNavigation : public QObject {
    Q_OBJECT

private slots:

    void flattenGivesSimpleNamesAndContainers()
    {
        const QList<SymbolLocation> symbols = symbolsOf(QStringLiteral("a.cpp"), source);
        const SymbolLocation load = find(symbols, QStringLiteral("load"), QStringLiteral("a.cpp"), false);
        QCOMPARE(load.container, QStringLiteral("Widget"));
        QCOMPARE(load.kind, SymbolKind::Method);
        // The cursor will go to "load", not to "Widget::".
        QCOMPARE(QByteArray(source).mid(load.position, 4), QByteArray("load"));

        const QList<SymbolLocation> declared = symbolsOf(QStringLiteral("a.h"), header);
        QCOMPARE(find(declared, QStringLiteral("load"), QStringLiteral("a.h"), true).container,
                 QStringLiteral("Widget"));
        QCOMPARE(find(declared, QStringLiteral("Widget"), QStringLiteral("a.h"), false).container,
                 QStringLiteral("app"));
    }

    void classWinsOverConstructors()
    {
        const auto all = project();
        const auto result = Definitions::resolve(request("Widget", "/p/main.cpp"), named(all, "Widget"), {});
        QVERIFY(!result.ambiguous);
        QCOMPARE(result.targets.first().kind, SymbolKind::Class);
    }

    void definitionTogglesWithDeclaration()
    {
        const auto all = project();
        const SymbolLocation declaration = find(all, "load", "Widget.h", true);
        auto result =
            Definitions::resolve(request("load", declaration.filePath, declaration.position), named(all, "load"), {});
        QCOMPARE(result.targets.size(), 1);
        QCOMPARE(result.targets.first().filePath, QStringLiteral("/p/Widget.cpp"));
        QVERIFY(!result.targets.first().declarationOnly);

        const SymbolLocation &definition = result.targets.first();
        result =
            Definitions::resolve(request("load", definition.filePath, definition.position), named(all, "load"), {});
        QCOMPARE(result.targets.size(), 1);
        QCOMPARE(result.targets.first().filePath, QStringLiteral("/p/Widget.h"));
    }

    void sameNameInTwoClassesIsAmbiguous()
    {
        const auto all = project();
        const auto result = Definitions::resolve(request("load", "/p/main.cpp"), named(all, "load"), {});
        QVERIFY(result.ambiguous);
        QCOMPARE(result.targets.size(), 3); // two definitions, then the declaration
    }

    void qualifierAndEnclosingClassDisambiguate()
    {
        const auto all = project();
        Definitions::Request qualified = request("load", "/p/main.cpp");
        qualified.qualifier = QStringLiteral("Other");
        auto result = Definitions::resolve(qualified, named(all, "load"), {});
        QVERIFY(!result.ambiguous);
        QCOMPARE(result.targets.first().container, QStringLiteral("Other"));

        // Call to load() from Widget::Widget (line 2 of source).
        const QList<SymbolLocation> fileSymbols = symbolsOf(QStringLiteral("/p/Widget.cpp"), source);
        result = Definitions::resolve(request("load", "/p/Widget.cpp", -1, 2), named(all, "load"), fileSymbols);
        QVERIFY(!result.ambiguous);
        QCOMPARE(result.targets.first().container, QStringLiteral("Widget"));
    }

    void indexSkipsIgnoredFiles()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/Widget.h"), header);
        write(dir.filePath("build/generated.h"), "class Generated {};\n");
        write(dir.filePath(".gitignore"), "build/\n");
        Git::run(dir.path(), {"init", "-q"});

        SymbolIndex index;
        QSignalSpy finished(&index, &SymbolIndex::indexingFinished);
        index.setFolder(dir.path());
        QVERIFY(finished.wait());
        QCOMPARE(index.lookup("Widget").size(), 2); // the class and its constructor
        QVERIFY(index.lookup("Generated").isEmpty());

        // An added file is seen on the next reread.
        write(dir.filePath("src/Extra.cpp"), "void extra() {}\n");
        index.refresh();
        QVERIFY(finished.wait());
        QCOMPARE(index.lookup("extra").size(), 1);
    }

    void goToSplitsLineNumbers()
    {
        QCOMPARE(GoToPopup::splitLine("Main.cpp:42"), (std::pair<QString, int>{"Main.cpp", 41}));
        QCOMPARE(GoToPopup::splitLine("Main.cpp:42:7"), (std::pair<QString, int>{"Main.cpp", 41}));
        QCOMPARE(GoToPopup::splitLine("Main.cpp"), (std::pair<QString, int>{"Main.cpp", -1}));
    }

    void goToRanksFileNamesFirst()
    {
        // The name counts more than the folders.
        QVERIFY(GoToPopup::fileScore("src/editor/EditorTabs.cpp", "edtabs") >
                GoToPopup::fileScore("src/editor/TextFile.cpp", "edtabs"));
        QVERIFY(GoToPopup::fileScore("src/a/Widget.h", "widget") >
                GoToPopup::fileScore("src/widgets/Other.h", "widget"));
        QVERIFY(GoToPopup::fileScore("src/widgets/Other.h", "widget") >= 0); // through the path
        QVERIFY(GoToPopup::fileScore("src/app/main.cpp", "app/main") >= 0);
        QCOMPARE(GoToPopup::fileScore("src/app/main.cpp", "zzz"), -1);
    }

    void recentFilesKeepTheOrder()
    {
        RecentFiles recent;
        recent.touch("/p/a");
        recent.touch("/p/b");
        recent.touch("/p/a");
        QCOMPARE(recent.opened(), (QStringList{"/p/a", "/p/b"}));
        recent.markEditedByClaude({"/p/dir/c", "/p/b"});
        QCOMPARE(recent.editedByClaude(), (QStringList{"/p/b", "/p/dir/c"}));
        recent.rename("/p/dir", "/p/folder");
        QVERIFY(recent.isEditedByClaude("/p/folder/c"));
        recent.remove("/p/b");
        QCOMPARE(recent.opened(), (QStringList{"/p/a"}));
        QCOMPARE(recent.editedByClaude(), (QStringList{"/p/folder/c"}));
        for (int i = 0; i < 60; ++i)
            recent.touch(QStringLiteral("/p/%1").arg(i));
        QCOMPARE(recent.opened().size(), RecentFiles::maxSize);
    }

    void goToPopupFindsFilesSymbolsAndRecentFiles()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/Widget.h"), header);
        write(dir.filePath("src/Widget.cpp"), source);
        write(dir.filePath("docs/notes.md"), "# Notes\n");

        SymbolIndex index;
        QSignalSpy indexed(&index, &SymbolIndex::indexingFinished);
        index.setFolder(dir.path());
        QVERIFY(indexed.wait());
        RecentFiles recent;
        recent.touch(dir.filePath("docs/notes.md"));
        recent.touch(dir.filePath("src/Widget.h"));
        recent.markEditedByClaude({dir.filePath("src/Widget.cpp")});

        QWidget window;
        window.resize(1000, 800);
        window.show(); // the popup is a panel inside it
        GoToPopup popup(&index, &recent, &window);
        popup.setFolder(dir.path());
        QSignalSpy fileChosen(&popup, &GoToPopup::fileChosen);
        QSignalSpy symbolChosen(&popup, &GoToPopup::symbolChosen);

        popup.popup(GoToPopup::Tab::Files);
        QTRY_VERIFY(popup.isReady());
        popup.searchField()->setText("wcpp:3");
        QCOMPARE(popup.list()->topLevelItem(0)->text(0), QString("Widget.cpp:3"));
        QCOMPARE(popup.list()->topLevelItem(0)->text(1), QString("src"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QTRY_COMPARE(fileChosen.size(), 1);
        QCOMPARE(fileChosen.first().at(0).toString(), dir.filePath("src/Widget.cpp"));
        QCOMPARE(fileChosen.first().at(1).toInt(), 2);

        popup.popup(GoToPopup::Tab::Symbols);
        popup.searchField()->setText("Widget::load");
        QVERIFY(popup.list()->topLevelItem(0)->text(0) == "Widget::load");
        QVERIFY(popup.list()->topLevelItem(0)->text(1).startsWith("method"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QTRY_COMPARE(symbolChosen.size(), 1);
        const auto symbol = symbolChosen.first().at(0).value<SymbolLocation>();
        QCOMPARE(symbol.filePath, dir.filePath("src/Widget.cpp"));
        QVERIFY(!symbol.declarationOnly);

        popup.popup(GoToPopup::Tab::Symbols);
        QTest::keyClick(popup.searchField(), Qt::Key_Tab); // to Recent Files
        QCOMPARE(popup.tab(), GoToPopup::Tab::Recent);
        QTest::keyClick(popup.searchField(), Qt::Key_Backtab);
        QCOMPARE(popup.tab(), GoToPopup::Tab::Symbols);

        popup.popup(GoToPopup::Tab::Recent);
        QCOMPARE(popup.list()->topLevelItemCount(), 2);
        // The previous file is selected, as with Ctrl+Tab.
        QCOMPARE(popup.list()->currentItem()->text(0), QString("notes.md"));
        QTest::keyClick(popup.searchField(), Qt::Key_E, Qt::ControlModifier);
        QVERIFY(popup.editedByClaude()->isChecked());
        QCOMPARE(popup.list()->topLevelItemCount(), 1);
        QCOMPARE(popup.list()->topLevelItem(0)->text(0), QString("Widget.cpp"));
    }

    void historyGoesBackAndForward()
    {
        NavigationHistory history;
        const NavigationHistory::Location a{"a", 1};
        const NavigationHistory::Location b{"b", 2};
        const NavigationHistory::Location c{"c", 3};
        history.recordJump(a); // a → b
        history.recordJump(b); // b → c
        QCOMPARE(history.back(c), b);
        QCOMPARE(history.back(b), a);
        QVERIFY(!history.back(a));
        QCOMPARE(history.forward(a), b);
        history.recordJump(b); // new jump: no more going forward
        QVERIFY(!history.canGoForward());
    }

    void classifiesTheServersLocationsByKind()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath("a.cpp");
        write(path, "int total(int n) { return n; }\nint x = total(1);\nint y = x;\n");
        using LspProtocol::Location;
        const QList<Location> locations{{path, {{1, 8}, {1, 13}}}, {path, {{0, 4}, {0, 9}}}};

        const QList<Usage> usages = UsageFinder::classify(locations, {});

        QCOMPARE(usages.size(), 2);
        QCOMPARE(usages[0], (Usage{path, 0, 4, 5, "int total(int n) { return n; }", UsageKind::Declaration}));
        QCOMPARE(usages[1].kind, UsageKind::Call);
        QCOMPARE(usages[1].lineText, QString("int x = total(1);"));
    }

    void findsAWordInTheUnsavedTextsWithoutAServer()
    {
        const QString path = "/p/a.py";

        const QList<Usage> usages =
            UsageFinder::findWord("total", nullptr, {{path, "total = 1  # the total\nprint(total)\n"}});

        QCOMPARE(usages.size(), 3);
        QCOMPARE(usages[0].kind, UsageKind::Declaration);
        QCOMPARE(usages[1].kind, UsageKind::Textual);
        QCOMPARE(usages[2].line, 1);
        QCOMPARE(usages[2].column, 6);
    }

    void usagesPaneFiltersAndGoesFromUsageToUsage()
    {
        UsagesView view;
        const UsageResults results{{"total", "/p/a.cpp", {0, 4}},
                                   {{"/p/a.cpp", 0, 4, 5, "int total;", UsageKind::Declaration},
                                    {"/p/a.cpp", 3, 2, 5, "total = 2;", UsageKind::Write},
                                    {"/p/b.cpp", 1, 0, 5, "total", UsageKind::Read}},
                                   true};
        QList<int> lines;
        connect(&view, &UsagesView::usageActivated, this,
                [&lines](const QString &, int line, int) { lines.append(line); });

        view.showResults(results);
        QCOMPARE(view.tree()->topLevelItemCount(), 2);
        view.filterAction(UsageKind::Write)->setChecked(false);
        view.goToNext();
        view.goToNext();
        view.goToNext(); // back to the first

        QCOMPARE(lines, (QList<int>{0, 1, 0}));
        QCOMPARE(view.tree()->topLevelItem(0)->childCount(), 1); // the write is left out
    }

    void parsesHierarchyItemsAndCalls()
    {
        using namespace LspProtocol;
        const QJsonObject item{{"name", "total"},
                               {"detail", "int total(int)"},
                               {"kind", 12},
                               {"uri", uriOf("/p/a.cpp")},
                               {"range", toJson(Range{{2, 0}, {4, 1}})},
                               {"selectionRange", toJson(Range{{2, 4}, {2, 9}})}};
        const QJsonObject outside{{"name", "x"}, {"uri", "https://example.com/x"}};

        const QList<HierarchyItem> items = parseHierarchyItems(QJsonArray{item, outside});
        const QList<HierarchyItem> callers =
            parseHierarchyCalls(QJsonArray{QJsonObject{{"from", item}, {"fromRanges", QJsonArray{}}}}, "from");

        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].name, QString("total"));
        QCOMPARE(items[0].filePath, QString("/p/a.cpp"));
        QCOMPARE(items[0].selectionRange.start, (Position{2, 4}));
        QCOMPARE(items[0].raw, item);
        QCOMPARE(callers.size(), 1);
    }

    void hierarchyPaneUnfoldsOneLevelAtATime()
    {
        using LspProtocol::HierarchyItem;
        const auto element = [](const QString &name, int line) {
            return HierarchyItem{name, {}, "/p/a.cpp", {{line, 4}, {line, 9}}, QJsonObject{{"name", name}}};
        };
        QList<std::pair<Hierarchy::Direction, QString>> asked;
        HierarchyView view;
        view.showHierarchy(
            {"Calls of “total”",
             "Callers",
             "Callees",
             {element("total", 1)},
             [&](Hierarchy::Direction direction, const HierarchyItem &parent, const Hierarchy::ChildrenReply &reply) {
                 asked.append({direction, parent.name});
                 reply(parent.name == "total" ? QList<HierarchyItem>{element("main", 7)} : QList<HierarchyItem>{});
             }});
        QList<int> opened;
        connect(&view, &HierarchyView::elementActivated, this,
                [&opened](const QString &, int line, int) { opened.append(line); });

        const QTreeWidgetItem *root = view.tree()->topLevelItem(0);
        QCOMPARE(root->childCount(), 1); // asked when it was unfolded
        QTreeWidgetItem *caller = root->child(0);
        QCOMPARE(caller->text(0), QString("main  a.cpp:8"));
        caller->setExpanded(true);
        caller->setExpanded(false);
        caller->setExpanded(true); // not asked again
        emit view.tree()->itemClicked(caller, 0);
        view.setDirection(Hierarchy::Direction::Down);

        QCOMPARE(asked, (QList<std::pair<Hierarchy::Direction, QString>>{{Hierarchy::Direction::Up, "total"},
                                                                         {Hierarchy::Direction::Up, "main"},
                                                                         {Hierarchy::Direction::Down, "total"}}));
        QCOMPARE(opened, QList<int>{7});
    }

    void hierarchyPaneForgetsItsHierarchyWhenCleared()
    {
        HierarchyView view;
        view.showHierarchy({"Calls of “total”", "Callers", "Callees", {LspProtocol::HierarchyItem{"total"}}, {}});

        view.clear();

        QCOMPARE(view.tree()->topLevelItemCount(), 0);
    }

    void usagesPaneForgetsItsSearchesInAnotherProject()
    {
        UsagesView view;
        view.setProjectFolder("/p");
        view.showResults({{"total", "/p/a.cpp", {0, 4}}, {}, true});

        view.setProjectFolder("/q");

        QVERIFY(view.history().isEmpty());
        const QList<QLabel *> labels = view.findChildren<QLabel *>();
        QVERIFY(std::ranges::none_of(labels, [](const QLabel *label) { return label->text().contains("total"); }));
    }

    void usagesPaneKeepsTheEarlierSearchesAndRunsOneAgain()
    {
        UsagesView view;
        QList<QString> rerun;
        connect(&view, &UsagesView::rerunRequested, this,
                [&rerun](const UsageSearch &search) { rerun.append(search.name); });

        view.showResults({{"total", "/p/a.cpp", {0, 4}}, {}, true});
        view.showResults({{"count", "/p/a.cpp", {1, 4}}, {}, false});
        view.findChildren<QToolButton *>().first()->click(); // Rerun

        QCOMPARE(view.history().size(), 2);
        QCOMPARE(view.history().first().search.name, QString("count"));
        QCOMPARE(rerun, QList<QString>{"count"});
    }
};

QTEST_MAIN(TestNavigation)
#include "tst_navigation.moc"
