// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "index/OccurrenceStore.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "navigation/WorkspaceEditing.h"
#include "refactor/ChangeSignature.h"
#include "refactor/ChangeSignaturePopup.h"
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
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

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

// The places of `name` in `text` (of `path`), each occurrence of the
// whole word.
QList<Location> placesOf(const QString &path, const QString &text, const QString &name)
{
    QList<Location> places;
    const QRegularExpression word(QStringLiteral("\\b%1\\b").arg(name));
    QRegularExpressionMatchIterator it = word.globalMatch(text);
    while (it.hasNext()) {
        const qsizetype at = it.next().capturedStart();
        const int line = int(text.left(at).count('\n'));
        const int column = int(at - (text.lastIndexOf('\n', at - 1) + 1));
        places.append({path, {{line, column}, {line, column + int(name.size())}}});
    }
    return places;
}

// `texts` with `edit` applied, by path.
QHash<QString, QString> applied(QHash<QString, QString> texts, const WorkspaceEdit &edit)
{
    for (const DocumentChange &change : edit.changes) {
        const auto &documentEdit = std::get<DocumentEdit>(change);
        texts[documentEdit.filePath] = applyEdits(texts.value(documentEdit.filePath), documentEdit.edits);
    }
    return texts;
}

using ChangeSignature::Parameter;

// A project folder with its editors and (idle) language servers.
struct Project {
    QTemporaryDir dir;
    EditorTabs tabs{Theme::kodamiko()};
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

