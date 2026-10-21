// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "index/OccurrenceStore.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "navigation/WorkspaceEditing.h"
#include "refactor/EditJournal.h"
#include "refactor/EditPreview.h"
#include "refactor/FileReferences.h"
#include "refactor/MoveClass.h"
#include "refactor/RefactoringHistory.h"
#include "refactor/RefactoringPreviewPopup.h"
#include "refactor/RenameAnalysis.h"
#include "refactor/RenameSymbolPopup.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace LspProtocol;
using Kind = FileOperation::Kind;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

TextEdit replacing(int line, int start, int end, const QString &text)
{
    return {{{line, start}, {line, end}}, text};
}

// Empties what `journal` put in the trash ("…/files/name", described by
// "…/info/name.trashinfo"), which tests do not take back.
void emptyTrashOf(const EditJournal &journal)
{
    for (const EditJournal::Entry &entry : journal.entries()) {
        const auto *trashing = std::get_if<EditJournal::Trashing>(&entry);
        if (!trashing)
            continue;
        const QFileInfo inTrash(trashing->pathInTrash);
        QFile::remove(trashing->pathInTrash);
        QFile::remove(inTrash.absolutePath() + "/../info/" + inTrash.fileName() + ".trashinfo");
    }
}

// A project folder with its editors and (idle) language servers.
struct Project {
    QTemporaryDir dir;
    EditorTabs tabs{Theme::dark()};
    LanguageServers servers;

    QString path(const QString &name) const { return dir.filePath(name); }

    EditTargets targets() { return {.tabs = &tabs, .servers = &servers}; }
};

} // namespace

class TestRefactor : public QObject {
    Q_OBJECT

private slots:

