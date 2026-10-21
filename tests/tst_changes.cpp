// SPDX-License-Identifier: GPL-3.0-only

#include "changes/BranchesPopup.h"
#include "changes/ChangeTree.h"
#include "changes/ChangesView.h"
#include "changes/CommitPanel.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "changes/GitActions.h"
#include "changes/HunkActions.h"
#include "changes/NewBranchPopup.h"
#include "changes/SideBySide.h"
#include "changes/SideBySideDivider.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "git/Stashes.h"
#include "ui/FoldedPane.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/ViewHeader.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtTest>

namespace {

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// The Commit button, among the view's others.
QPushButton *commitButton(const QWidget &view)
{
    for (QPushButton *button : view.findChildren<QPushButton *>()) {
        if (button->text() == QLatin1String("Commit"))
            return button;
    }
    return nullptr;
}

// "Unstaged (2)" then "Staged (0)": the bar of each list, its title and its
// count (none outside a repository). A ViewHeader's labels: its count, then
// its title.
QStringList sectionTitles(const ChangesView &view)
{
    QStringList titles;
    for (const ViewHeader *header : view.findChildren<ViewHeader *>()) {
        const QList<QLabel *> labels = header->findChildren<QLabel *>(Qt::FindDirectChildrenOnly);
        const QLabel *count = labels.at(0);
        const QString title = labels.at(1)->text();
        titles << (count->isHidden() ? title : QStringLiteral("%1 (%2)").arg(title, count->text()));
    }
    std::ranges::sort(titles, std::greater<>()); // "Unstaged" first
    return titles;
}

// The list of unstaged files, or of staged ones; their files are under its
// one top-level row.
QTreeWidget *fileList(const ChangesView &view, const char *name)
{
    return view.findChild<QTreeWidget *>(name);
}

// "n1\nn2\n…": line n says its number.
QByteArray numberedLines(int count)
{
    QByteArray text;
    for (int n = 1; n <= count; ++n)
        text += "n" + QByteArray::number(n) + "\n";
    return text;
}

QStringList textsOf(const QList<DiffLine> &lines)
{
    QStringList texts;
    for (const DiffLine &line : lines)
        texts.append(line.text);
    return texts;
}

// The button of `parent` whose tooltip is `toolTip`; nullptr if none.
QToolButton *buttonWithToolTip(const QWidget *parent, const QString &toolTip)
{
    for (QToolButton *button : parent->findChildren<QToolButton *>()) {
        if (button->toolTip() == toolTip)
            return button;
    }
    return nullptr;
}

// A line added after line 29 of 49: line 30 of 50.
const QByteArray oneAddedLine = "@@ -27,6 +27,7 @@\n n27\n n28\n n29\n+n30\n n31\n n32\n n33\n";

} // namespace