    void changesTheOpenFilesAtOnceAndTheClosedOnesInTheBackground()
    {
        Project project;
        write(project.path("open.h"), "int total;\n");
        write(project.path("closed.h"), "int total;\n");
        const auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("open.h")));
        QVERIFY(editor);
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("open.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("closed.h"), {}, {replacing(0, 4, 9, "sum")}},
        }};
        std::optional<WorkspaceEditing::Outcome> outcome;

        WorkspaceEditing::applyInBackground(
            edit, project.targets(), &project.tabs,
            [&outcome](const WorkspaceEditing::Outcome &applied) { outcome = applied; });

        QCOMPARE(editor->text(), QString("int sum;\n"));
        QTRY_VERIFY(outcome);
        QVERIFY2(outcome->errors.isEmpty(), qPrintable(outcome->errors.join('\n')));
        QCOMPARE(outcome->filesChanged, 2);
        QCOMPARE(read(project.path("closed.h")), QByteArray("int sum;\n"));
    }

    void undoesTheOpenFilesWhenAClosedOneCannotBeChangedInTheBackground()
    {
        Project project;
        write(project.path("open.h"), "int total;\n");
        const auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.path("open.h")));
        QVERIFY(editor);
        const WorkspaceEdit edit{{
            DocumentEdit{project.path("open.h"), {}, {replacing(0, 4, 9, "sum")}},
            DocumentEdit{project.path("missing.h"), {}, {replacing(0, 4, 9, "sum")}},
        }};
        std::optional<WorkspaceEditing::Outcome> outcome;

        WorkspaceEditing::applyInBackground(
            edit, project.targets(), &project.tabs,
            [&outcome](const WorkspaceEditing::Outcome &applied) { outcome = applied; });

        QTRY_VERIFY(outcome);
        QVERIFY(!outcome->errors.isEmpty());
        QCOMPARE(outcome->filesChanged, 0);
        QCOMPARE(editor->text(), QString("int total;\n"));
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

    void findsTheNameInTheCommentsAndStringsOfTheServersFilesOutsideItsEdit()
    {
        const Project project;
        write(project.path("a.cpp"), "// total of all\nint total = 0; // é total\nconst char *s = \"total\";\n");
        // The server renames the variable on line 1.
        const WorkspaceEdit codeEdit{{DocumentEdit{project.path("a.cpp"), {}, {replacing(1, 4, 9, "sum")}}}};

        const RenameAnalysis::Result result = RenameAnalysis::analyze({.name = "total",
                                                                       .newName = "sum",
                                                                       .codeEdit = codeEdit,
                                                                       .unsavedTexts = {},
                                                                       .projectFolder = project.dir.path(),
                                                                       .searchesCommentsAndStrings = true});

        QCOMPARE(result.textualEdit.changes.size(), 1);
        const auto &inSource = std::get<DocumentEdit>(result.textualEdit.changes[0]);
        QCOMPARE(inSource.filePath, project.path("a.cpp"));
        QCOMPARE(inSource.edits, (QList<TextEdit>{replacing(0, 3, 8, "sum"), replacing(1, 20, 25, "sum"),
                                                  replacing(2, 17, 22, "sum")})); // "é" is one UTF-16 unit
        QVERIFY(result.conflicts.isEmpty());
    }

    void leavesTheNameInFilesTheServerDoesNotChange()
    {
        const Project project;
        write(project.path("a.cpp"), "int total = 0;\n");
        write(project.path("other.cpp"), "// total of another\n");
        write(project.path("style.css"), ".total { color: red; }\n");
        write(project.path("notes.md"), "The total is kept.\n");
        const WorkspaceEdit codeEdit{{DocumentEdit{project.path("a.cpp"), {}, {replacing(0, 4, 9, "sum")}}}};

        const RenameAnalysis::Result result = RenameAnalysis::analyze({.name = "total",
                                                                       .newName = "sum",
                                                                       .codeEdit = codeEdit,
                                                                       .unsavedTexts = {},
                                                                       .projectFolder = project.dir.path(),
                                                                       .searchesCommentsAndStrings = true});

        QVERIFY(result.textualEdit.isEmpty());
    }

    void findsTheNameInAnUnsavedText()
    {
        const Project project;
        write(project.path("a.cpp"), "int total;\n");
        const WorkspaceEdit codeEdit{{DocumentEdit{project.path("a.cpp"), {}, {replacing(1, 4, 9, "sum")}}}};

        const RenameAnalysis::Result result =
            RenameAnalysis::analyze({.name = "total",
                                     .newName = "sum",
                                     .codeEdit = codeEdit,
                                     .unsavedTexts = {{project.path("a.cpp"), "/* total */\nint total;\n"}},
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

        // An absolute path on every system: "C:/p/A.java" on Windows.
        const QString path = QDir::rootPath() + "p/A.java";
        const MoveClass::Result result =
            MoveClass::plan({.path = path, .languageId = "java", .text = text, .cursor = {8, 7}});

        QCOMPARE(result.newPath, QDir::rootPath() + "p/B.java");
        QCOMPARE(std::get<DocumentEdit>(result.edit.changes[1]).edits.first().newText,
                 QString("package p;\nimport java.util.List;\n\n/** Doc. */\n@Deprecated\nclass B extends A {}\n"));
        const MoveClass::Result publicOne =
            MoveClass::plan({.path = path, .languageId = "java", .text = text, .cursor = {4, 15}});
        QCOMPARE(publicOne.error, QString("“A” is the public class of its file"));
    }

    void leavesAClassAloneInItsFile()
    {
        const MoveClass::Result result =
            MoveClass::plan({.path = "/p/a.h", .languageId = "cpp", .text = "class A {};\n", .cursor = {0, 7}});

        QCOMPARE(result.error, QString("“A” is already alone in its file"));
        QVERIFY(result.edit.isEmpty());
    }

    void readsTheSignatureAtTheCursor_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("names");
        QTest::newRow("cpp") << "cpp" << "int §total(int a, const char *b = \"x\");" << QStringList{"a", "b"};
        QTest::newRow("c-definition") << "c" << "static int §f(void *p, size_t n) { return 0; }"
                                      << QStringList{"p", "n"};
        QTest::newRow("java") << "java" << "class A { int §f(int a, String b) { return a; } }" << QStringList{"a", "b"};
        QTest::newRow("javascript") << "javascript" << "function §f(a, b = 2, ...rest) {}"
                                    << QStringList{"a", "b", "rest"};
        QTest::newRow("typescript") << "typescript" << "function §f(a: number, b?: string): void {}"
                                    << QStringList{"a", "b"};
        QTest::newRow("ts-method") << "typescript" << "class C { §m(x: number) {} }" << QStringList{"x"};
        QTest::newRow("python-method") << "python" << "class C:\n    def §m(self, a, b=1, *args, **kw):\n        pass\n"
                                       << QStringList{"a", "b", "args", "kw"};
    }

    void readsTheSignatureAtTheCursor()
    {
        QFETCH(const QString, language);
        QFETCH(const QString, text);
        QFETCH(const QStringList, names);
        const qsizetype at = text.indexOf(QStringLiteral("§"));
        const Position cursor{int(text.left(at).count('\n')), int(at - (text.lastIndexOf('\n', at - 1) + 1))};

        const ChangeSignature::Signature signature =
            ChangeSignature::read(language, QString(text).remove(QStringLiteral("§")), cursor);

        QVERIFY2(signature.error.isEmpty(), qPrintable(signature.error));
        QCOMPARE(signature.names, names);
    }

    void reordersAddsAndRemovesInDeclarationsAndCalls()
    {
        const QString header = "int total(int a, int b);\n";
        const QString source = "int total(int a, int b) { return a + b; }\nint x = total(1, 2) + total(3, 4);\n";
        const QHash<QString, QString> texts{{"/p/total.h", header}, {"/p/total.cpp", source}};
        const ChangeSignature::Input input{
            .parameters = {Parameter{.oldIndex = 1}, Parameter{.oldIndex = 0},
                           Parameter{.declaration = "int scale", .name = "scale", .valueAtCalls = "10"}},
            .oldNames = {"a", "b"},
            .places = placesOf("/p/total.h", header, "total") + placesOf("/p/total.cpp", source, "total"),
            .texts = texts};

        const ChangeSignature::Result result = ChangeSignature::plan(input);

        QVERIFY(result.conflicts.isEmpty());
        const QHash<QString, QString> after = applied(texts, result.edit);
        QCOMPARE(after.value("/p/total.h"), QString("int total(int b, int a, int scale);\n"));
        QCOMPARE(after.value("/p/total.cpp"), QString("int total(int b, int a, int scale) { return a + b; }\n"
                                                      "int x = total(2, 1, 10) + total(4, 3, 10);\n"));
    }

    void removesAParameterOfAScriptFunctionAndMethodCalls()
    {
        const QString text = "class C {\n  m(a, b) {}\n}\nnew C().m(1, 2);\nconst c = new C();\nc.m(f(3), [4, 5]);\n";
        const ChangeSignature::Input input{.parameters = {Parameter{.oldIndex = 1}},
                                           .oldNames = {"a", "b"},
                                           .places = placesOf("/p/a.ts", text, "m"),
                                           .texts = {{"/p/a.ts", text}}};

        const ChangeSignature::Result result = ChangeSignature::plan(input);

        QCOMPARE(applied(input.texts, result.edit).value("/p/a.ts"),
                 QString("class C {\n  m(b) {}\n}\nnew C().m(2);\nconst c = new C();\nc.m([4, 5]);\n"));
    }

    void keepsPythonsSelfAndKeywordArguments()
    {
        const QString text = "class C:\n    def m(self, a, b=1):\n        pass\n\n"
                             "C().m(1)\nC().m(1, 2)\nC().m(1, b=3)\nC().m(a=4)\n";
        const ChangeSignature::Input input{
            .parameters = {Parameter{.oldIndex = 1}, Parameter{.oldIndex = 0},
                           Parameter{.declaration = "c=0", .name = "c", .valueAtCalls = "9"}},
            .oldNames = {"a", "b"},
            .places = placesOf("/p/c.py", text, "m"),
            .texts = {{"/p/c.py", text}}};

        const ChangeSignature::Result result = ChangeSignature::plan(input);

        QCOMPARE(result.conflicts, QStringList{"c.py:2: “a” comes after a parameter with a default value"});
        QCOMPARE(applied(input.texts, result.edit).value("/p/c.py"),
                 QString("class C:\n    def m(self, b=1, a, c=0):\n        pass\n\n"
                         "C().m(a=1, c=9)\nC().m(2, 1, 9)\nC().m(b=3, a=1, c=9)\nC().m(a=4, c=9)\n"));
    }

    void leavesTheCallsItCannotChange()
    {
        const QString text = "int f(int a, int b = 2) { return a; }\nint x = f(1);\nint (*p)(int, int) = f;\n"
                             "int y = f(f(1, 2), 3);\n";
        const ChangeSignature::Input input{.parameters = {Parameter{.oldIndex = 1}, Parameter{.oldIndex = 0}},
                                           .oldNames = {"a", "b"},
                                           .places = placesOf("/p/f.cpp", text, "f"),
                                           .texts = {{"/p/f.cpp", text}}};

        const ChangeSignature::Result result = ChangeSignature::plan(input);

        QCOMPARE(result.conflicts,
                 (QStringList{"f.cpp:1: “int a” comes after a parameter with a default value",
                              "f.cpp:2: the call leaves out “b”, which now comes before an argument; left as it is",
                              "f.cpp:3: the function is used without being called there; left as it is",
                              "f.cpp:4: a call inside another's arguments; left as it is"}));
        QCOMPARE(applied(input.texts, result.edit).value("/p/f.cpp"),
                 QString("int f(int b = 2, int a) { return a; }\nint x = f(1);\nint (*p)(int, int) = f;\n"
                         "int y = f(3, f(1, 2));\n"));
    }

    void changeSignaturePopupReordersAddsAndRemoves()
    {
        QWidget window;
        window.show();
        std::optional<QList<Parameter>> chosen;
        auto *popup = new ChangeSignaturePopup(
            "cpp", {"total", {"int a", "int b", "int c"}, {"a", "b", "c"}, {}},
            [&chosen](const QList<Parameter> &parameters) { chosen = parameters; }, &window);
        popup->popup();
        QCOMPARE(popup->table()->rowCount(), 3);

        popup->table()->selectRow(1);
        popup->moveUp(); // b, a, c
        popup->table()->selectRow(2);
        popup->removeParameter(); // b, a
        popup->addParameter();    // after a
        popup->accept();
        QVERIFY(!chosen);
        QVERIFY(popup->error()->isVisibleTo(popup)); // the new one has no declaration
        popup->table()->item(2, 0)->setText("int scale = 1");
        popup->table()->item(2, 1)->setText("10");
        popup->accept();

        QVERIFY(chosen);
        QCOMPARE(chosen->size(), 3);
        QCOMPARE((*chosen)[0].oldIndex, std::optional<int>(1));
        QCOMPARE((*chosen)[1].oldIndex, std::optional<int>(0));
        QCOMPARE((*chosen)[2].declaration, QString("int scale = 1"));
        QCOMPARE((*chosen)[2].name, QString("scale"));
        QCOMPARE((*chosen)[2].valueAtCalls, QString("10"));
    }

    void namesTheParameterOfADeclaration_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("declaration");
        QTest::addColumn<QString>("name");
        QTest::newRow("cpp") << "cpp" << "const std::string &label = {}" << "label";
        QTest::newRow("c") << "c" << "char *buffer" << "buffer";
        QTest::newRow("java") << "java" << "List<String> items" << "items";
        QTest::newRow("typescript") << "typescript" << "scale?: number" << "scale";
        QTest::newRow("javascript") << "javascript" << "options = {}" << "options";
        QTest::newRow("python") << "python" << "verbose: bool = False" << "verbose";
    }

    void namesTheParameterOfADeclaration()
    {
        QFETCH(const QString, language);
        QFETCH(const QString, declaration);
        QFETCH(const QString, name);

        QCOMPARE(ChangeSignature::nameOf(language, declaration), name);
    }
};

QTEST_MAIN(TestRefactor)
#include "tst_refactor.moc"
