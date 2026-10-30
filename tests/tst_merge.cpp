// SPDX-License-Identifier: GPL-3.0-only

#include "git/ConflictVersions.h"
#include "git/GitProcess.h"
#include "git/Integration.h"
#include "git/RepositoryStatus.h"
#include "git/ThreeWayMerge.h"
#include "merge/MergeDocument.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

using ThreeWayMerge::Chunk;
using ThreeWayMerge::ChunkKind;
using Choice = MergeDocument::Choice;

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray read(const QTemporaryDir &dir, const QString &path)
{
    QFile file(dir.filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray git(const QTemporaryDir &dir, const QStringList &arguments)
{
    return Git::run(dir.path(), arguments);
}

// A repository on main whose first commit has `files`, and a branch
// "other" from it.
void initRepository(const QTemporaryDir &dir, const QHash<QString, QByteArray> &files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        write(dir, it.key(), it.value());
    git(dir, {"init", "-q", "-b", "main"});
    git(dir, {"config", "core.autocrlf", "false"});
    git(dir, {"add", "-A"});
    git(dir, {"commit", "-qm", "init"});
    git(dir, {"branch", "other"});
}

void commitOn(const QTemporaryDir &dir, const QString &branch, const std::function<void()> &change)
{
    git(dir, {"switch", "-q", branch});
    change();
    git(dir, {"add", "-A"});
    git(dir, {"commit", "-qm", "on " + branch});
}

// Merges "other" into main, which stops on conflicts.
void mergeOther(const QTemporaryDir &dir)
{
    git(dir, {"switch", "-q", "main"});
    QVERIFY_THROWS_EXCEPTION(GitError, git(dir, {"merge", "-q", "other"}));
}

RepositoryInfo infoOf(const QTemporaryDir &dir)
{
    const std::optional<RepositoryInfo> info = RepositoryInfo::read(dir.path());
    if (!info)
        qFatal("no git repository");
    return *info;
}

QList<ChunkKind> kindsOf(const QList<Chunk> &chunks)
{
    QList<ChunkKind> kinds;
    for (const Chunk &chunk : chunks)
        kinds.append(chunk.kind);
    return kinds;
}

// The text the chunks give when every conflict takes `side`.
QByteArray resolvedWith(const QList<Chunk> &chunks, QByteArrayList Chunk::*side)
{
    QByteArray text;
    for (const Chunk &chunk : chunks) {
        switch (chunk.kind) {
        case ChunkKind::Unchanged:
        case ChunkKind::Current:
        case ChunkKind::BothSame:
            text += chunk.current.join();
            break;
        case ChunkKind::Incoming:
            text += chunk.incoming.join();
            break;
        case ChunkKind::Conflict:
            text += (chunk.*side).join();
            break;
        }
    }
    return text;
}

// What `git merge-file` gives for the same three versions, taking `option`
// ("--ours" or "--theirs") for every conflict.
QByteArray gitMergeFile(const QByteArray &base,
                        const QByteArray &current,
                        const QByteArray &incoming,
                        const QString &option)
{
    const QTemporaryDir dir;
    write(dir, "current", current);
    write(dir, "base", base);
    write(dir, "incoming", incoming);
    return git(dir, {"merge-file", "-p", option, "current", "base", "incoming"});
}

const ConflictVersions::File *fileAt(const QList<ConflictVersions::File> &files, const QString &path)
{
    const auto found = std::ranges::find(files, path, &ConflictVersions::File::path);
    return found == files.end() ? nullptr : &*found;
}

// A document with, in order: a line only main changed, a conflict, and a
// line only "other" changed (segments 1, 3 and 5).
MergeDocument threeChanges()
{
    return MergeDocument(
        ThreeWayMerge::merge("a\nb\nc\nd\ne\nf\ng\n", "a\nB\nc\nmain\ne\nf\ng\n", "a\nb\nc\nother\ne\nF\ng\n"));
}

} // namespace

class TestMerge : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        // Neither the user's configuration nor the system's.
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    // ThreeWayMerge

    void changesOfOneSideOnlyMergeWithoutConflict()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("a\nb\nc\nd\ne\n", "a\nB\nc\nd\ne\n", "a\nb\nc\nD\ne\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::Unchanged, ChunkKind::Current, ChunkKind::Unchanged,
                                         ChunkKind::Incoming, ChunkKind::Unchanged}));
        QCOMPARE(chunks[1].current, QByteArrayList{"B\n"});
        QCOMPARE(chunks[1].incoming, QByteArrayList{"b\n"});
        QCOMPARE(chunks[3].incoming, QByteArrayList{"D\n"});
    }

    void aLineBothSidesChangeDifferentlyIsAConflict()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("a\nb\nc\n", "a\nmain\nc\n", "a\nother\nc\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::Unchanged, ChunkKind::Conflict, ChunkKind::Unchanged}));
        QCOMPARE(chunks[1].base, QByteArrayList{"b\n"});
        QCOMPARE(chunks[1].current, QByteArrayList{"main\n"});
        QCOMPARE(chunks[1].incoming, QByteArrayList{"other\n"});
    }

    void theSameChangeOnBothSidesIsNoConflict()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("a\nb\nc\n", "a\nB\nc\n", "a\nB\nc\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::Unchanged, ChunkKind::BothSame, ChunkKind::Unchanged}));
    }

    void changesWithNoCommonLineBetweenThemConflict()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("a\nb\nc\nd\n", "a\nB\nc\nd\n", "a\nb\nC\nd\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::Unchanged, ChunkKind::Conflict, ChunkKind::Unchanged}));
        QCOMPARE(chunks[1].current, (QByteArrayList{"B\n", "c\n"}));
        QCOMPARE(chunks[1].incoming, (QByteArrayList{"b\n", "C\n"}));
    }

    void linesBothSidesAddAroundAConflictLeaveIt()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("a\nz\n", "a\nnew\nmain\nend\nz\n", "a\nnew\nother\nend\nz\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::Unchanged, ChunkKind::BothSame, ChunkKind::Conflict,
                                         ChunkKind::BothSame, ChunkKind::Unchanged}));
        QCOMPARE(chunks[2].current, QByteArrayList{"main\n"});
        QCOMPARE(chunks[2].incoming, QByteArrayList{"other\n"});
    }

    void aFileAddedOnBothSidesMergesWithoutAncestor()
    {
        const QList<Chunk> chunks = ThreeWayMerge::merge("", "[shop]\nlocale = fr\n", "[shop]\nlocale = en\n");

        QCOMPARE(kindsOf(chunks), (QList{ChunkKind::BothSame, ChunkKind::Conflict}));
        QCOMPARE(chunks[1].current, QByteArrayList{"locale = fr\n"});
    }

    void eachVersionComesBackWholeFromTheChunks()
    {
        const QByteArray base = "a\r\nb\r\nc";
        const QByteArray current = "a\r\nB\r\nc";
        const QByteArray incoming = "a\r\nb\r\nc\n";

        const QList<Chunk> chunks = ThreeWayMerge::merge(base, current, incoming);

        QByteArray baseBack;
        QByteArray currentBack;
        QByteArray incomingBack;
        for (const Chunk &chunk : chunks) {
            baseBack += chunk.base.join();
            currentBack += chunk.current.join();
            incomingBack += chunk.incoming.join();
        }
        QCOMPARE(baseBack, base);
        QCOMPARE(currentBack, current);
        QCOMPARE(incomingBack, incoming);
    }

    void takingOneSideOfEveryConflictGivesWhatGitGives()
    {
        const QByteArray base = "import math\n\nTAX = 0.20\n\n\ndef tax(p):\n    return round(p * TAX, 2)\n\n\n"
                                "def discount(p, d):\n    if d > 100:\n        raise ValueError\n    return p\n";
        const QByteArray current = "import math\nfrom decimal import Decimal\n\nTAX = 0.20\n\n\ndef tax(p):\n"
                                   "    return Decimal(p) * TAX\n\n\ndef discount(p, d):\n    if not 0 <= d <= 100:\n"
                                   "        raise ValueError(d)\n    return p\n\n\ndef short(p):\n    return p\n";
        const QByteArray incoming =
            "import math\n\nTAX = 0.21\n\n\ndef tax(p):\n    return round(p * TAX + 1e-9, 2)\n"
            "\n\ndef discount(p, d):\n    if d > 50:\n        raise ValueError\n    return p\n\n\n"
            "def per_unit(p, n):\n    return p / n\n";

        const QList<Chunk> chunks = ThreeWayMerge::merge(base, current, incoming);

        QCOMPARE(resolvedWith(chunks, &Chunk::current), gitMergeFile(base, current, incoming, "--ours"));
        QCOMPARE(resolvedWith(chunks, &Chunk::incoming), gitMergeFile(base, current, incoming, "--theirs"));
    }

    // ConflictVersions

    void readsTheThreeVersionsOfAFileBothSidesChanged()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"a.txt", "base\n"}});
        commitOn(dir, "other", [&dir] { write(dir, "a.txt", "other\n"); });
        commitOn(dir, "main", [&dir] { write(dir, "a.txt", "main\n"); });
        mergeOther(dir);

        const QList<ConflictVersions::File> files = ConflictVersions::read(infoOf(dir));

        QCOMPARE(files.size(), 1);
        const ConflictVersions::File &file = files.first();
        QCOMPARE(file.kind, ConflictVersions::Kind::BothModified);
        QCOMPARE(file.path, QString("a.txt"));
        QCOMPARE(file.base->content, QByteArray("base\n"));
        QCOMPARE(file.current->content, QByteArray("main\n"));
        QCOMPARE(file.incoming->content, QByteArray("other\n"));
        QVERIFY(!file.isBinary);
    }

    void tellsWhichSideDeletedAFile()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"gone-here.txt", "a\n"}, {"gone-there.txt", "a\n"}});
        commitOn(dir, "other", [&dir] {
            write(dir, "gone-here.txt", "changed\n");
            QFile::remove(dir.filePath("gone-there.txt"));
        });
        commitOn(dir, "main", [&dir] {
            QFile::remove(dir.filePath("gone-here.txt"));
            write(dir, "gone-there.txt", "changed\n");
        });
        mergeOther(dir);

        const QList<ConflictVersions::File> files = ConflictVersions::read(infoOf(dir));

        QCOMPARE(fileAt(files, "gone-here.txt")->kind, ConflictVersions::Kind::DeletedByCurrent);
        QVERIFY(!fileAt(files, "gone-here.txt")->current);
        QCOMPARE(fileAt(files, "gone-there.txt")->kind, ConflictVersions::Kind::DeletedByIncoming);
        QVERIFY(!fileAt(files, "gone-there.txt")->incoming);
    }

    void tellsAFileAddedOnBothSides()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"readme", "r\n"}});
        commitOn(dir, "other", [&dir] { write(dir, "new.toml", "other\n"); });
        commitOn(dir, "main", [&dir] { write(dir, "new.toml", "main\n"); });
        mergeOther(dir);

        const QList<ConflictVersions::File> files = ConflictVersions::read(infoOf(dir));

        QCOMPARE(fileAt(files, "new.toml")->kind, ConflictVersions::Kind::BothAdded);
        QVERIFY(!fileAt(files, "new.toml")->base);
    }

    void pairsAFileRenamedDifferentlyOnBothSides()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"utils.py", "def slugify(text):\n    return text\n"}});
        commitOn(dir, "other", [&dir] { git(dir, {"mv", "utils.py", "strings.py"}); });
        commitOn(dir, "main", [&dir] { git(dir, {"mv", "utils.py", "text.py"}); });
        mergeOther(dir);

        const QList<ConflictVersions::File> files = ConflictVersions::read(infoOf(dir));

        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().kind, ConflictVersions::Kind::RenamedOnBothSides);
        QCOMPARE(files.first().path, QString("utils.py"));
        QCOMPARE(files.first().current->path, QString("text.py"));
        QCOMPARE(files.first().incoming->path, QString("strings.py"));
    }

    void marksABinaryConflict()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"logo.png", QByteArray("\x89PNG\0base", 9)}});
        commitOn(dir, "other", [&dir] { write(dir, "logo.png", QByteArray("\x89PNG\0other", 10)); });
        commitOn(dir, "main", [&dir] { write(dir, "logo.png", QByteArray("\x89PNG\0main", 9)); });
        mergeOther(dir);

        const QList<ConflictVersions::File> files = ConflictVersions::read(infoOf(dir));

        QVERIFY(fileAt(files, "logo.png")->isBinary);
    }

    // Integration

    void writingAResolutionMarksTheFileResolved()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"a.txt", "base\n"}});
        commitOn(dir, "other", [&dir] { write(dir, "a.txt", "other\n"); });
        commitOn(dir, "main", [&dir] { write(dir, "a.txt", "main\n"); });
        mergeOther(dir);

        Integration::writeResolution(infoOf(dir), "a.txt", "main and other\n");

        QCOMPARE(read(dir, "a.txt"), QByteArray("main and other\n"));
        QVERIFY(ConflictVersions::read(infoOf(dir)).isEmpty());
        QCOMPARE(git(dir, {"show", ":a.txt"}), QByteArray("main and other\n"));
    }

    void namesTheBranchesOfAMerge()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"a.txt", "base\n"}});
        commitOn(dir, "other", [&dir] { write(dir, "a.txt", "other\n"); });
        commitOn(dir, "main", [&dir] { write(dir, "a.txt", "main\n"); });
        mergeOther(dir);

        const Integration::ConflictSides sides = Integration::conflictSides(infoOf(dir));

        QCOMPARE(sides.current, QString("main"));
        QCOMPARE(sides.incoming, QString("other"));
    }

    void namesTheCommitARebaseReplays()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"a.txt", "base\n"}});
        commitOn(dir, "main", [&dir] { write(dir, "a.txt", "main\n"); });
        git(dir, {"switch", "-q", "other"});
        write(dir, "a.txt", "other\n");
        git(dir, {"commit", "-qam", "Change a"});
        QVERIFY_THROWS_EXCEPTION(GitError, git(dir, {"rebase", "main"}));

        const Integration::ConflictSides sides = Integration::conflictSides(infoOf(dir));

        QCOMPARE(sides.current, QString("main"));
        QVERIFY2(sides.incoming.endsWith(" Change a"), qPrintable(sides.incoming));
    }

    // MergeDocument

    void aNewDocumentTakesEachSideChangesAndLeavesConflictsOpen()
    {
        const MergeDocument document = threeChanges();

        QCOMPARE(document.resultText(), QByteArray("a\nB\nc\nd\ne\nF\ng\n"));
        QVERIFY(!document.isResolved());
        QCOMPARE(document.unresolvedCount(), 1);
        QCOMPARE(document.segments()[3].choice, Choice::Unresolved);
    }

    void takingASideResolvesTheConflict()
    {
        MergeDocument document = threeChanges();

        document.choose(3, Choice::Incoming);

        QCOMPARE(document.resultText(), QByteArray("a\nB\nc\nother\ne\nF\ng\n"));
        QVERIFY(document.isResolved());
    }

    void takingBothSidesKeepsTheOrderChosen()
    {
        MergeDocument document = threeChanges();

        document.choose(3, Choice::IncomingThenCurrent);

        QCOMPARE(document.resultLines(3), (QByteArrayList{"other\n", "main\n"}));
        QCOMPARE(document.resultRange(5), (MergeDocument::LineRange{6, 1}));
    }

    void bothSidesOfTheLastLineStayOnTheirOwnLines()
    {
        MergeDocument document(ThreeWayMerge::merge("a\r\nb", "a\r\nmain", "a\r\nother"));

        document.choose(1, Choice::CurrentThenIncoming);

        QCOMPARE(document.resultText(), QByteArray("a\r\nmain\nother"));
    }

    void keepingTheCurrentSideOfAnIncomingChangeUndoesIt()
    {
        MergeDocument document = threeChanges();

        document.choose(5, Choice::Current);

        QCOMPARE(document.resultLines(5), QByteArrayList{"f\n"});
    }

    void typingInAConflictResolvesItWithTheTypedLines()
    {
        MergeDocument document = threeChanges();

        document.replaceResultLines(3, 1, {"main and other\n"});

        QCOMPARE(document.segments()[3].choice, Choice::Edited);
        QCOMPARE(document.resultText(), QByteArray("a\nB\nc\nmain and other\ne\nF\ng\n"));
        QVERIFY(document.isResolved());
    }

    void anEditAcrossStretchesJoinsThem()
    {
        MergeDocument document = threeChanges();

        document.replaceResultLines(1, 3, {"one line\n"});

        QCOMPARE(document.segments().size(), 5);
        const MergeDocument::Segment &joined = document.segments()[1];
        QCOMPARE(joined.chunk.kind, ChunkKind::Conflict);
        QCOMPARE(joined.chunk.current, (QByteArrayList{"B\n", "c\n", "main\n"}));
        QCOMPARE(document.resultText(), QByteArray("a\none line\ne\nF\ng\n"));
        QVERIFY(document.isResolved());
    }

    void typingWhereAnOpenConflictHasNoLinesGoesToIt()
    {
        MergeDocument document(ThreeWayMerge::merge("a\n", "a\nmain\n", "a\nother\n"));
        QCOMPARE(document.resultRange(1), (MergeDocument::LineRange{1, 0}));

        document.replaceResultLines(1, 0, {"typed\n"});

        QCOMPARE(document.segments().size(), 2);
        QCOMPARE(document.segments()[1].choice, Choice::Edited);
        QCOMPARE(document.resultText(), QByteArray("a\ntyped\n"));
    }

    void undoAndRedoAChoice()
    {
        MergeDocument document = threeChanges();
        document.choose(3, Choice::Current);

        document.undo();

        QCOMPARE(document.segments()[3].choice, Choice::Unresolved);
        QVERIFY(document.canRedo());
        document.redo();
        QCOMPARE(document.segments()[3].choice, Choice::Current);
        QVERIFY(!document.canRedo());
    }

    void aNewChangeAfterUndoForgetsTheRedo()
    {
        MergeDocument document = threeChanges();
        document.choose(3, Choice::Current);
        document.undo();

        document.choose(3, Choice::Incoming);

        QVERIFY(!document.canRedo());
        QVERIFY(document.canUndo());
    }

    void resolvingTheRestIsOneChange()
    {
        MergeDocument document(ThreeWayMerge::merge("a\nb\nc\nd\ne\n", "A\nb\nc\nd\nE\n", "1\nb\nc\nd\n5\n"));
        QCOMPARE(document.unresolvedCount(), 2);

        document.resolveRemaining(Choice::Incoming);

        QCOMPARE(document.resultText(), QByteArray("1\nb\nc\nd\n5\n"));
        document.undo();
        QCOMPARE(document.unresolvedCount(), 2);
        QVERIFY(!document.canUndo());
    }

private:
    QTemporaryFile config_;
};

QTEST_GUILESS_MAIN(TestMerge)
#include "tst_merge.moc"
