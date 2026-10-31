// SPDX-License-Identifier: GPL-3.0-only

#include "FileLens.h"

#include "CommitDelegate.h"
#include "CommitTrack.h"
#include "scintilla/ScintillaView.h"
#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "ui/AppFonts.h"

#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <numeric>

namespace {

// The margins, from the left.
constexpr int commitMargin = 0; // the commit's day and summary
constexpr int ageMargin = 1;
constexpr int numberMargin = 2;
constexpr int commitMarginChars = 34;
constexpr int ageMarginPx = 4;
// Scintilla's hand cursor (Window::Cursor::hand), which Scintilla.h does
// not name: a commit's name can be clicked.
constexpr long handCursor = 8;
// The margin's text styles, from its style offset.
constexpr int commitNameStyle = 0;
constexpr int hoveredNameStyle = 1;
constexpr long commitNameStyleCount = 2;

// Markers 0 to 4: the ages, the oldest first, in the age margin; then the
// lines of the chosen commit, behind the text.
constexpr std::array ageTints{0.16, 0.3, 0.46, 0.66, 0.92};
constexpr int ageCount = int(ageTints.size());
constexpr int chosenMarker = ageCount;
constexpr qreal chosenTint = 0.16;

QColor blend(const QColor &from, const QColor &to, qreal share)
{
    return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * share),
                            float(from.greenF() + (to.greenF() - from.greenF()) * share),
                            float(from.blueF() + (to.blueF() - from.blueF()) * share));
}

QColor ageColor(const Theme &theme, int age)
{
    return blend(theme.background, theme.accent, ageTints.at(size_t(age)));
}

} // namespace

