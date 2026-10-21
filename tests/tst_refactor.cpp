// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "navigation/WorkspaceEditing.h"
#include "refactor/EditJournal.h"
#include "refactor/EditPreview.h"
#include "refactor/RefactoringHistory.h"
#include "refactor/RefactoringPreviewPopup.h"
#include "refactor/RenameSymbolPopup.h"
#include "ui/Theme.h"

#include <QFile>
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
        QList<RenameSymbolPopup::Mode> modes;
        auto *popup = new RenameSymbolPopup(
            "total", "a.cpp",
            [&modes](const QString &newName, RenameSymbolPopup::Mode mode) {
                if (newName == "total")
                    return QString("Same name");
                modes.append(mode);
                return QString();
            },
            &window);
        popup->popup();
        QCOMPARE(popup->field()->text(), QString("total"));

        popup->preview();
        QCOMPARE(popup->error(), QString("Same name")); // still open
        popup->field()->setText("sum");
        popup->preview();

        QCOMPARE(modes, QList<RenameSymbolPopup::Mode>{RenameSymbolPopup::Mode::Preview});
    }
};

QTEST_MAIN(TestRefactor)
#include "tst_refactor.moc"
