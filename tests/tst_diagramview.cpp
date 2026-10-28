// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "navigation/CodeNavigator.h"
#include "navigation/SymbolIndex.h"
#include "viewers/DiagramCheck.h"
#include "viewers/DiagramKinds.h"
#include "viewers/DiagramView.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/Style.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/flow/Flow.h"

#include <QFile>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace diagram;

namespace {

const char *const kMermaid = R"(flowchart TB
  A(["Début"]) --> B["Étape"]
)";

const char *const kFlowboard = R"({
  "format": "flowboard",
  "version": 1,
  "rankdir": "TB",
  "shapes": [
    {"id": 1, "key": "A", "variant": "stadium", "text": "Début", "x": 0, "y": 0},
    {"id": 2, "key": "B", "variant": "square", "text": "Étape", "x": 0, "y": 120}
  ],
  "links": [
    {"id": 3, "from": 1, "to": 2}
  ]
}
)";

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    return path;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

bool loadInto(Board &board, const QString &name, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    QString error;
    std::optional<document::Loaded> result = document::load(board, name, content, &error);
    if (!result) {
        qWarning() << error;
        return false;
    }
    if (loaded)
        *loaded = std::move(*result);
    return true;
}

} // namespace

class TestDiagramView : public QObject {
    Q_OBJECT

private slots:

    // The model, its history and selection, without a window.
    void editsAndUndoesTheDocument()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"), kMermaid));
        QCOMPARE(board.shapes().size(), size_t(2));
        QCOMPARE(board.links().size(), size_t(1));
        const Shape *a = byKey(board, QStringLiteral("A"));
        QVERIFY(a);
        QCOMPARE(a->type, QStringLiteral("terminator"));
        QCOMPARE(a->displayText(), QStringLiteral("Début"));
        const QString aId = a->id; // shapes are rebuilt by undo: kept by id

        const Shape *c = board.createShapeAt(QPointF(300, 0), QStringLiteral("decision"),
                                             QJsonObject{{QStringLiteral("text"), QStringLiteral("Valid?")},
                                                         {QStringLiteral("variant"), QStringLiteral("diamond")}},
                                             false);
        QVERIFY(c);
        QCOMPARE(board.shapes().size(), size_t(3));
        QCOMPARE(board.selection(), QSet<QString>{c->id});
        QVERIFY(board.canUndo());
        board.undo();
        QCOMPARE(board.shapes().size(), size_t(2));
        board.redo();
        QCOMPARE(board.shapes().size(), size_t(3));

        board.selectShape(aId);
        board.deleteSelection(); // with its links
        QCOMPARE(board.shapes().size(), size_t(2));
        QCOMPARE(board.links().size(), size_t(0));
        board.undo();
        QCOMPARE(board.links().size(), size_t(1));