FileLens::FileLens(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , theme_(theme)
    , track_(new CommitTrack(this))
    , info_(new QLabel(this))
    , message_(new QLabel(this))
    , pages_(new QStackedWidget(this))
    , linesPage_(new QWidget(pages_))
{
    connect(track_, &CommitTrack::commitClicked, this, &FileLens::commitClicked);
    info_->setObjectName(QStringLiteral("historyMuted"));
    info_->setTextFormat(Qt::RichText);
    info_->setWordWrap(true);
    message_->setObjectName(QStringLiteral("historyMuted"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);

    auto *top = new QVBoxLayout;
    top->setContentsMargins(16, 10, 16, 8);
    top->setSpacing(6);
    top->addWidget(track_);
    top->addWidget(info_);
    auto *linesLayout = new QVBoxLayout(linesPage_);
    linesLayout->setContentsMargins(0, 0, 0, 0);
    linesLayout->setSpacing(0);
    linesLayout->addLayout(top);
    pages_->addWidget(message_);
    pages_->addWidget(linesPage_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pages_);
    createCode(LanguageRegistry::plainText);
    applyTheme(theme);
}

FileLens::~FileLens() = default;

void FileLens::createCode(const QString &language)
{
    highlighter_.reset();
    if (code_)
        code_->deleteLater();
    language_ = language;
    code_ = new ScintillaView(linesPage_);
    code_->setReadOnly(true);
    code_->setFrameShape(QFrame::NoFrame);
    code_->setCaretLineVisible(false);
    hoveredRun_ = -1;
    code_->setMarginType(commitMargin, SC_MARGIN_TEXT);
    code_->setMarginSensitivity(commitMargin, true);
    code_->send(SCI_SETMARGINCURSORN, commitMargin, handCursor);
    // The name under the mouse stands out.
    code_->viewport()->setMouseTracking(true);
    code_->viewport()->installEventFilter(this);
    code_->setMarginType(ageMargin, SC_MARGIN_SYMBOL);
    code_->setMarginWidth(ageMargin, ageMarginPx);
    code_->setMarginMarkerMask(ageMargin, (1 << ageCount) - 1);
    code_->setMarginType(numberMargin, SC_MARGIN_NUMBER);
    code_->setMarginMarkerMask(numberMargin, 0);
    connect(code_, &ScintillaView::marginLineClicked, this, [this](int, int line) {
        if (line >= 0 && line < blame_.lineCommits.size())
            emit commitClicked(blame_.commits.at(blame_.lineCommits.at(line)).id);
    });
    highlighter_ = HighlighterFactory::create(language);
    if (highlighter_)
        highlighter_->attach(code_);
    static_cast<QVBoxLayout *>(linesPage_->layout())->addWidget(code_, 1);
}

void FileLens::showBlame(const QString &path, FileBlame blame)
{
    blame_ = std::move(blame);
    const QString language = LanguageRegistry::languageForFile(path);
    if (language != language_) {
        createCode(language);
        styleCode();
    }
    pages_->setCurrentWidget(linesPage_);
    showLines();
}

void FileLens::showMessage(const QString &message)
{
    message_->setText(message);
    pages_->setCurrentWidget(message_);
}

void FileLens::setChosenCommit(const QString &id)
{
    chosen_ = id;
    track_->setChosen(id);
    markChosenLines();
    showInfo();
}

void FileLens::applyTheme(const Theme &theme)
{
    theme_ = theme;
    track_->applyTheme(theme);
    styleCode();
    showLines();
}

void FileLens::styleCode()
{
    ScintillaStyles::applyDefaultStyle(*code_, theme_);
    if (highlighter_)
        highlighter_->applyTheme(theme_);
    ScintillaStyles::applyTypography(*code_, theme_.font, theme_.lineHeight);
    ScintillaStyles::applyEditorColors(*code_, theme_);
    // The commits' names in the interface's font, dimmed, the one under the
    // mouse bright and underlined; styles of their own, after those of the
    // highlighter.
    code_->send(SCI_RELEASEALLEXTENDEDSTYLES);
    const long offset = code_->send(SCI_ALLOCATEEXTENDEDSTYLES, commitNameStyleCount);
    code_->send(SCI_MARGINSETSTYLEOFFSET, offset);
    const QByteArray family = font().family().toUtf8();
    for (const int style : {commitNameStyle, hoveredNameStyle}) {
        const long scintillaStyle = offset + style;
        code_->sends(SCI_STYLESETFONT, scintillaStyle, family.constData());
        code_->send(SCI_STYLESETSIZE, scintillaStyle, long(std::max(7, theme_.font.pointSize() - 1)));
        code_->send(SCI_STYLESETBACK, scintillaStyle, scintillaColor(theme_.marginBackground));
    }
    code_->send(SCI_STYLESETFORE, offset + commitNameStyle, scintillaColor(theme_.mutedText));
    code_->send(SCI_STYLESETFORE, offset + hoveredNameStyle, scintillaColor(theme_.foreground));
    code_->send(SCI_STYLESETUNDERLINE, offset + hoveredNameStyle, 1L);
    code_->setMarginWidth(commitMargin, QString(commitMarginChars, QLatin1Char('m')));
    for (int age = 0; age < ageCount; ++age) {
        code_->markerDefine(SC_MARK_FULLRECT, age);
        code_->setMarkerBackgroundColor(ageColor(theme_, age), age);
    }
    code_->markerDefine(SC_MARK_BACKGROUND, chosenMarker);
    code_->setMarkerBackgroundColor(blend(theme_.background, theme_.accent, chosenTint), chosenMarker);
}

QList<int> FileLens::ages() const
{
    // By rank of date: a file edited in bursts still uses every color.
    QList<int> order(blame_.commits.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, {}, [this](int commit) { return blame_.commits.at(commit).date; });
    QList<int> result(blame_.commits.size(), ageCount - 1);
    const qsizetype last = order.size() - 1;
    for (qsizetype rank = 0; rank < order.size(); ++rank)
        result[order.at(rank)] = last > 0 ? int(rank * (ageCount - 1) / last) : ageCount - 1;
    return result;
}

void FileLens::showLines()
{
    code_->setText(blame_.lines.join(QLatin1Char('\n')));
    code_->setMarginWidth(numberMargin, QString::number(blame_.lines.size() * 10));
    code_->clearMarginText();
    code_->markerDeleteAll();
    hoveredRun_ = -1;
    const QList<int> age = ages();
    QList<CommitTrack::Mark> marks;
    for (qsizetype commit = 0; commit < blame_.commits.size(); ++commit)
        marks.append({blame_.commits.at(commit), 0, ageColor(theme_, age.at(commit)),
                      CommitDelegate::isMadeWithClaude(blame_.commits.at(commit))});
    for (int line = 0; line < blame_.lineCommits.size(); ++line) {
        const int commit = blame_.lineCommits.at(line);
        ++marks[commit].lineCount;
        code_->markerAdd(line, age.at(commit));
        // Named beside its first line of a run.
        if (runStart(line) == line)
            code_->setMarginText(line, commitName(line), commitNameStyle);
    }
    track_->setMarks(marks);
    markChosenLines();
    showInfo();
}

int FileLens::runStart(int line) const
{
    while (line > 0 && blame_.lineCommits.at(line - 1) == blame_.lineCommits.at(line))
        --line;
    return line;
}

QString FileLens::commitName(int line) const
{
    const CommitSummary &summary = blame_.commits.at(blame_.lineCommits.at(line));
    const QString day = QLocale().toString(summary.date.toLocalTime().date(), QStringLiteral("MMM d"));
    return QStringLiteral("%1  %2 ").arg(day, summary.summary);
}

bool FileLens::eventFilter(QObject *watched, QEvent *event)
{
    if (code_ && watched == code_->viewport()) {
        if (event->type() == QEvent::MouseMove)
            hoverAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        else if (event->type() == QEvent::Leave)
            setHoveredRun(-1);
    }
    return QWidget::eventFilter(watched, event);
}

void FileLens::hoverAt(QPoint position)
{
    const long marginWidth = code_->send(SCI_GETMARGINWIDTHN, commitMargin);
    if (position.x() >= marginWidth) {
        setHoveredRun(-1);
        return;
    }
    const long nearest = code_->send(SCI_POSITIONFROMPOINT, position.x(), position.y());
    const int line = int(code_->send(SCI_LINEFROMPOSITION, nearest));
    // Below the last line, the nearest position is still on it.
    const long lineTop = code_->send(SCI_POINTYFROMPOSITION, 0L, code_->send(SCI_POSITIONFROMLINE, line));
    const bool isOnLine = position.y() < lineTop + code_->send(SCI_TEXTHEIGHT, line);
    const bool isBlamed = line >= 0 && line < blame_.lineCommits.size();
    setHoveredRun(isOnLine && isBlamed ? runStart(line) : -1);
}

void FileLens::setHoveredRun(int line)
{
    if (line == hoveredRun_)
        return;
    if (hoveredRun_ >= 0)
        code_->setMarginText(hoveredRun_, commitName(hoveredRun_), commitNameStyle);
    hoveredRun_ = line;
    if (hoveredRun_ >= 0)
        code_->setMarginText(hoveredRun_, commitName(hoveredRun_), hoveredNameStyle);
}

void FileLens::markChosenLines()
{
    code_->markerDeleteAll(chosenMarker);
    int first = -1;
    for (int line = 0; line < blame_.lineCommits.size(); ++line) {
        if (blame_.commits.at(blame_.lineCommits.at(line)).id != chosen_)
            continue;
        code_->markerAdd(line, chosenMarker);
        if (first < 0)
            first = line;
    }
    if (first >= 0)
        code_->setFirstVisibleLine(std::max(0, first - 3));
}

void FileLens::showInfo()
{
    const auto chosen =
        std::ranges::find_if(blame_.commits, [this](const CommitSummary &commit) { return commit.id == chosen_; });
    const qsizetype lineCount = blame_.lines.size();
    if (chosen == blame_.commits.end()) {
        const qsizetype withClaude = std::ranges::count_if(blame_.lineCommits, [this](int commit) {
            return CommitDelegate::isMadeWithClaude(blame_.commits.at(commit));
        });
        info_->setText(tr("%n commit(s) wrote the %1 lines at HEAD, %2 of them with Claude. Choose a commit to light "
                          "its lines.",
                          nullptr, int(blame_.commits.size()))
                           .arg(lineCount)
                           .arg(withClaude));
        return;
    }
    const int index = int(chosen - blame_.commits.begin());
    const qsizetype ownLines = std::ranges::count(blame_.lineCommits, index);
    info_->setText(tr("<b>%1</b> &nbsp;%2 · %3 &nbsp;— %4 of the %5 lines still from this commit")
                       .arg(chosen->summary.toHtmlEscaped(), chosen->author.toHtmlEscaped(), chosen->shortId)
                       .arg(ownLines)
                       .arg(lineCount));
}
