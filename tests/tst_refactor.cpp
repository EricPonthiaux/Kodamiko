// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "navigation/WorkspaceEditing.h"
#include "refactor/EditJournal.h"
#include "refactor/RefactoringHistory.h"
#include "ui/Theme.h"

#include <QFile>
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
};

QTEST_MAIN(TestRefactor)
#include "tst_refactor.moc"