        board.selectAll();
        QVERIFY(board.paintSelection(QStringLiteral("#ffcdd2")));
        QCOMPARE(byKey(board, QStringLiteral("A"))->customFill, QStringLiteral("#ffcdd2"));
        board.duplicateSelection();
        QCOMPARE(board.shapes().size(), size_t(6));
        // A copy is a new element: it gets a key of its own.
        QVERIFY(byKey(board, QStringLiteral("A_copy")));
    }

    void writesMermaidBackWithItsLayout()
    {
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"), kMermaid, &loaded));
        QVERIFY(loaded.needsArrange); // nothing placed yet
        QVERIFY(graphviz::autoArrange(board, {}));
        const Shape *a = byKey(board, QStringLiteral("A"));
        const Shape *b = byKey(board, QStringLiteral("B"));
        QVERIFY(b->y > a->y + a->h); // B below A, not on top of it
        QVERIFY(board.links().front()->spline);

        const QByteArray saved = document::save(board, *loaded.codec);
        QVERIFY(saved.startsWith("flowchart TB\n  A([\"Début\"])\n  B[\"Étape\"]\n  A --> B\n"));
        QVERIFY(saved.contains("%% kodamiko {\"key\":\"A\""));

        // Read back: every element keeps its place; nothing to lay out.
        Board again;
        document::Loaded reloaded;
        QVERIFY(loadInto(again, QStringLiteral("a.mmd"), saved, &reloaded));
        QVERIFY(!reloaded.needsArrange);
        QCOMPARE(byKey(again, QStringLiteral("B"))->y, std::round(b->y * 10) / 10);
        QVERIFY(again.links().front()->spline);

        // An element Claude adds without a place goes beside its neighbor;
        // the others keep theirs.
        Board edited;
        QVERIFY(loadInto(edited, QStringLiteral("a.mmd"), saved + "  B --> C{\"Done?\"}\n", &reloaded));
        QVERIFY(!reloaded.needsArrange);
        const Shape *c = byKey(edited, QStringLiteral("C"));
        QCOMPARE(c->type, QStringLiteral("decision"));
        QCOMPARE(reloaded.unplaced, QStringList{c->id});
        graphviz::placeNewcomers(edited, reloaded.unplaced, {});
        const Shape *editedB = byKey(edited, QStringLiteral("B"));
        QCOMPARE(editedB->y, std::round(b->y * 10) / 10);
        QVERIFY(c->y > editedB->y + editedB->h); // below B, as a full layout would put it
        QVERIFY(!c->rect().intersects(editedB->rect()));
    }

    void readsTheMermaidFlowchartSyntax()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"),
                         "graph LR\n  Début((Go)) -- yes --> B{{Prepare}} & C[(Store)]\n"
                         "  B -.-> D>Odd] ==>|done| E[\"a --> b\"]\n  E --o F[/In/]\n  F ~~~ A@{ shape: dbl-circ }\n"
                         "  classDef x fill:#f00\n"));
        QCOMPARE(board.meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("LR"));
        QCOMPARE(board.shapes().size(), size_t(7));
        QCOMPARE(byKey(board, QStringLiteral("Début"))->type, QStringLiteral("terminator"));
        QCOMPARE(byKey(board, QStringLiteral("E"))->displayText(), QStringLiteral("a --> b")); // one node
        QCOMPARE(board.links().size(), size_t(6));
        QStringList kinds;
        for (const auto &l : board.links()) {
            const auto *flow = dynamic_cast<const flow::FlowLink *>(l.get());
            kinds.append(flow->line + QLatin1Char('/') + flow->head + QLatin1Char('/') + l->label);
        }
        QCOMPARE(kinds, (QStringList{"solid/arrow/yes", "solid/arrow/yes", "dotted/arrow/", "thick/arrow/done",
                                     "solid/circle/", "invisible/none/"}));
    }

    void wrappingKeepsABlankLineOfSpacesOrOfACarriageReturn()
    {
        const Font font(14);

        QCOMPARE(text::wrap(font, "a\n \nb", 200), (QStringList{"a", "", "b"}));
        QCOMPARE(text::wrap(font, "a\r\n\r\nb", 200), (QStringList{"a", "", "b"}));
    }

    void wrappingNeverCutsAnEmojiInHalf()
    {
        const Font font(14);
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
        const QString label = emoji.repeated(12);

        // Widths around one and a few emoji, so that the best fit falls
        // between the two halves of a pair for some of them.
        for (qreal maxWidth = 6; maxWidth < 80; maxWidth += 3) {
            for (const QString &line : text::wrap(font, label, maxWidth)) {
                QVERIFY2(!line.isEmpty() && !line.back().isHighSurrogate() && !line.front().isLowSurrogate(),
                         qPrintable(QStringLiteral("width %1").arg(maxWidth)));
            }
        }
    }

    void anArrowheadIsSeventyPercentAsWideAsItIsLong()
    {
        const qreal size = 9;
        const qreal length = size * std::cos(graphvizNormalArrowSpread);
        const qreal base = 2 * size * std::sin(graphvizNormalArrowSpread);

        QVERIFY(qFuzzyCompare(base / length, 0.7));
        QVERIFY(qFuzzyCompare(arrowBaseInset(size), length));
    }

    void drawsATerminatorWithoutVariantAsAStadium()
    {
        Board board;
        board.setFamily(&families::flowchart());
        auto *unset =
            dynamic_cast<flow::FlowShape *>(board.addShape(QStringLiteral("terminator"), {{"text", "Start"}}));
        auto *stadium = dynamic_cast<flow::FlowShape *>(
            board.addShape(QStringLiteral("terminator"), {{"text", "Start"}, {"variant", "stadium"}}));
        QVERIFY(unset && stadium);
        stadium->x = unset->x;
        stadium->y = unset->y;

        // Links meet it where they meet a stadium, not an ellipse.
        for (const QPointF toward : {QPointF(unset->x - 40, unset->y - 40), QPointF(unset->x + 20, unset->y - 80)})
            QCOMPARE(unset->borderPoint(toward), stadium->borderPoint(toward));
    }

    void freeTextShrinksWithItsText()
    {
        Board board;
        board.setFamily(&families::flowchart());
        auto *text = dynamic_cast<flow::FlowTextShape *>(board.addShape(QStringLiteral("flowText"), {}));
        QVERIFY(text);
        text->text = QStringLiteral("A long line of free text that wraps past its width");
        board.measureShape(*text);
        const qreal wide = text->w;

        text->text = QStringLiteral("Hi");
        board.measureShape(*text);

        QVERIFY2(text->w < wide / 2, qPrintable(QStringLiteral("%1 then %2").arg(wide).arg(text->w)));
    }

    void keepsTheFlowchartLinksToTheCode()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"),
                         "flowchart TB\n  A[Hook] --> B[Done]\n  click A href \"src/agent/HookServer.h\"\n"));

        const QByteArray saved = document::save(board, *codecFor(QStringLiteral("mermaid"), "flowchart TB"));

        QCOMPARE(byKey(board, QStringLiteral("A"))->codeLink, QStringLiteral("src/agent/HookServer.h"));
        QVERIFY(byKey(board, QStringLiteral("B"))->codeLink.isEmpty());
        QVERIFY(saved.contains("\n  click A href \"src/agent/HookServer.h\"\n"));
    }

    void appliesAnEditedSource()
    {
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"), kMermaid, &loaded));
        QVERIFY(graphviz::autoArrange(board, {}));
        const QPointF before(byKey(board, QStringLiteral("A"))->x, byKey(board, QStringLiteral("A"))->y);
        const QString source = document::source(board, *loaded.codec);

        // Same elements: they keep their place.
        const std::optional<document::Loaded> applied = document::applySource(board, *loaded.codec, source, nullptr);
        QVERIFY(applied);
        QVERIFY(!applied->needsArrange);
        // The layout is kept to a tenth of a pixel.
        QVERIFY(std::abs(byKey(board, QStringLiteral("A"))->x - before.x()) < 0.1);
        QVERIFY(std::abs(byKey(board, QStringLiteral("A"))->y - before.y()) < 0.1);
        // One step of history.
        const std::optional<document::Loaded> extended =
            document::applySource(board, *loaded.codec, source + "\n  B --> C[New]", nullptr);
        QVERIFY(extended);
        QCOMPARE(extended->unplaced.size(), 1);
        QCOMPARE(board.shapes().size(), size_t(3));
        board.undo();
        QCOMPARE(board.shapes().size(), size_t(2));

        QString error;
        QVERIFY(!document::applySource(board, *loaded.codec, "not a diagram", &error));
        QVERIFY(!error.isEmpty());
    }

    void opensFlowboardFiles()
    {
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("old.flow"), kFlowboard, &loaded));
        QVERIFY(!loaded.needsArrange); // the coordinates are in the file
        QCOMPARE(byKey(board, QStringLiteral("B"))->y, 120.0);
        const QByteArray saved = document::save(board, *loaded.codec);
        QVERIFY(saved.contains("\"format\": \"flowboard\""));
        QVERIFY(saved.contains("Étape"));
    }

    // The gestures, with the mouse and the keyboard.
    void editsWithTheMouseAndKeyboard()
    {
        const QTemporaryDir dir;
        Board placed;
        document::Loaded loaded;
        QVERIFY(loadInto(placed, QStringLiteral("a.mmd"), kMermaid, &loaded));
        QVERIFY(graphviz::autoArrange(placed, {}));
        const QString path = writeFile(dir, QStringLiteral("gestures.mmd"), document::save(placed, *loaded.codec));

        DiagramView view(Theme::kodamiko());
        view.resize(1000, 700);
        QString error;
        QVERIFY2(view.load(path, &error), qPrintable(error));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        Canvas *canvas = view.canvas();
        Board *board = view.board();
        canvas->fitView(false);
        const auto screen = [&](QPointF world) { return canvas->camera().toScreen(world).toPoint(); };
        const Shape *a = byKey(*board, QStringLiteral("A"));
        const QString aId = a->id;
        const qreal ax = a->x;
        const QString bId = byKey(*board, QStringLiteral("B"))->id;

        // Drag A to the right.
        const QPoint start = screen(QPointF(a->cx(), a->cy()));
        QTest::mousePress(canvas, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(canvas, start + QPoint(step * 15, 0));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, start + QPoint(150, 0));
        const qreal moved = board->shape(aId)->x - ax;
        QVERIFY2(std::fabs(moved - 150 / canvas->zoom()) < 8, qPrintable(QString::number(moved)));
        QVERIFY(view.isModified());

        // Ctrl+Z puts it back.
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(board->shape(aId)->x, ax);
        QVERIFY(!view.isModified());

        // Link B to A from B's east port (ports show on the selection).
        board->selectShape(bId);
        const Shape *b = board->shape(bId);
        const QList<Port> ports = b->connectionPorts(*board);
        const QPoint port = screen(QPointF(ports[1].x, ports[1].y));
        a = board->shape(aId);
        const QPoint target = screen(QPointF(a->cx(), a->cy()));
        QTest::mouseMove(canvas, port);
        QTest::mousePress(canvas, Qt::LeftButton, {}, port);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(canvas, port + (target - port) * step / 10);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, target);
        QCOMPARE(board->links().size(), size_t(2));
        QCOMPARE(board->links().back()->from, bId);
        QCOMPARE(board->links().back()->to, aId);

        // Double-click A, type, Enter: one step of history.
        a = board->shape(aId);
        QTest::mouseDClick(canvas, Qt::LeftButton, {}, screen(QPointF(a->cx(), a->cy())));
        QVERIFY(canvas->isEditingText());
        auto *editor = canvas->findChild<QPlainTextEdit *>();
        QVERIFY(editor && editor->hasFocus());
        QTest::keyClicks(editor, QStringLiteral("Accueil"));
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(!canvas->isEditingText());
        QCOMPARE(board->shape(aId)->displayText(), QStringLiteral("Accueil"));
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(board->shape(aId)->displayText(), QStringLiteral("Début"));

        // Delete removes the selection.
        board->selectShape(bId);
        QTest::keyClick(canvas, Qt::Key_Delete);
        QVERIFY(!board->shape(bId));

        // Saved, the file is Mermaid with the layout after it.
        QVERIFY2(view.save(&error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY(saved.startsWith("flowchart TB\n"));
        QVERIFY(!saved.contains("B[\"Étape\"]"));
    }

    void checksDiagramsForClaude()
    {
        const QString mmd = QStringLiteral("docs/a.mmd");
        QVERIFY(DiagramCheck::problems(mmd, kMermaid).isEmpty());
        QVERIFY(DiagramCheck::problems(mmd, "  \n").isEmpty());
        QVERIFY(DiagramCheck::problems(QStringLiteral("a.flow"), kFlowboard).isEmpty());

        const QStringList unknown = DiagramCheck::problems(mmd, "pie title Pets\n  \"Dogs\" : 386\n");
        QCOMPARE(unknown.size(), 1);
        QVERIFY(unknown.first().startsWith(QStringLiteral("Unsupported Mermaid diagram")));

        const QStringList garbage = DiagramCheck::problems(mmd, "flowchart TB\n  A --> B\n  this is not mermaid\n");
        QCOMPARE(garbage, QStringList{QStringLiteral("Not understood: this is not mermaid")});

        QCOMPARE(DiagramCheck::feedback(QStringLiteral("docs/a.mmd"), garbage),
                 QStringLiteral("Kodamiko's diagram editor read docs/a.mmd with problems:\n- %1\n"
                                "Fix the file so that it reads as you meant it.")
                     .arg(garbage.first()));
    }

    void listsTheDiagramKinds()
    {
        const QList<DiagramKind> kinds = DiagramKinds::available();
        QVERIFY(!kinds.isEmpty());
        const DiagramKind flowchart = DiagramKinds::find(QStringLiteral("flowchart"));
        QCOMPARE(flowchart.extension, QStringLiteral("mmd"));
        // Every kind's blank diagram reads cleanly, in its own family.
        for (const DiagramKind &kind : kinds) {
            const QString name = QStringLiteral("new.") + kind.extension;
            QVERIFY2(DiagramCheck::problems(name, kind.blank).isEmpty(), qPrintable(kind.id));
            Board board;
            document::Loaded loaded;
            QVERIFY2(loadInto(board, name, kind.blank, &loaded), qPrintable(kind.id));
            QCOMPARE(loaded.codec->family().id, kind.id);
        }
        QCOMPARE(DiagramKinds::withExtension(QStringLiteral("docs/returns"), flowchart),
                 QStringLiteral("docs/returns.mmd"));
        QCOMPARE(DiagramKinds::withExtension(QStringLiteral("Returns.MMD"), flowchart), QStringLiteral("Returns.MMD"));
        QVERIFY(DiagramKinds::find(QStringLiteral("nope")).id.isEmpty());
    }

    void opensDiagramFilesInTheDiagramEditor()
    {
        const QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("process.mmd"), kMermaid);
        QVERIFY(DiagramView::isDiagramFile(path));
        QVERIFY(DiagramView::isDiagramFile(dir.filePath(QStringLiteral("old.flow"))));
        QVERIFY(!DiagramView::isDiagramFile(dir.filePath(QStringLiteral("notes.md"))));

        EditorTabs tabs(Theme::kodamiko());
        auto *view = qobject_cast<DiagramView *>(tabs.openFile(path));
        QVERIFY(view);
        QCOMPARE(view->filePath(), QFileInfo(path).absoluteFilePath());
        QVERIFY(!view->isModified());        // laying it out is not a change
        QCOMPARE(tabs.openFile(path), view); // no second tab
        QCOMPARE(tabs.count(), 1);
        QCOMPARE(tabs.currentFilePath(), view->filePath());
        QCOMPARE(tabs.openFilePaths(), QStringList{view->filePath()});
    }

    // Ctrl+click from a diagram to the code, then Back (Alt+←) to the element
    // it started from, and Forward (Alt+→) to the code again.
    // A link that names the header goes to the code: the definition in the
    // .cpp, as Ctrl+click in the editor does.
    void followsALinkToAHeaderToTheDefinition()
    {
        const QTemporaryDir dir;
        const QString header =
            writeFile(dir, QStringLiteral("Queue.h"), "class Queue {\npublic:\n    void enqueue(int item);\n};\n");
        const QString source = writeFile(dir, QStringLiteral("Queue.cpp"),
                                         "#include \"Queue.h\"\n\nvoid Queue::enqueue(int item)\n{\n}\n");
        EditorTabs tabs(Theme::kodamiko());
        SymbolIndex symbols;
        symbols.setFolder(dir.path());
        symbols.updateFile(header, "class Queue {\npublic:\n    void enqueue(int item);\n};\n");
        symbols.updateFile(source, "#include \"Queue.h\"\n\nvoid Queue::enqueue(int item)\n{\n}\n");
        CodeNavigator navigator(&tabs, &symbols);

        navigator.goToSymbol(QStringLiteral("enqueue"), QStringLiteral("Queue"), QStringLiteral("Queue.h"));

        QVERIFY(tabs.currentEditor());
        QCOMPARE(tabs.currentEditor()->filePath(), QFileInfo(source).absoluteFilePath());
    }

    // The links of a diagram in the other languages the symbol index reads:
    // members qualified with ".", a TypeScript declaration to its
    // implementation, a root file named like a symbol.
    void followsLinksInJavaJavaScriptAndTypeScript()
    {
        const QTemporaryDir dir;
        const QHash<QString, QByteArray> files{
            {QStringLiteral("Queue.java"), "class Queue {\n    void enqueue(int item) {\n    }\n}\n"},
            {QStringLiteral("queue.js"), "class Queue {\n  enqueue(item) {\n  }\n}\n"},
            {QStringLiteral("queue.d.ts"), "export declare function drain(): void;\n"},
            {QStringLiteral("drain.ts"), "export function drain(): void {\n}\n"},
        };
        EditorTabs tabs(Theme::kodamiko());
        SymbolIndex symbols;
        symbols.setFolder(dir.path());
        for (auto it = files.begin(); it != files.end(); ++it)
            symbols.updateFile(writeFile(dir, it.key(), it.value()), it.value());
        CodeNavigator navigator(&tabs, &symbols);
        const auto openedAt = [&tabs] {
            const EditorWidget *editor = tabs.currentEditor();
            if (!editor)
                return QString();
            int line = 0;
            int column = 0;
            editor->getCursorPosition(&line, &column);
            return QStringLiteral("%1:%2").arg(QFileInfo(editor->filePath()).fileName()).arg(line + 1);
        };

        navigator.goToSymbol(QStringLiteral("enqueue"), QStringLiteral("Queue"), QStringLiteral("Queue.java"));
        const QString java = openedAt();
        navigator.goToSymbol(QStringLiteral("enqueue"), QStringLiteral("Queue"), QStringLiteral("queue.js"));
        const QString javaScript = openedAt();
        navigator.goToSymbol(QStringLiteral("drain"), {}, QStringLiteral("queue.d.ts"));
        const QString typeScript = openedAt();
        navigator.goToSymbol(QStringLiteral("ts"), QStringLiteral("drain"), {});
        const QString rootFile = openedAt();

        QCOMPARE(java, QStringLiteral("Queue.java:2"));
        QCOMPARE(javaScript, QStringLiteral("queue.js:2"));
        QCOMPARE(typeScript, QStringLiteral("drain.ts:1"));
        QCOMPARE(rootFile, QStringLiteral("drain.ts:1"));
    }

    void comesBackToTheDiagramElementWithBack()
    {
        const QTemporaryDir dir;
        const QString diagramPath =
            writeFile(dir, QStringLiteral("agents.mmd"), "classDiagram\n  class Hook\n  class Other\n");
        const QString codePath =
            QFileInfo(writeFile(dir, QStringLiteral("Hook.h"), "class Hook {};\n")).absoluteFilePath();
        EditorTabs tabs(Theme::kodamiko());
        SymbolIndex symbols;
        CodeNavigator navigator(&tabs, &symbols);
        auto *view = qobject_cast<DiagramView *>(tabs.openFile(diagramPath));
        QVERIFY(view);
        view->revealElement(QStringLiteral("Hook"));

        navigator.goTo(codePath, 6);
        QVERIFY(tabs.currentEditor());
        view->board()->clearSelection();
        navigator.goBack();
        const bool showsTheDiagram = tabs.currentWidget() == view;
        const QString element = view->selectedElement();
        navigator.goForward();

        QVERIFY(showsTheDiagram);
        QCOMPARE(element, QStringLiteral("Hook"));
        QVERIFY(tabs.currentEditor());
        QCOMPARE(tabs.currentEditor()->filePath(), codePath);
    }

    void refusesInvalidDiagrams()
    {
        const QTemporaryDir dir;
        DiagramView view(Theme::kodamiko());
        QString error;
        QVERIFY(!view.load(writeFile(dir, QStringLiteral("broken.flow"), "{ not json"), &error));
        QVERIFY(!error.isEmpty());
        const QString pie = writeFile(dir, QStringLiteral("pie.mmd"), "pie\n  \"a\" : 1");
        QVERIFY(!view.load(pie, &error));

        // In a tab, such a file opens as text.
        EditorTabs tabs(Theme::kodamiko());
        QSignalSpy messages(&tabs, &EditorTabs::statusMessage);
        QWidget *page = tabs.openFile(pie);
        QVERIFY(page && !qobject_cast<DiagramView *>(page));
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.last().first().toString().startsWith(QStringLiteral("pie.mmd opened as text: ")));
    }

    void savesAndRecognisesItsOwnWrites()
    {
        const QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("process.mmd"), kMermaid);
        DiagramView view(Theme::kodamiko());
        QString error;
        QVERIFY2(view.load(path, &error), qPrintable(error));
        QVERIFY(view.matchesDisk());

        QVERIFY2(view.save(&error), qPrintable(error));
        QVERIFY(view.matchesDisk());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY(saved.contains("Étape"));
        QVERIFY(saved.contains("%% kodamiko "));

        // A change made by someone else is not ours.
        writeFile(dir, QStringLiteral("process.mmd"), saved + "\n");
        QVERIFY(!view.matchesDisk());
        QVERIFY2(view.reloadFromDisk(&error), qPrintable(error));
        QVERIFY(view.matchesDisk());
        QVERIFY(!view.isModified());
    }

    void opensAnEmptyFileAsABlankDiagram()
    {
        const QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("new.mmd"), QByteArray());
        DiagramView view(Theme::kodamiko());
        QString error;
        QVERIFY2(view.load(path, &error), qPrintable(error));
        QVERIFY(!view.isModified());
        QVERIFY2(view.save(&error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("flowchart TB"));
    }

    void severalDiagramsCanBeOpenAtOnce()
    {
        const QTemporaryDir dir;
        const QString first = writeFile(dir, QStringLiteral("a.mmd"), kMermaid);
        const QString second = writeFile(dir, QStringLiteral("b.flow"), kFlowboard);
        EditorTabs tabs(Theme::kodamiko());
        QVERIFY(qobject_cast<DiagramView *>(tabs.openFile(first)));
        QVERIFY(qobject_cast<DiagramView *>(tabs.openFile(second)));
        QCOMPARE(tabs.count(), 2);
        QVERIFY(tabs.saveAll());
        QVERIFY(tabs.closeAll());
    }
};

QTEST_MAIN(TestDiagramView)
#include "tst_diagramview.moc"