class TestChanges : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    void diffLayoutDropsGitNotation()
    {
        const FilePatch patch = FilePatch::parse("diff --git a/x.cpp b/x.cpp\n"
                                                 "index 1..2 100644\n"
                                                 "--- a/x.cpp\n"
                                                 "+++ b/x.cpp\n"
                                                 "@@ -10,3 +10,3 @@ void run()\n"
                                                 " keep\n"
                                                 "-old\n"
                                                 "+new\n"
                                                 " end\n"
                                                 "@@ -40 +40,0 @@\n"
                                                 "-gone\n"
                                                 "\\ No newline at end of file\n");
        const QList<DiffLine> lines = DiffLayout::lines({{"x.cpp", "modified", patch}});
        QStringList texts;
        for (const DiffLine &line : lines)
            texts.append(line.text);
        QCOMPARE(texts, QStringList({"", "x.cpp   modified   +1 −2", "⋯ lines 10–12  ·  void run()", "keep", "old",
                                     "new", "end", "⋯ line 40", "gone", "No line break at the end of the file"}));
        QCOMPARE(lines[0].kind, DiffLineKind::Spacer);
        QCOMPARE(lines[1].kind, DiffLineKind::FileHeader);
        QCOMPARE(lines[1].emphasis, 5);    // "x.cpp"
        QCOMPARE(lines[1].statsStart, 19); // "+1 −2"
        QCOMPARE(lines[4].kind, DiffLineKind::Removed);
        QCOMPARE(lines[4].oldNumber, 11);
        QCOMPARE(lines[4].newNumber, 0);
        QCOMPARE(lines[5].kind, DiffLineKind::Added);
        QCOMPARE(lines[5].newNumber, 11);
        QCOMPARE(lines[6].oldNumber, 12);
        QCOMPARE(lines[6].newNumber, 12);
        // Each line leads back to its place in the patch, for staging.
        QCOMPARE(lines[5].location.hunk, 0);
        QCOMPARE(lines[5].location.line, 2);
        QVERIFY(patch.hunks[0].isChange(lines[5].location.line));
        QCOMPARE(lines[2].location.line, -1);
        QCOMPARE(lines[1].location.hunk, -1);
        QCOMPARE(lines[0].location.file, -1); // a blank line leads nowhere

        DiffView view(Theme::dark());
        view.setFiles(
            {{"src/x.cpp", "modified", patch}, {"y.bin", "added", FilePatch::parse("Binary files differ\n")}});
        QCOMPARE(view.displayedLines()[1].text, QString("x.cpp   src  ·  modified   +1 −2"));
        QCOMPARE(view.displayedLines().size(), lines.size() + 3);
        QCOMPARE(view.fileStart(1), static_cast<int>(lines.size()) + 1);
        QCOMPARE(view.locate(5).line, 2);
        QVERIFY(view.text().contains("Binary file"));
        QVERIFY(!view.text().contains("@@"));
    }

    void separatorsHoldTheUnchangedLinesAroundTheBlocks()
    {
        const DiffFile file{"x.cpp", "modified", FilePatch::parse(oneAddedLine), false, numberedLines(50)};

        const QList<DiffLine> lines = DiffLayout::lines({file});

        QCOMPARE(textsOf(lines).mid(2), QStringList({"⋯ ↑  lines 27–33", "n27", "n28", "n29", "n30", "n31", "n32",
                                                     "n33", "⋯ ↓  17 more lines"}));
        QCOMPARE(lines[2].hidden, (LineRange{1, 26}));
        QCOMPARE(lines[2].arrows.size(), 1);
        QCOMPARE(lines[2].arrows[0].reveal, Reveal::LastLines);
        QCOMPARE(lines[2].arrows[0].position, 2);
        QCOMPARE(lines.last().kind, DiffLineKind::HiddenLines);
        QCOMPARE(lines.last().hidden, (LineRange{34, 50}));
    }

    void revealedLinesKeepTheirNumbersOnBothSides()
    {
        const DiffFile file{"x.cpp", "modified", FilePatch::parse(oneAddedLine), false, numberedLines(50)};
        const QList<DiffLine> hidden = DiffLayout::lines({file});
        QSet<int> revealed;
        for (const auto &[separator, reveal] :
             {std::pair{hidden[2], Reveal::LastLines}, std::pair{hidden.last(), Reveal::FirstLines}}) {
            const LineRange part = DiffLayout::revealedPart(separator.hidden, reveal);
            for (int n = part.first; n <= part.last; ++n)
                revealed.insert(n);
        }

        const QList<DiffLine> lines = DiffLayout::lines({file}, {{"x.cpp", revealed}});

        QCOMPARE(lines[2].text, QString("⋯ ↑  lines 27–33")); // 1 to 6 still hidden
        QCOMPARE(lines[2].hidden, (LineRange{1, 6}));
        QCOMPARE(lines[3].text, QString("n7"));
        QCOMPARE(lines[3].oldNumber, 7);
        QCOMPARE(lines[3].newNumber, 7);
        // Part of the block for its buttons, out of its patch for staging.
        QCOMPARE(lines[3].location.hunk, 0);
        QCOMPARE(lines[3].location.line, -1);
        QCOMPARE(lines[23].text, QString("n27"));
        QCOMPARE(lines[30].text, QString("n34")); // the old version has one line less
        QCOMPARE(lines[30].oldNumber, 33);
        QCOMPARE(lines[30].newNumber, 34);
        QCOMPARE(lines.last().text, QString("n50"));
        QCOMPARE(lines.size(), 47);
    }

    void aShortGapBetweenBlocksIsRevealedAtOnce()
    {
        const QByteArray patch = "@@ -1,3 +1,3 @@\n-x\n+n1\n n2\n n3\n"
                                 "@@ -15,3 +15,3 @@\n n15\n-y\n+n16\n n17\n"
                                 "@@ -40,3 +40,3 @@\n n40\n-z\n+n41\n n42\n";
        const DiffFile file{"x.txt", "modified", FilePatch::parse(patch), false, numberedLines(42)};
        const QList<DiffLine> hidden = DiffLayout::lines({file});
        QStringList separators;
        for (const DiffLine &line : hidden) {
            if (line.kind == DiffLineKind::HunkHeader)
                separators.append(line.text);
        }
        QCOMPARE(separators, QStringList({"⋯ lines 1–3", "⋯ ↕  lines 15–17", "⋯ ↑ ↓  lines 40–42"}));
        QCOMPARE(hidden.last().text, QString("n42")); // nothing after the last block

        const LineRange part = DiffLayout::revealedPart(hidden[7].hidden, Reveal::AllLines);
        QSet<int> revealed;
        for (int n = part.first; n <= part.last; ++n)
            revealed.insert(n);
        const QList<DiffLine> lines = DiffLayout::lines({file}, {{"x.txt", revealed}});

        QCOMPARE(lines[7].text, QString("n4"));
        QCOMPARE(lines[17].text, QString("n14"));
        QCOMPARE(lines[18].text, QString("⋯ lines 15–17"));
        QVERIFY(lines[18].arrows.isEmpty());
    }

    void clickingAnArrowRevealsLinesInPlace()
    {
        DiffView view(Theme::dark());
        view.resize(700, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const DiffFile file{"x.txt", "modified", FilePatch::parse(oneAddedLine), false, numberedLines(50)};
        view.setFiles({file});
        const qsizetype hiddenCount = view.displayedLines().size();

        // The ↑ after "⋯ " on the separator, line 2.
        const long position =
            view.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, 2) + long(QStringLiteral("⋯ ").toUtf8().size());
        const QPoint arrow(int(view.SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0UL, position)) + 2,
                           view.lineTop(2) + view.lineHeight() / 2);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, arrow);

        QTRY_COMPARE(view.displayedLines().value(3).text, QString("n7"));
        QCOMPARE(view.displayedLines()[2].kind, DiffLineKind::HunkHeader);
        QCOMPARE(view.firstVisibleLine(), 0);

        view.hideUnchangedLines();
        view.setFiles({file});
        QCOMPARE(view.displayedLines().size(), hiddenCount);
    }

    void sideBySideFacesRemovedLinesWithAddedOnes()
    {
        const FilePatch patch = FilePatch::parse("@@ -10,4 +10,3 @@\n keep\n-a\n-b\n+A\n end\n");

        const DiffColumns columns = SideBySide::columns(DiffLayout::lines({{"x.cpp", "", patch}}));

        QCOMPARE(textsOf(columns.oldSide).mid(2), QStringList({"⋯ lines 10–12", "keep", "a", "b", "end"}));
        QCOMPARE(textsOf(columns.newSide).mid(2), QStringList({"⋯ lines 10–12", "keep", "A", "", "end"}));
        QCOMPARE(columns.newSide[5].kind, DiffLineKind::Filler);
        // The filler leads to the line it faces, for staging.
        QCOMPARE(columns.newSide[5].location.line, columns.oldSide[5].location.line);
        QCOMPARE(columns.oldSide[4].oldNumber, 11);
        QCOMPARE(columns.newSide[4].newNumber, 11);
    }

    void foldingTheFileListShowsTheBarAndKeepsTheFileArrows()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        write(project, "b.txt", "b\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "a2\n");
        write(project, "b.txt", "b2\n");
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 400);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        const auto *column = view.findChild<QFrame *>("changesColumn");
        const auto *bar = view.findChild<FoldedPane *>();
        const auto *header = view.findChild<DiffHeaderBar *>();
        QToolButton *nextFile = buttonWithToolTip(header, "Next file (Alt+Down)");
        QVERIFY(nextFile);
        QVERIFY(!bar->isVisible());
        QVERIFY(nextFile->isVisible());

        buttonWithToolTip(&view, "Hide the file list")->click();

        QVERIFY(!column->isVisible());
        QVERIFY(bar->isVisible());
        QVERIFY(nextFile->isVisible());
    }

    void eachListAndTheCommitAreaTakeAResizableShareOfTheColumn()
    {
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 700);
        view.show();
        QSplitter *column = view.fileSplitter();
        QTreeWidget *unstaged = fileList(view, "unstagedList");
        const int before = unstaged->height();

        column->setSizes({400, 100, 200});

        QCOMPARE(column->count(), 3);
        QVERIFY(column->widget(0)->isAncestorOf(unstaged));
        QVERIFY(column->widget(1)->isAncestorOf(fileList(view, "stagedList")));
        QVERIFY(qobject_cast<CommitPanel *>(column->widget(2)));
        QVERIFY(unstaged->height() > before);
    }

    void theCommitMessageGrowsWithTheCommitArea()
    {
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 700);
        view.show();
        QSplitter *column = view.fileSplitter();
        const auto *message = column->widget(2)->findChild<QPlainTextEdit *>();
        const int before = message->height();

        column->setSizes({200, 200, 300});

        QCOMPARE(before, message->minimumHeight()); // a few lines at first
        QVERIFY(message->height() > before + 100);
    }

    void selectingInOneListClearsTheOther()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        write(project, "b.txt", "b\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "a2\n");
        write(project, "b.txt", "b2\n");
        Git::run(project.path(), {"add", "b.txt"});
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        QTreeWidget *unstaged = fileList(view, "unstagedList");
        QTreeWidget *staged = fileList(view, "stagedList");
        unstaged->setCurrentItem(unstaged->topLevelItem(0)->child(0));

        staged->setCurrentItem(staged->topLevelItem(0)->child(0));

        QVERIFY(unstaged->selectedItems().isEmpty());
        QCOMPARE(staged->selectedItems().size(), 1);
    }

    void theBarsIconBringsTheFileListBack()
    {
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 400);
        view.show();
        view.setFileListVisible(false);
        const auto *bar = view.findChild<FoldedPane *>();

        buttonWithToolTip(bar, "Show the file list")->click();

        QVERIFY(view.isFileListVisible());
        QVERIFY(!bar->isVisible());
        QVERIFY(view.splitter()->sizes().constFirst() > 0);
    }

    void diffHeaderLinesUpWithTheFileActionsWithOrWithoutFiles()
    {
        ThemeStyle::apply(Theme::dark()); // the buttons' height, as in the application
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 300);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        const auto *header = view.findChild<DiffHeaderBar *>();
        const auto *fileActions = view.findChild<QFrame *>("fileActions");
        const auto bottomOf = [&view](const QWidget *bar) { return bar->mapTo(&view, QPoint(0, bar->height())).y(); };

        view.refresh(); // no changes: the header hides its buttons
        QTRY_COMPARE(refreshed.size(), 1);
        QTRY_COMPARE(bottomOf(header), bottomOf(fileActions));
        write(project, "a.txt", "b\n");
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 2);
        QTRY_COMPARE(bottomOf(header), bottomOf(fileActions));
        // The columns right under the title bar: the handle's line meets its rule.
        QCOMPARE(view.splitter()->y(), view.findChild<QFrame *>("viewTitleBar")->geometry().bottom() + 1);
        // Across the handle between them too: one line.
        QWidget *handle = view.splitter()->handle(1);
        const QImage image = handle->grab().toImage();
        const int ruleY = header->height() - 1;
        for (int x = 0; x < image.width(); ++x)
            QCOMPARE(image.pixelColor(x, ruleY), Theme::dark().line);
    }

    void sideBySideScrollsBothSidesTogether()
    {
        DiffView view(Theme::dark());
        view.resize(800, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const DiffFile file{"x.txt", "modified", FilePatch::parse(oneAddedLine), false, numberedLines(50)};
        view.setFiles({file});

        view.setMode(DiffView::Mode::SideBySide);
        auto *oldSide = view.findChild<DiffView *>();
        QVERIFY(oldSide && oldSide->isVisible());
        QCOMPARE(oldSide->displayedLines().size(), view.displayedLines().size());
        QVERIFY(oldSide->width() < view.width() / 2 + 1);
        view.revealLines(2, Reveal::LastLines);
        view.setFirstVisibleLine(12);

        QTRY_COMPARE(oldSide->firstVisibleLine(), 12);
        oldSide->setFirstVisibleLine(4);
        QTRY_COMPARE(view.firstVisibleLine(), 4);

        view.setMode(DiffView::Mode::Unified);
        QVERIFY(!oldSide->isVisible());
    }

    void diffSaysAFileIsTooLargeToShow()
    {
        DiffView view(Theme::dark());

        view.setFiles({{"dump.log", "added", {}, true}});

        QCOMPARE(view.displayedLines().last().text, QString("File too large to show"));
        QCOMPARE(view.displayedLines().last().kind, DiffLineKind::Note);
    }

    void diffShowsChangedWords()
    {
        using Range = TextRange;
        // One word replaced: only it stands out, on both sides.
        const auto [before, after] = WordDiff::changes("int count = 0;", "int total = 0;");
        QCOMPARE(before, QList<Range>({{4, 5}}));
        QCOMPARE(after, QList<Range>({{4, 5}}));
        // Two neighbouring words: one range, the space between them with them.
        QCOMPARE(WordDiff::changes("call(a, b)", "call(x y, b)").second, QList<Range>({{5, 3}}));
        // Rewritten: nothing stands out.
        QVERIFY(WordDiff::changes("return value;", "throw Error(\"no\");").first.isEmpty());

        // Removed then added lines face each other in the layout.
        const FilePatch patch = FilePatch::parse("@@ -1,2 +1,2 @@\n-int count = 0;\n-gone\n+int total = 0;\n");
        const QList<DiffLine> lines = DiffLayout::lines({{"x.cpp", "", patch}});
        QCOMPARE(lines[3].changes, QList<Range>({{4, 5}}));
        QVERIFY(lines[4].changes.isEmpty());
        QCOMPARE(lines[5].changes, QList<Range>({{4, 5}}));
    }

    void diffHidesHeadingsOfProse()
    {
        const FilePatch patch = FilePatch::parse("@@ -10 +10 @@ a sentence cut\n-a\n+b\n");
        QCOMPARE(DiffLayout::lines({{"x.md", "", patch}})[2].text, QString("⋯ line 10"));
        QCOMPARE(DiffLayout::lines({{"x.cpp", "", patch}})[2].text, QString("⋯ line 10  ·  a sentence cut"));
    }

    void diffHeaderFollowsTheFileAtTheTop()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        const QList<DiffFile> files = {{"src/one.cpp", "modified", FilePatch::parse(patch)},
                                       {"src/two.cpp", "modified", FilePatch::parse(patch)}};
        DiffView diff(Theme::dark());
        DiffHeaderBar header(&diff);
        diff.resize(600, 30); // a line or two: the second file needs scrolling
        diff.show();
        diff.setFiles(files);
        const auto texts = [&header] {
            QStringList all;
            for (const QLabel *label : header.findChildren<QLabel *>())
                all.append(label->text());
            return all;
        };
        QVERIFY(texts().contains("one.cpp"));
        diff.setFirstVisibleLine(diff.fileStart(1));
        QTRY_VERIFY(texts().contains("two.cpp")); // Scintilla tells once it has painted
        QVERIFY(texts().contains("2 / 2"));
    }

    void filePagerShowsThePositionOutOfTheDetails()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        const QList<DiffFile> files = {{"src/one.cpp", "modified", FilePatch::parse(patch)},
                                       {"src/two.cpp", "modified", FilePatch::parse(patch)}};
        DiffView diff(Theme::dark());
        DiffHeaderBar header(&diff);
        const auto texts = [&header] {
            QStringList all;
            for (const QLabel *label : header.findChildren<QLabel *>())
                all.append(label->text());
            return all;
        };

        diff.setFiles(files);

        QVERIFY(texts().contains("1 / 2"));
        QVERIFY(!texts().join(' ').contains("of 2"));
    }

    void filePagerSitsInTheMiddleOfTheBar()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        const QList<DiffFile> files = {{"src/one.cpp", "modified", FilePatch::parse(patch)},
                                       {"src/two.cpp", "modified", FilePatch::parse(patch)}};
        DiffView diff(Theme::dark());
        DiffHeaderBar header(&diff);
        header.resize(800, header.sizeHint().height());
        header.show();

        diff.setFiles(files);

        const auto *pager = header.findChild<QFrame *>("filePager");
        QTRY_VERIFY(pager->isVisible());
        const QRect inside = header.contentsRect().marginsRemoved(header.layout()->contentsMargins());
        QTRY_VERIFY(std::abs(pager->geometry().center().x() - inside.center().x()) <= 1);
    }

    void draggingTheDividerSharesTheWidthBetweenTheSides()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        DiffView diff(Theme::dark());
        diff.resize(800, 300);
        diff.show();
        diff.setMode(DiffView::Mode::SideBySide);
        diff.setFiles({{"src/one.cpp", "modified", FilePatch::parse(patch)}});
        auto *divider = diff.findChild<SideBySideDivider *>();
        const auto *oldSide = diff.findChild<DiffView *>();
        QVERIFY(divider && divider->isVisible());
        QVERIFY(std::abs(oldSide->width() - diff.contentsRect().width() / 2) <= 2);

        const QPoint center = divider->rect().center();
        QTest::mousePress(divider, Qt::LeftButton, {}, center);
        QTest::mouseMove(divider, center + QPoint(200, 0));
        QTest::mouseRelease(divider, Qt::LeftButton, {}, center + QPoint(200, 0));

        QVERIFY(oldSide->width() > diff.contentsRect().width() / 2 + 150);
        QCOMPARE(diff.viewport()->x(), diff.contentsRect().x() + oldSide->width() + 1);
    }

    void zoomingEitherSideZoomsTheOther()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        DiffView diff(Theme::dark());
        diff.zoomIn(); // before the old side exists
        diff.setMode(DiffView::Mode::SideBySide);
        diff.setFiles({{"src/one.cpp", "modified", FilePatch::parse(patch)}});
        auto *oldSide = diff.findChild<DiffView *>();
        const auto zoom = [](const DiffView &view) { return view.SendScintilla(QsciScintillaBase::SCI_GETZOOM); };
        QCOMPARE(zoom(*oldSide), 1);

        diff.zoomIn();
        QCOMPARE(zoom(*oldSide), 2);
        oldSide->zoomOut();
        QCOMPARE(zoom(diff), 1);
    }

    void theOldSideDraggedToItsMinimumGrowsWithTheView()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        DiffView diff(Theme::dark());
        diff.resize(800, 300);
        diff.show();
        diff.setMode(DiffView::Mode::SideBySide);
        diff.setFiles({{"src/one.cpp", "modified", FilePatch::parse(patch)}});
        auto *divider = diff.findChild<SideBySideDivider *>();
        const auto *oldSide = diff.findChild<DiffView *>();
        const QPoint center = divider->rect().center();
        QTest::mousePress(divider, Qt::LeftButton, {}, center);
        QTest::mouseMove(divider, center - QPoint(2000, 0)); // far past the left edge
        QTest::mouseRelease(divider, Qt::LeftButton, {}, center - QPoint(2000, 0));
        const int narrowest = oldSide->width();

        diff.resize(1600, 300);

        QTRY_VERIFY(oldSide->width() > narrowest + 40);
    }

    void goToChangeScrollsBlockByBlock()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n@@ -40 +40 @@\n-c\n+d\n";
        DiffView diff(Theme::dark());
        diff.resize(600, 40);
        diff.show();
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        const QList<DiffLine> &lines = diff.displayedLines();
        QList<int> blocks;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].kind == DiffLineKind::HunkHeader)
                blocks.append(i);
        }
        QCOMPARE(blocks.size(), 2);
        diff.goToChange(DiffView::Direction::Next);
        QCOMPARE(diff.firstVisibleLine(), blocks[0]);
        diff.goToChange(DiffView::Direction::Next);
        QCOMPARE(diff.firstVisibleLine(), blocks[1]);
        diff.goToChange(DiffView::Direction::Previous);
        QCOMPARE(diff.firstVisibleLine(), blocks[0]);
    }

    void keysGoFromBlockToBlockAndFileToFile()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n@@ -40 +40 @@\n-c\n+d\n";
        DiffView diff(Theme::dark());
        diff.resize(600, 40);
        diff.show();
        diff.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&diff));
        diff.setFocus();
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}, {"y.txt", "modified", FilePatch::parse(patch)}});
        const int firstBlock = diff.hunkStart(diff.fileStart(0) + 2);

        QTest::keyClick(&diff, Qt::Key_F7);
        QCOMPARE(diff.firstVisibleLine(), firstBlock);
        int line = 0;
        int index = 0;
        diff.getCursorPosition(&line, &index);
        QCOMPARE(line, firstBlock); // on the block, for Space

        QTest::keyClick(&diff, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(diff.firstVisibleLine(), diff.fileStart(1));
        QTest::keyClick(&diff, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(diff.firstVisibleLine(), diff.fileStart(0));
    }

    void removedLinesOpenAtTheNextLineLeft()
    {
        const QByteArray patch = "@@ -1,3 +1,2 @@\n a\n-b\n c\n@@ -9,2 +8 @@\n z\n-end\n";
        DiffView diff(Theme::dark());
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        const QList<DiffLine> &lines = diff.displayedLines();
        QList<int> removed;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].kind == DiffLineKind::Removed)
                removed.append(i);
        }
        QCOMPARE(removed.size(), 2);

        QCOMPARE(diff.newFileLine(removed[0]), 1);        // "c", line 2
        QCOMPARE(diff.newFileLine(removed[1]), 8);        // after "z", the last line
        QCOMPARE(diff.newFileLine(diff.fileStart(0)), 0); // the title bar: the first line shown
        QCOMPARE(diff.newFileLine(removed[0] - 1), 0);    // "a" itself
    }

    void blockButtonsActOnTheBlockUnderTheMouse()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n";
        DiffView diff(Theme::dark());
        const HunkActions actions(&diff);
        diff.resize(600, 300);
        diff.show();
        QVERIFY(QTest::qWaitForWindowExposed(&diff));
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        int block = -1;
        for (int i = 0; i < diff.displayedLines().size(); ++i) {
            if (diff.displayedLines()[i].kind == DiffLineKind::Added)
                block = diff.hunkStart(i);
        }
        QVERIFY(block >= 0);
        const QSignalSpy applied(&actions, &HunkActions::applyRequested);
        const QPoint over(100, diff.lineTop(block + 1) + 2);
        QMouseEvent move(QEvent::MouseMove, over, diff.viewport()->mapToGlobal(over), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(diff.viewport(), &move);
        QToolButton *stage = nullptr;
        for (QToolButton *button : diff.viewport()->findChildren<QToolButton *>()) {
            if (button->text() == "Stage")
                stage = button;
        }
        if (!stage)
            QFAIL("no Stage button over the block");
        QVERIFY(stage->isVisible());
        stage->click();
        QCOMPARE(applied.size(), 1);
        QCOMPARE(applied.first().at(0).toInt(), block);
    }

    void blockButtonsHideWhenTheDiffScrolls()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n@@ -40 +40 @@\n-c\n+d\n@@ -80 +80 @@\n-e\n+f\n";
        DiffView diff(Theme::dark());
        const HunkActions actions(&diff);
        diff.resize(600, 120); // a few lines: the last block needs scrolling
        diff.show();
        QVERIFY(QTest::qWaitForWindowExposed(&diff));
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        const int block = diff.hunkStart(diff.fileStart(0) + 2);
        QVERIFY(block >= 0);
        const QPoint over(100, diff.lineTop(block + 1) + 2);
        QMouseEvent move(QEvent::MouseMove, over, diff.viewport()->mapToGlobal(over), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(diff.viewport(), &move);
        const QList<QToolButton *> buttons = diff.viewport()->findChildren<QToolButton *>();
        const auto anyVisible = [&buttons] {
            return std::any_of(buttons.begin(), buttons.end(), [](const QToolButton *b) { return b->isVisible(); });
        };
        QVERIFY(anyVisible());

        // As the keyboard scrolls: no wheel, no mouse move.
        diff.goToChange(DiffView::Direction::Next);
        diff.goToChange(DiffView::Direction::Next);

        QTRY_VERIFY(!anyVisible());
    }

    void diffColorsTheCodeAsTheEditor()
    {
        const FilePatch patch = FilePatch::parse("@@ -1 +1 @@\n-// old comment\n+return value;\n");
        DiffView diff(Theme::dark());
        diff.setFiles({{"x.cpp", "modified", patch}});
        int removed = -1;
        int added = -1;
        for (int i = 0; i < diff.displayedLines().size(); ++i) {
            if (diff.displayedLines()[i].kind == DiffLineKind::Removed)
                removed = i;
            if (diff.displayedLines()[i].kind == DiffLineKind::Added)
                added = i;
        }
        const auto styleAt = [&diff](int line) {
            const long position = diff.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
            return int(diff.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position));
        };

        const int comment = styleAt(removed);
        const int keyword = styleAt(added);

        // Both sides colored, each with its own syntax style (not the plain one).
        QVERIFY(comment != 0);
        QVERIFY(keyword != 0);
        QVERIFY(comment != keyword);
    }

    void diffColorsEveryFileOfALanguage()
    {
        // One scratch editor colors every C++ file: the second one too.
        const FilePatch patch = FilePatch::parse("@@ -1 +1 @@\n-// old comment\n+return value;\n");
        DiffView diff(Theme::dark());

        diff.setFiles({{"x.cpp", "modified", patch}, {"y.cpp", "modified", patch}});

        const QList<DiffLine> &lines = diff.displayedLines();
        const int lastAdded = static_cast<int>(lines.size()) - 1;
        QCOMPARE(lines[lastAdded].kind, DiffLineKind::Added);
        const long position = diff.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, lastAdded);
        QVERIFY(diff.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position) != 0);
    }

    void outsideARepository()
    {
        const QTemporaryDir project;
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        QCOMPARE(sectionTitles(view), QStringList({"Unstaged", "Staged"}));
        QVERIFY(!commitButton(view)->isEnabled());
    }

    void stageAllThenCommit()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "b\n");
        write(project, "new.txt", "n\n");

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh(); // off the UI thread
        QTRY_COMPARE(refreshed.size(), 1);
        const QTreeWidget *list = fileList(view, "unstagedList");
        QCOMPARE(sectionTitles(view), QStringList({"Unstaged (2)", "Staged (0)"}));
        QCOMPARE(list->topLevelItem(0)->child(0)->text(0), QString("a.txt"));
        QCOMPARE(list->topLevelItem(0)->child(0)->data(0, FileChangeDelegate::kindRole).toInt(),
                 static_cast<int>(ChangeKind::Modified));
        QCOMPARE(list->topLevelItem(0)->child(1)->data(0, FileChangeDelegate::kindRole).toInt(),
                 static_cast<int>(ChangeKind::Added));

        view.stageAll();
        QTRY_COMPARE(sectionTitles(view), QStringList({"Unstaged (0)", "Staged (2)"}));

        // The last commit's message is offered for "amend", then removed.
        auto *message = view.findChild<QPlainTextEdit *>();
        auto *amend = view.findChild<QCheckBox *>();
        amend->setChecked(true);
        QCOMPARE(message->toPlainText(), QString("init"));
        amend->setChecked(false);
        QVERIFY(message->toPlainText().isEmpty());

        message->setPlainText("second");
        commitButton(view)->click();
        QTRY_COMPARE(sectionTitles(view), QStringList({"Unstaged (0)", "Staged (0)"}));
        QVERIFY(message->toPlainText().isEmpty());
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("second\n"));
    }

    void spaceStagesTheBlockAtTheCursorAndF4OpensItsLine()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "one\n2\n3\n4\n5\n6\n7\n8\n9\nten\n");

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 600);
        view.show();
        view.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&view));
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *diff = view.findChild<DiffView *>();
        diff->setFocus();
        QTest::keyClick(diff, Qt::Key_F7);
        QTest::keyClick(diff, Qt::Key_F7); // the second block: lines 7 to 10

        const QSignalSpy opened(&view, &ChangesView::openFileRequested);
        QTest::keyClick(diff, Qt::Key_F4);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).toString(), QString("a.txt"));
        QCOMPARE(opened.first().at(1).toInt(), 6); // the block's first line

        QTest::keyClick(diff, Qt::Key_Space);
        QTRY_COMPARE(sectionTitles(view), QStringList({"Unstaged (1)", "Staged (1)"}));
        const QByteArray staged = Git::run(project.path(), {"diff", "--cached"});
        QVERIFY(staged.contains("+ten"));
        QVERIFY(!staged.contains("+one"));
    }

    void hidingWhitespaceRereadsTheDiffAndBlocksLineStaging()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a b\nc\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "a   b\nd\n");
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 600);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *diff = view.findChild<DiffView *>();
        QToolButton *whitespace = nullptr;
        for (QToolButton *button : view.findChild<DiffHeaderBar *>()->findChildren<QToolButton *>()) {
            if (button->toolTip() == QLatin1String("Hide whitespace changes"))
                whitespace = button;
        }
        QVERIFY(whitespace);

        whitespace->click();

        QTRY_COMPARE(refreshed.size(), 2);
        const QList<DiffLine> &lines = diff->displayedLines();
        const auto removed = [&lines](const QString &text) {
            return std::any_of(lines.begin(), lines.end(), [&text](const DiffLine &line) {
                return line.kind == DiffLineKind::Removed && line.text == text;
            });
        };
        QVERIFY(!removed("a b")); // now a line of context
        QVERIFY(removed("c"));
        const QSignalSpy messages(&view, &ChangesView::statusMessage);
        diff->setFocus();
        QTest::keyClick(diff, Qt::Key_F7); // the block "-c +d"
        QTest::keyClick(diff, Qt::Key_Space);
        QCOMPARE(messages.size(), 1); // why nothing was staged
        QVERIFY(Git::run(project.path(), {"diff", "--cached"}).isEmpty());

        whitespace->click();
        QTRY_COMPARE(refreshed.size(), 3);
        QVERIFY(removed("a b"));
    }

    void sideBySideStagesBothSidesOfASelectedRow()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "1\n2\n3\nfour\n5\n6\n7\n8\n9\nten\n");
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.resize(900, 600);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *diff = view.findChild<DiffView *>();
        diff->setMode(DiffView::Mode::SideBySide);
        const QList<DiffLine> &rows = diff->displayedLines();
        const auto four =
            std::find_if(rows.begin(), rows.end(), [](const DiffLine &row) { return row.text == "four"; });
        QVERIFY(four != rows.end());
        const int row = int(four - rows.begin());

        diff->setSelection(row, 0, row + 1, 0);
        diff->setFocus();
        QTest::keyClick(diff, Qt::Key_Space);

        QTRY_COMPARE(sectionTitles(view), QStringList({"Unstaged (1)", "Staged (1)"}));
        const QByteArray staged = Git::run(project.path(), {"diff", "--cached"});
        QVERIFY(staged.contains("-4\n+four"));
        QVERIFY(!staged.contains("+ten"));
    }

    void filesShowInTheirFoldersSortedAndMerged()
    {
        QTreeWidget tree;
        const QStringList paths{"src/changes/b.cpp", "src/changes/a.cpp", "src/git/x.cpp", "README.md",
                                "docs/guide/intro.md"};

        const QList<QTreeWidgetItem *> rows = ChangeTree::addFiles(tree.invisibleRootItem(), paths);
        for (qsizetype i = 0; i < rows.size(); ++i)
            FileChangeDelegate::setChange(rows[i], {ChangeKind::Modified, paths[i], {}});

        QCOMPARE(rows.size(), 5);
        QCOMPARE(tree.topLevelItemCount(), 3);                          // docs/guide, src, then README.md
        QCOMPARE(tree.topLevelItem(0)->text(0), QString("docs/guide")); // a folder of one folder: merged
        QCOMPARE(tree.topLevelItem(1)->text(0), QString("src"));
        QCOMPARE(tree.topLevelItem(2), rows[3]);
        QTreeWidgetItem *changes = tree.topLevelItem(1)->child(0);
        QCOMPARE(changes->text(0), QString("changes"));
        QCOMPARE(changes->data(0, FileChangeDelegate::folderRole).toString(), QString("src/changes"));
        QCOMPARE(changes->child(0), rows[1]); // a.cpp before b.cpp
        QCOMPARE(ChangeTree::filesUnder(tree.topLevelItem(1)), QList<QTreeWidgetItem *>({rows[1], rows[0], rows[2]}));
        QVERIFY(ChangeTree::comesBefore("src/z.cpp", "a.cpp")); // a folder before a file
        QVERIFY(ChangeTree::comesBefore("src/a.cpp", "src/B.cpp"));
        QVERIFY(!ChangeTree::comesBefore("src/b.cpp", "src/a.cpp"));
    }

    void aFolderStandsForItsFilesAndStaysClosed()
    {
        const QTemporaryDir project;
        write(project, "src/a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "src/a.txt", "a2\n");
        write(project, "src/b.txt", "b\n");
        write(project, "c.txt", "c\n");
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show(); // refreshed after a git command only when shown
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        QTreeWidget *list = fileList(view, "unstagedList");
        QTreeWidgetItem *src = list->topLevelItem(0)->child(0);
        QVERIFY(FileChangeDelegate::isFolder(src));
        // The diff in the list's order: the folder's files first.
        QStringList diffOrder;
        for (const DiffView::FileSummary &file : view.findChild<DiffView *>()->fileSummaries())
            diffOrder.append(file.path);
        QCOMPARE(diffOrder, QStringList({"src/a.txt", "src/b.txt", "c.txt"}));
        src->setExpanded(false);
        list->setCurrentItem(src);
        QToolButton *stage = nullptr;
        for (QToolButton *button : view.findChildren<QToolButton *>()) {
            if (button->text() == QLatin1String("Stage"))
                stage = button;
        }
        QVERIFY(stage && stage->isEnabled());

        stage->click();

        QTRY_COMPARE(sectionTitles(view), QStringList({"Unstaged (1)", "Staged (2)"}));
        QTreeWidgetItem *stagedSrc = fileList(view, "stagedList")->topLevelItem(0)->child(0);
        QCOMPARE(stagedSrc->text(0), QString("src"));
        QVERIFY(stagedSrc->isExpanded()); // another section: its own folder
        write(project, "c.txt", "c2\n");
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 3);
        // The list rebuilt, the folder closed by hand stays closed.
        fileList(view, "stagedList")->topLevelItem(0)->child(0)->setExpanded(false);
        write(project, "c.txt", "c3\n");
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 4);
        QVERIFY(!fileList(view, "stagedList")->topLevelItem(0)->child(0)->isExpanded());
    }

    void snapshotsPopupListsAndRestores()
    {
        const QTemporaryDir project;
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "changed\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        Snapshots::create(info, "Before discarding a.txt", {"a.txt"});
        write(project, "a.txt", "later\n");

        QWidget window;
        GitCommands commands;
        SnapshotsPopup popup(&commands, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 1);
        QCOMPARE(popup.detailsTitle()->text(), QString("Before discarding a.txt"));
        QCOMPARE(popup.files()->count(), 1);
        QCOMPARE(popup.files()->item(0)->text(), QString("a.txt"));

        const QSignalSpy succeeded(&commands, &GitCommands::succeeded);
        popup.restoreSelected();
        QTRY_COMPARE(succeeded.count(), 1);
        QFile file(project.filePath("a.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed\n"));
    }

    void snapshotsPopupWithoutSnapshots()
    {
        const QTemporaryDir project;
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        QWidget window;
        GitCommands commands;
        SnapshotsPopup popup(&commands, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 0);
    }

    void snapshotDatesReadNaturally()
    {
        const QDateTime now(QDate(2026, 10, 16), QTime(15, 0)); // a Friday
        QCOMPARE(SnapshotsPopup::describeDate(now.addSecs(-20), now), QString("Just now"));
        QCOMPARE(SnapshotsPopup::describeDate(now.addSecs(-12LL * 60), now), QString("12 min ago"));
        QVERIFY(SnapshotsPopup::describeDate(now.addSecs(-3LL * 3600), now).startsWith("Today at "));
        QVERIFY(SnapshotsPopup::describeDate(now.addDays(-1), now).startsWith("Yesterday at "));
        QVERIFY(SnapshotsPopup::describeDate(now.addDays(-3), now).startsWith(QLocale().dayName(2)));
    }

    void newBranchPopupChecksTheName()
    {
        QWidget window;
        NewBranchPopup popup(Theme::dark(), &window);
        RepositoryInfo info;
        info.branch = "main";
        info.headId = "abc1234";
        popup.load(info, {"main", "feature/x"});
        QSignalSpy requested(&popup, &NewBranchPopup::createRequested);

        QVERIFY(!popup.createButton()->isEnabled()); // no name yet
        QTest::keyClicks(popup.nameField(), "fix login bug");
        QCOMPARE(popup.nameField()->text(), QString("fix-login-bug")); // as typed
        QVERIFY(popup.createButton()->isEnabled());

        popup.nameField()->setText("feature/x");
        QVERIFY(!popup.createButton()->isEnabled());
        QVERIFY(popup.message()->text().contains("already exists"));
        popup.nameField()->setText("bad..name");
        QVERIFY(!popup.createButton()->isEnabled());

        popup.nameField()->setText("feature/y");
        popup.switchToggle()->setChecked(false);
        popup.create();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toString(), QString("feature/y"));
        QCOMPARE(requested.first().at(1).toBool(), false);
        popup.switchToggle()->setChecked(true); // the saved choice back to its default
    }

    void branchesPopupFiltersAndSwitches()
    {
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        Git::run(project.path(), {"branch", "feature/a"});
        Git::run(project.path(), {"branch", "feature/b"});

        QWidget window;
        GitCommands commands;
        GitActions actions(&commands, &window, [&] { return project.path(); });
        BranchesPopup popup(&actions, Theme::dark(), &window);
        popup.load(project.path());
        QStringList rows;
        for (int i = 0; i < popup.list()->count(); ++i)
            rows.append(popup.list()->item(i)->text());
        QCOMPARE(rows, QStringList({"Local", "feature/a", "feature/b", "main"}));
        QCOMPARE(popup.selected()->name, QString("feature/a")); // not the current branch

        QTest::keyClicks(popup.filterField(), "/B");
        QCOMPARE(popup.list()->count(), 2); // the heading, feature/b
        QCOMPARE(popup.selected()->name, QString("feature/b"));
        QTest::keyClick(popup.filterField(), Qt::Key_Return);
        QTRY_COMPARE(RepositoryInfo::read(project.path())->branch, QString("feature/b"));

        popup.load(project.path());
        popup.filterField()->setText("main");
        QVERIFY(popup.switchButton()->isEnabled());
        popup.filterField()->setText("feature/b");
        QVERIFY(popup.selected()->current);
        QVERIFY(!popup.switchButton()->isEnabled());
    }

    void stashesPopupShowsTheirChanges()
    {
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "changed\n");
        write(project, "new.txt", "new\n");
        Stashes::push(*RepositoryInfo::read(project.path()), "work in progress", true);

        QWidget window;
        GitCommands commands;
        GitActions actions(&commands, &window, [&] { return project.path(); });
        StashesPopup popup(&actions, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 1);
        QVERIFY(popup.list()->item(0)->text().startsWith("On main: work in progress"));

        const QList<DiffFile> files = StashesPopup::splitPatch(Stashes::patch(project.path(), 0));
        QCOMPARE(files.size(), 2);
        QCOMPARE(files[0].path, QString("a.txt"));
        QCOMPARE(files[0].status, QString("modified"));
        QCOMPARE(files[1].path, QString("new.txt"));
        QCOMPARE(files[1].status, QString("added"));
        QCOMPARE(files[1].patch.addedLines(), 1);
    }

    void operationBarDuringAMerge()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        Git::run(project.path(), {"switch", "-qc", "other"});
        write(project, "a.txt", "other\n");
        Git::run(project.path(), {"commit", "-qam", "other"});
        Git::run(project.path(), {"switch", "-q", "main"});
        write(project, "a.txt", "main\n");
        Git::run(project.path(), {"commit", "-qam", "main"});
        QVERIFY_THROWS_EXCEPTION(GitError, Git::run(project.path(), {"merge", "other"}));

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show();
        view.setFolder(project.path());
        QVERIFY(!view.operationBar()->isVisible());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        QVERIFY(view.operationBar()->isVisible());
        QVERIFY2(view.operationText()->text().contains("1 conflict"), qPrintable(view.operationText()->text()));
        QVERIFY(!view.continueButton()->isEnabled());

        write(project, "a.txt", "resolved\n");
        Git::run(project.path(), {"add", "a.txt"});
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 2);
        QVERIFY(view.continueButton()->isEnabled());
        const QSignalSpy continued(&view, &ChangesView::continueRequested);
        view.continueButton()->click();
        QCOMPARE(continued.size(), 1);

        Git::run(project.path(), {"commit", "--no-edit", "-q"});
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 3);
        QVERIFY(!view.operationBar()->isVisible());
    }

    void generateAsksTheClaudeSessionForTheMessage()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "Add a"});
        write(project, "a.txt", "b\n");

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        const QSignalSpy requested(&view, &ChangesView::commitMessageRequested);
        auto *message = view.findChild<QPlainTextEdit *>();
        message->setPlainText("mine");

        view.generateCommitMessage(); // nothing staged: every change
        QCOMPARE(requested.size(), 1);
        const QString request = requested.first().at(0).toString();
        QVERIFY(request.contains("set_commit_message"));
        QVERIFY(request.contains("nothing is staged"));

        view.showCommitMessage("Change a\n\nBecause.");
        QCOMPARE(message->toPlainText(), QString("Change a\n\nBecause."));
        message->undo(); // what was written before comes back
        QCOMPARE(message->toPlainText(), QString("mine"));
    }

private:
    QTemporaryFile config_;
};

QTEST_MAIN(TestChanges)
#include "tst_changes.moc"