    void appliesTextEditsAndFileOperationsInOrder()
    {
        Project project;
        write(project.path("old.h"), "int total;\n");
        write(project.path("gone.h"), "x\n");
        QVERIFY(project.tabs.openFile(project.path("old.h")));
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("old.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{
                .kind = Kind::Rename, .filePath = project.path("old.h"), .newPath = project.path("inc/new.h")},
            FileOperation{.kind = Kind::Create, .filePath = project.path("created.h")},
            FileOperation{.kind = Kind::Delete, .filePath = project.path("gone.h")},
        }};

        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, project.targets());

        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));
        QCOMPARE(outcome.filesChanged, 3);
        QCOMPARE(read(project.path("inc/new.h")), QByteArray("int sum;\n")); // saved before moving
        QVERIFY(!QFileInfo::exists(project.path("old.h")));
        QVERIFY(project.tabs.editorFor(project.path("inc/new.h")));
        QVERIFY(QFileInfo::exists(project.path("created.h")));
        QVERIFY(!QFileInfo::exists(project.path("gone.h")));
        emptyTrashOf(outcome.journal);
    }

    void undoesTheChangesAlreadyMadeWhenOneFails()
    {
        Project project;
        write(project.path("open.h"), "int total;\n");
        write(project.path("closed.h"), "int total;\n");
        const auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("open.h")));
        QVERIFY(editor);
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("open.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("closed.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = project.path("missing.h"), .newPath = project.path("x.h")},
        }};

        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, project.targets());

        QVERIFY(!outcome.errors.isEmpty());
        QCOMPARE(outcome.filesChanged, 0);
        QVERIFY(outcome.journal.isEmpty());
        QCOMPARE(editor->text(), QString("int total;\n"));
        QCOMPARE(read(project.path("closed.h")), QByteArray("int total;\n"));
    }

    void undoesARefactoringInOpenAndClosedFiles()
    {
        Project project;
        write(project.path("open.h"), "int total;\n");
        write(project.path("closed.h"), "int total;\r\n"); // keeps its line ending
        write(project.path("moved.h"), "m\n");
        write(project.path("gone.h"), "g\n");
        const auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("open.h")));
        QVERIFY(editor);
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("open.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("closed.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = project.path("moved.h"), .newPath = project.path("m/to.h")},
            FileOperation{.kind = Kind::Create, .filePath = project.path("created.h")},
            FileOperation{.kind = Kind::Delete, .filePath = project.path("gone.h")},
        }};
        WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, project.targets());
        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));
        RefactoringHistory history;
        history.record("Rename “total” to “sum”", std::move(outcome.journal));
        QCOMPARE(history.lastDescription(), QString("Rename “total” to “sum”"));

        const QStringList errors = history.undoLast(project.targets());

        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(editor->text(), QString("int total;\n"));
        QCOMPARE(read(project.path("closed.h")), QByteArray("int total;\r\n"));
        QCOMPARE(read(project.path("moved.h")), QByteArray("m\n"));
        QVERIFY(!QFileInfo::exists(project.path("m/to.h")));
        QVERIFY(!QFileInfo::exists(project.path("created.h")));
        QCOMPARE(read(project.path("gone.h")), QByteArray("g\n")); // back from the trash
        QVERIFY(history.lastDescription().isEmpty());
    }

    void undoesTheTextsOfAFileRenamedByTheRefactoring()
    {
        Project project;
        write(project.path("old.h"), "int total;\n");
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("old.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = project.path("old.h"), .newPath = project.path("new.h")},
        }};
        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, project.targets());
        QVERIFY(outcome.errors.isEmpty());

        QVERIFY(outcome.journal.changedSince(project.targets()).isEmpty()); // found under its new name
        const QStringList errors = outcome.journal.revert(project.targets());

        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(read(project.path("old.h")), QByteArray("int total;\n"));
        QVERIFY(!QFileInfo::exists(project.path("new.h")));
    }

    void keepsARefactoringWhoseFilesChangedSince()
    {
        Project project;
        write(project.path("a.h"), "int total;\n");
        write(project.path("b.h"), "int total;\n");
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("a.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("b.h"), {}, {replacing(0, 4, 9, "sum")}},
        }};
        WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, project.targets());
        RefactoringHistory history;
        history.record("Rename", std::move(outcome.journal));
        auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("b.h")));
        QVERIFY(editor);
        editor->setText("int sum = 1;\n"); // typed after the refactoring

        const QStringList errors = history.undoLast(project.targets());

        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors.first().contains("b.h"), qPrintable(errors.first()));
        QCOMPARE(read(project.path("a.h")), QByteArray("int sum;\n")); // nothing undone
        QCOMPARE(history.lastDescription(), QString("Rename"));        // still undoable once fixed
    }

    void previewsTheEditInMemoryFollowingRenamedFiles()
    {
        Project project;
        write(project.path("open.h"), "int total;\n");
        write(project.path("old.h"), "int total;\r\n");
        auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("open.h")));
        QVERIFY(editor);
        editor->setText("int total; // unsaved\n");
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("open.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = project.path("old.h"), .newPath = project.path("new.h")},
            DocumentEdit{project.path("new.h"), {}, {{{{0, 10}, {0, 10}}, "\nint more;"}}},
        }};

        const QList<RefactoredFile> files = EditPreview::simulate(edit, project.targets());

        QCOMPARE(files.size(), 2);
        QCOMPARE(files[0].after, QString("int sum; // unsaved\n"));
        QVERIFY(files[0].hasTextEditsOnly);
        QCOMPARE(files[1].kind, RefactoredFile::Kind::Renamed);
        QCOMPARE(files[1].originalPath, project.path("old.h"));
        QCOMPARE(files[1].after, QString("int total;\r\nint more;\r\n")); // its line ending
        QVERIFY(!files[1].hasTextEditsOnly);
        const QList<DiffFile> diffs = EditPreview::diffFiles(files, project.dir.path());
        QCOMPARE(diffs[1].path, QString("new.h"));
        QCOMPARE(diffs[1].status, QString("renamed from old.h"));
        QCOMPARE(diffs[1].patch.addedLines(), 1);
        QCOMPARE(read(project.path("old.h")), QByteArray("int total;\r\n")); // nothing changed
    }

    void previewLeavesTheUncheckedFilesOut()
    {
        Project project;
        write(project.path("a.h"), "int total;\n");
        write(project.path("b.h"), "int total;\n");
        write(project.path("c.h"), "c\n");
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("a.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("b.h"), {}, {replacing(0, 4, 9, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = project.path("c.h"), .newPath = project.path("d.h")},
        }};
        std::optional<WorkspaceEdit> applied;
        QWidget window;
        auto *popup = new RefactoringPreviewPopup(
            {"Rename", project.dir.path(), edit, EditPreview::simulate(edit, project.targets())},
            [&applied](const WorkspaceEdit &kept) { applied = kept; }, &window);
        window.show();
        popup->popup();
        QCOMPARE(popup->files()->count(), 3);
        QVERIFY(!(popup->files()->item(2)->flags() & Qt::ItemIsUserCheckable)); // renamed: kept

        popup->files()->item(1)->setCheckState(Qt::Unchecked);
        popup->accept();

        QVERIFY(applied);
        QCOMPARE(applied->changes.size(), 2);
        QCOMPARE(std::get<DocumentEdit>(applied->changes[0]).filePath, project.path("a.h"));
        QVERIFY(std::holds_alternative<FileOperation>(applied->changes[1]));
    }

    void renamePopupAsksForAPreviewOrARefactoring()
    {
        QWidget window;
        window.show();
        QList<RenameSymbolPopup::Choice> choices;
        auto *popup = new RenameSymbolPopup(
            "total", "a.cpp",
            [&choices](const RenameSymbolPopup::Choice &choice) {
                if (choice.newName == "total")
                    return QString("Same name");
                choices.append(choice);
                return QString();
            },
            &window);
        popup->popup();
        QCOMPARE(popup->field()->text(), QString("total"));

        popup->preview();
        QCOMPARE(popup->error(), QString("Same name")); // still open
        popup->field()->setText("sum");
        popup->commentsAndStrings()->setChecked(false);
        popup->preview();

        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices[0].newName, QString("sum"));
        QCOMPARE(choices[0].mode, RenameSymbolPopup::Mode::Preview);
        QVERIFY(!choices[0].inCommentsAndStrings);
    }

    void findsTheNameInCommentsStringsAndTextOutsideTheServersEdit()
    {
        const Project project;
        write(project.path("a.cpp"), "// total of all\nint total = 0; // é total\nconst char *s = \"total\";\n");
        write(project.path("notes.md"), "The total is kept.\n");
        write(project.path("other.cpp"), "int unrelated;\n");
        OccurrenceStore index;
        index.setFiles({{project.path("a.cpp"), {1, 1}, {{"total", 3, 0, OccurrenceRole::Comment}}},
                        {project.path("notes.md"), {1, 1}, {{"total", 4, 0, OccurrenceRole::Text}}}});
        // The server renames the variable on line 1.
        const WorkspaceEdit codeEdit{{DocumentEdit{project.path("a.cpp"), {}, {replacing(1, 4, 9, "sum")}}}};

        const RenameAnalysis::Result result = RenameAnalysis::analyze({.name = "total",
                                                                       .newName = "sum",
                                                                       .codeEdit = codeEdit,
                                                                       .index = &index,
                                                                       .unsavedTexts = {},
                                                                       .projectFolder = project.dir.path(),
                                                                       .searchesCommentsAndStrings = true});

        QCOMPARE(result.textualEdit.changes.size(), 2);
        const auto &inSource = std::get<DocumentEdit>(result.textualEdit.changes[0]);
        QCOMPARE(inSource.filePath, project.path("a.cpp"));
        QCOMPARE(inSource.edits, (QList<TextEdit>{replacing(0, 3, 8, "sum"), replacing(1, 20, 25, "sum"),
                                                  replacing(2, 17, 22, "sum")})); // "é" is one UTF-16 unit
        QCOMPARE(std::get<DocumentEdit>(result.textualEdit.changes[1]).filePath, project.path("notes.md"));
        QVERIFY(result.conflicts.isEmpty());
    }

    void findsTheNameInAnUnsavedText()
    {
        const Project project;
        const RenameAnalysis::Result result =
            RenameAnalysis::analyze({.name = "total",
                                     .newName = "sum",
                                     .codeEdit = {},
                                     .index = nullptr,
                                     .unsavedTexts = {{project.path("a.cpp"), "/* total */\n"}},
                                     .projectFolder = project.dir.path(),
                                     .searchesCommentsAndStrings = true});

        QCOMPARE(result.textualEdit.changes.size(), 1);
    }

    void reportsTheConflictsOfARename()
    {
        const Project project;
        write(project.path("a.cpp"), "int total;\nint sum;\n");
        write(project.path("locked.cpp"), "int total;\n");
        QFile::setPermissions(project.path("locked.cpp"), QFile::ReadOwner);
        const WorkspaceEdit codeEdit{{
            DocumentEdit{project.path("a.cpp"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("locked.cpp"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{"/elsewhere/b.cpp", {}, {replacing(0, 4, 9, "sum")}},
        }};

        const RenameAnalysis::Result result = RenameAnalysis::analyze({.name = "total",
                                                                       .newName = "sum",
                                                                       .codeEdit = codeEdit,
                                                                       .index = nullptr,
                                                                       .unsavedTexts = {},
                                                                       .projectFolder = project.dir.path(),
                                                                       .searchesCommentsAndStrings = false});

        QCOMPARE(result.conflicts,
                 (QStringList{"“sum” is already declared in a.cpp:2", "locked.cpp is read-only",
                              QDir::toNativeSeparators("/elsewhere/b.cpp") + " is outside the project"}));
        QVERIFY(result.textualEdit.isEmpty());
        QFile::setPermissions(project.path("locked.cpp"), QFile::ReadOwner | QFile::WriteOwner);
    }

    void mergesTheTextualEditsIntoTheCodeEditOfTheirFile()
    {
        const WorkspaceEdit code{{
            DocumentEdit{"/p/a.cpp", 3, {replacing(1, 0, 5, "sum")}},
            FileOperation{.kind = Kind::Rename, .filePath = "/p/a.cpp", .newPath = "/p/b.cpp"},
        }};
        const WorkspaceEdit textual{{
            DocumentEdit{"/p/a.cpp", {}, {replacing(0, 3, 8, "sum")}},
            DocumentEdit{"/p/notes.md", {}, {replacing(0, 4, 9, "sum")}},
        }};

        const WorkspaceEdit merged = EditPreview::merge(code, textual);

        QCOMPARE(merged.changes.size(), 3);
        QCOMPARE(std::get<DocumentEdit>(merged.changes[0]).filePath, QString("/p/notes.md")); // first
        const auto &source = std::get<DocumentEdit>(merged.changes[1]);
        QCOMPARE(source.version, std::optional<int>(3));
        QCOMPARE(source.edits.size(), 2);
        QVERIFY(std::holds_alternative<FileOperation>(merged.changes[2]));
    }

    void previewShowsTheTextualFilesAndTheConflicts()
    {
        Project project;
        write(project.path("a.cpp"), "int total; // total\n");
        const WorkspaceEdit code{{DocumentEdit{project.path("a.cpp"), {}, {replacing(0, 4, 9, "sum")}}}};
        const WorkspaceEdit textual{{DocumentEdit{project.path("a.cpp"), {}, {replacing(0, 14, 19, "sum")}}}};
        std::optional<WorkspaceEdit> applied;
        QWidget window;
        auto *popup = new RefactoringPreviewPopup(
            {.title = "Rename",
             .root = project.dir.path(),
             .edit = code,
             .files = EditPreview::simulate(code, project.targets()),
             .textualEdit = textual,
             .textualFiles = EditPreview::simulate(textual, project.targets()),
             .conflicts = {"“sum” is already declared in a.cpp:3"}},
            [&applied](const WorkspaceEdit &kept) { applied = kept; }, &window);
        window.show();
        popup->popup();
        QCOMPARE(popup->files()->count(), 2);
        QCOMPARE(popup->files()->item(1)->text(), QString("a.cpp · in comments"));
        QVERIFY(popup->conflicts()->isVisible());

        popup->accept();

        QVERIFY(applied);
        QCOMPARE(applied->changes.size(), 1);
        QCOMPARE(std::get<DocumentEdit>(applied->changes[0]).edits.size(), 2); // one edit of the file
    }

    void updatesTheIncludesOfAMovedHeader()
    {
        const Project project;
        write(project.path("src/main.cpp"), "#include \"util/total.h\"\n#include <vector>\n");
        write(project.path("src/util/total.cpp"), "#include \"total.h\"\n#include \"helper.h\"\n");
        write(project.path("src/util/helper.h"), "#pragma once\n");
        write(project.path("src/math/total.h"), "#pragma once\n#include \"helper.h\"\n"); // moved from util/
        OccurrenceStore index;
        index.setFiles({{project.path("src/main.cpp"), {1, 1}, {{"total", 10, 0, OccurrenceRole::Import}}},
                        {project.path("src/util/total.cpp"), {1, 1}, {{"total", 10, 0, OccurrenceRole::Import}}}});

        const WorkspaceEdit edit =
            FileReferences::update({.moves = {{project.path("src/util/total.h"), project.path("src/math/total.h")}},
                                    .projectFolder = project.dir.path(),
                                    .index = &index,
                                    .unsavedTexts = {}});

        QCOMPARE(edit.changes.size(), 3);
        const auto editOf = [&edit](const QString &path) {
            for (const DocumentChange &change : edit.changes) {
                if (std::get<DocumentEdit>(change).filePath == path)
                    return std::get<DocumentEdit>(change).edits;
            }
            return QList<TextEdit>();
        };
        QCOMPARE(editOf(project.path("src/main.cpp")), (QList<TextEdit>{replacing(0, 10, 22, "math/total.h")}));
        QCOMPARE(editOf(project.path("src/util/total.cpp")),
                 (QList<TextEdit>{replacing(0, 10, 17, "../math/total.h")}));
        // The header's own include, from its new folder.
        QCOMPARE(editOf(project.path("src/math/total.h")), (QList<TextEdit>{replacing(1, 10, 18, "../util/helper.h")}));
    }

    void updatesTheRelativeImportsOfAMovedScript()
    {
        const Project project;
        write(project.path("lib/total.ts"), "export const total = 1;\n"); // moved from the root
        const QString importer = project.path("main.ts");

        const WorkspaceEdit edit = FileReferences::update(
            {.moves = {{project.path("total.ts"), project.path("lib/total.ts")}},
             .projectFolder = project.dir.path(),
             .index = nullptr,
             .unsavedTexts = {{importer, "import { total } from './total';\nconst t = require(\"./total.ts\");\n"}}});

        QCOMPARE(edit.changes.size(), 1);
        QCOMPARE(std::get<DocumentEdit>(edit.changes[0]).edits,
                 (QList<TextEdit>{replacing(0, 23, 30, "./lib/total"), replacing(1, 19, 29, "./lib/total.ts")}));
    }

    void updatesThePythonImportsOfAMovedModule()
    {
        const Project project;
        write(project.path("pkg/sub/mod.py"), "def f(): pass\n"); // moved from pkg/
        const QString importer = project.path("app.py");

        const WorkspaceEdit edit =
            FileReferences::update({.moves = {{project.path("pkg/mod.py"), project.path("pkg/sub/mod.py")}},
                                    .projectFolder = project.dir.path(),
                                    .index = nullptr,
                                    .unsavedTexts = {{importer, "from pkg.mod import f\nimport os, pkg.mod as m\n"}}});

        QCOMPARE(edit.changes.size(), 1);
        QCOMPARE(std::get<DocumentEdit>(edit.changes[0]).edits,
                 (QList<TextEdit>{replacing(0, 5, 12, "pkg.sub.mod"), replacing(1, 11, 18, "pkg.sub.mod")}));
    }

    void listsTheFilesOfAMovedFolder()
    {
        const Project project;
        write(project.path("to/a.h"), "");
        write(project.path("to/deep/b.h"), "");

        QList<FileReferences::Move> moves = FileReferences::filesOf(project.path("from"), project.path("to"));
        std::ranges::sort(moves, [](const auto &a, const auto &b) { return a.to < b.to; });

        QCOMPARE(moves, (QList<FileReferences::Move>{{project.path("from/a.h"), project.path("to/a.h")},
                                                     {project.path("from/deep/b.h"), project.path("to/deep/b.h")}}));
    }

    void movesACppClassAndItsMembersToTheirOwnFiles()
    {
        Project project;
        const QByteArray header = "// SPDX-License-Identifier: GPL-3.0-only\n"
                                  "\n"
                                  "#pragma once\n"
                                  "\n"
                                  "#include <QString>\n"
                                  "\n"
                                  "namespace app {\n"
                                  "\n"
                                  "// A square.\n"
                                  "class Square {\n"
                                  "public:\n"
                                  "    int side() const;\n"
                                  "};\n"
                                  "\n"
                                  "class Circle {\n"
                                  "};\n"
                                  "\n"
                                  "} // namespace app\n";
        const QByteArray source = "// SPDX-License-Identifier: GPL-3.0-only\n"
                                  "\n"
                                  "#include \"shapes.h\"\n"
                                  "\n"
                                  "namespace app {\n"
                                  "\n"
                                  "int Square::side() const\n"
                                  "{\n"
                                  "    return 1;\n"
                                  "}\n"
                                  "\n"
                                  "int other() { return 2; }\n"
                                  "\n"
                                  "} // namespace app\n";
        write(project.path("shapes.h"), header);
        write(project.path("shapes.cpp"), source);

        const MoveClass::Result result = MoveClass::plan({.path = project.path("shapes.h"),
                                                          .languageId = "cpp",
                                                          .text = header,
                                                          .cursor = {10, 4},
                                                          .sourcePath = project.path("shapes.cpp"),
                                                          .sourceText = source});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(result.edit, project.targets());
        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));

        QCOMPARE(result.className, QString("Square"));
        QCOMPARE(result.newPath, project.path("Square.h"));
        QCOMPARE(read(project.path("Square.h")), QByteArray("// SPDX-License-Identifier: GPL-3.0-only\n\n"
                                                            "#pragma once\n\n"
                                                            "#include <QString>\n\n"
                                                            "namespace app {\n\n"
                                                            "// A square.\n"
                                                            "class Square {\n"
                                                            "public:\n"
                                                            "    int side() const;\n"
                                                            "};\n\n"
                                                            "} // namespace app\n"));
        QCOMPARE(read(project.path("shapes.h")), QByteArray("// SPDX-License-Identifier: GPL-3.0-only\n\n"
                                                            "#pragma once\n\n"
                                                            "#include <QString>\n"
                                                            "#include \"Square.h\"\n\n"
                                                            "namespace app {\n\n"
                                                            "class Circle {\n"
                                                            "};\n\n"
                                                            "} // namespace app\n"));
        QCOMPARE(read(project.path("Square.cpp")), QByteArray("// SPDX-License-Identifier: GPL-3.0-only\n\n"
                                                              "#include \"Square.h\"\n"
                                                              "#include \"shapes.h\"\n\n"
                                                              "namespace app {\n\n"
                                                              "int Square::side() const\n"
                                                              "{\n"
                                                              "    return 1;\n"
                                                              "}\n\n"
                                                              "} // namespace app\n"));
        QVERIFY(!read(project.path("shapes.cpp")).contains("Square"));
    }

    void movesAClassOfAGuardedHeader()
    {
        const QByteArray header = "#ifndef SHAPES_H\n#define SHAPES_H\n\nstruct A {};\n\nstruct B {};\n\n#endif\n";

        const MoveClass::Result result =
            MoveClass::plan({.path = "/p/shapes.h", .languageId = "cpp", .text = header, .cursor = {5, 8}});

        QCOMPARE(result.className, QString("B"));
        const auto &created = std::get<DocumentEdit>(result.edit.changes[1]);
        QCOMPARE(created.edits.first().newText, QString("#ifndef B_H\n#define B_H\n\nstruct B {};\n\n#endif\n"));
        const auto &original = std::get<DocumentEdit>(result.edit.changes[2]);
        QCOMPARE(original.edits.first().newText, QString("#include \"B.h\"\n")); // after the #define
        QCOMPARE(original.edits.first().range.start.line, 2);
    }

    void movesAJavaClassWithItsPackageAndImports()
    {
        const QByteArray text = "package p;\n\nimport java.util.List;\n\npublic class A {}\n\n"
                                "/** Doc. */\n@Deprecated\nclass B extends A {}\n";

        const MoveClass::Result result =
            MoveClass::plan({.path = "/p/A.java", .languageId = "java", .text = text, .cursor = {8, 7}});

        QCOMPARE(result.newPath, QString("/p/B.java"));
        QCOMPARE(std::get<DocumentEdit>(result.edit.changes[1]).edits.first().newText,
                 QString("package p;\nimport java.util.List;\n\n/** Doc. */\n@Deprecated\nclass B extends A {}\n"));
        const MoveClass::Result publicOne =
            MoveClass::plan({.path = "/p/A.java", .languageId = "java", .text = text, .cursor = {4, 15}});
        QCOMPARE(publicOne.error, QString("“A” is the public class of its file"));
    }

    void leavesAClassAloneInItsFile()
    {
        const MoveClass::Result result =
            MoveClass::plan({.path = "/p/a.h", .languageId = "cpp", .text = "class A {};\n", .cursor = {0, 7}});

        QCOMPARE(result.error, QString("“A” is already alone in its file"));
        QVERIFY(result.edit.isEmpty());
    }
};

QTEST_MAIN(TestRefactor)
#include "tst_refactor.moc"
