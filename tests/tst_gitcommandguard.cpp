// SPDX-License-Identifier: GPL-3.0-only

// The git commands Claude runs in its shell, sent to Kodamiko's git tools.

#include "agent/vendor/claude/GitCommandGuard.h"

#include <QtTest>

using GitCommandGuard::Location;
using GitCommandGuard::refusal;

namespace {

const Location project{.projectFolder = QStringLiteral("/work/kodamiko"),
                       .workingDirectory = QStringLiteral("/work/kodamiko")};

// The refusal of `command` in the project, empty when it is allowed.
QString refusalOf(const QString &command, const Location &location = project)
{
    return refusal(command, location).value_or(QString());
}

} // namespace

class TestGitCommandGuard : public QObject {
    Q_OBJECT

private slots:

    void givesTheToolCallThatStagesTheFiles()
    {
        const QString reason = refusalOf(QStringLiteral("git add src/a.cpp 'b c.h'"));

        QVERIFY(reason.contains(QStringLiteral("mcp__kodamiko__stage_files {\"paths\":[\"src/a.cpp\",\"b c.h\"]}")));
    }

    void explainsWhyTheToolsComeFirst()
    {
        const QString reason = refusalOf(QStringLiteral("git add a"));

        QVERIFY(reason.contains(QStringLiteral("asks the user")));
        QVERIFY(reason.contains(QStringLiteral("snapshot")));
        QVERIFY(reason.contains(QStringLiteral("Nothing of this command line ran")));
    }

    void readsTheCommitMessageOfAHeredoc()
    {
        const QString reason = refusalOf(
            QStringLiteral("git commit -m \"$(cat <<'EOF'\nFix the parser\n\nIt lost the last line.\nEOF\n)\""));

        QVERIFY(reason.contains(
            QStringLiteral("mcp__kodamiko__commit {\"message\":\"Fix the parser\\n\\nIt lost the last line.\"}")));
    }

    void stagesThenCommitsForCommitAll()
    {
        const QString reason = refusalOf(QStringLiteral("git commit -am 'Tidy' --amend"));

        QVERIFY(reason.contains(QStringLiteral("mcp__kodamiko__stage_files")));
        QVERIFY(reason.contains(QStringLiteral("then mcp__kodamiko__commit {\"amend\":true,\"message\":\"Tidy\"}")));
    }

    void findsGitInEveryPartOfACompoundCommand()
    {
        const QString reason =
            refusalOf(QStringLiteral("npm test 2>&1 | tail -5 && git add -A && git push -u origin HEAD"));

        QVERIFY(reason.contains(QStringLiteral("mcp__kodamiko__stage_files {\"paths\":[\".\"]}")));
        QVERIFY(reason.contains(QStringLiteral("mcp__kodamiko__push {}")));
    }

    void translatesBranchesStashesAndOperations()
    {
        QVERIFY(refusalOf(QStringLiteral("git checkout -b feature/x"))
                    .contains(QStringLiteral("switch_branch {\"create\":true,\"name\":\"feature/x\"}")));
        QVERIFY(refusalOf(QStringLiteral("git branch -D old"))
                    .contains(QStringLiteral("delete_branch {\"force\":true,\"name\":\"old\"}")));
        QVERIFY(
            refusalOf(QStringLiteral("git stash pop stash@{2}")).contains(QStringLiteral("apply_stash {\"index\":2}")));
        QVERIFY(refusalOf(QStringLiteral("git rebase --continue")).contains(QStringLiteral("continue_operation")));
        QVERIFY(refusalOf(QStringLiteral("git restore --staged a.txt"))
                    .contains(QStringLiteral("stage_files {\"paths\":[\"a.txt\"],\"unstage\":true}")));
        QVERIFY(refusalOf(QStringLiteral("git checkout HEAD -- a.txt"))
                    .contains(QStringLiteral("discard_changes {\"paths\":[\"a.txt\"],\"staged\":true}")));
        QVERIFY(refusalOf(QStringLiteral("git cherry-pick abc123"))
                    .contains(QStringLiteral("cherry_pick {\"id\":\"abc123\"}")));
    }

    void letsReadingGitRun()
    {
        QVERIFY(!refusal(QStringLiteral("git status && git log --oneline -5 && git diff --cached"), project));
        QVERIFY(!refusal(QStringLiteral("git branch -a"), project));
        QVERIFY(!refusal(QStringLiteral("git stash list"), project));
    }

    void letsWhatNoToolDoesRun()
    {
        QVERIFY(!refusal(QStringLiteral("git push --force-with-lease"), project));
        QVERIFY(!refusal(QStringLiteral("git reset --hard HEAD~1"), project));
        QVERIFY(!refusal(QStringLiteral("git reset HEAD~1"), project));
        QVERIFY(!refusal(QStringLiteral("git add -p"), project));
        QVERIFY(!refusal(QStringLiteral("git rebase -i main"), project));
        QVERIFY(!refusal(QStringLiteral("git tag v1.0"), project));
    }

    void letsGitInAnotherRepositoryRun()
    {
        QVERIFY(!refusal(QStringLiteral("cd /tmp/other && git commit -m x"), project));
        QVERIFY(!refusal(QStringLiteral("git -C /tmp/other add ."), project));
        QVERIFY(!refusal(QStringLiteral("cd ~/elsewhere; git add ."), project));
        QVERIFY(!refusal(QStringLiteral("git add ."), {.projectFolder = QStringLiteral("/work/kodamiko"),
                                                       .workingDirectory = QStringLiteral("/work/other")}));
    }

    void followsADirectoryChangeInsideTheProject()
    {
        QVERIFY(refusal(QStringLiteral("cd src && git add a.cpp"), project));
        QVERIFY(
            refusal(QStringLiteral("git -C /work/kodamiko/src add a.cpp"),
                    {.projectFolder = QStringLiteral("/work/kodamiko"), .workingDirectory = QStringLiteral("/tmp")}));
    }

    void ignoresGitWrittenInsideAnotherCommand()
    {
        QVERIFY(!refusal(QStringLiteral("echo \"git add a\" && grep -r 'git commit' docs"), project));
    }
};

QTEST_MAIN(TestGitCommandGuard)
#include "tst_gitcommandguard.moc"
