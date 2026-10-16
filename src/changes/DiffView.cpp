#include "DiffView.h"

#include "syntax/ScintillaStyles.h"

#include <QFontMetrics>

namespace {

// Styles of the text (no lexer: they are set by hand) and of the margin.
enum Style { PlainStyle = 0, FileHeaderStyle = 40, FileNameStyle, HunkHeaderStyle, NoteStyle, NumberStyle, AddedNumberStyle,
             RemovedNumberStyle };
// Markers: full-line backgrounds, and a stronger strip in the side margin.
enum Marker { AddedBackground = 0, RemovedBackground, FileHeaderBackground, HunkHeaderBackground, AddedStrip, RemovedStrip };
constexpr int numberMargin = 0;
constexpr int stripMargin = 1;

QColor mix(const QColor &a, const QColor &b, double share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share);
}

struct DiffColors {
    QColor added;
    QColor removed;
    QColor addedStrong;
    QColor removedStrong;
    QColor fileHeader;
    QColor hunkHeader;
    QColor muted;
};

DiffColors colorsFor(const Theme &theme)
{
    const QColor green = theme.isDark ? QColor("#4caf6e") : QColor("#2da44e");
    const QColor red = theme.isDark ? QColor("#e0596a") : QColor("#cf222e");
    const QColor accent = theme.accent;
    return {mix(theme.background, green, theme.isDark ? 0.16 : 0.13),
            mix(theme.background, red, theme.isDark ? 0.16 : 0.11),
            green,
            red,
            mix(theme.background, accent, theme.isDark ? 0.14 : 0.10),
            mix(theme.background, theme.foreground, 0.05),
            theme.marginForeground};
}

QString linesLabel(int start, int count)
{
    if (count <= 1)
        return DiffView::tr("line %1").arg(start);
    return DiffView::tr("lines %1–%2").arg(start).arg(start + count - 1);
}

} // namespace

DiffView::DiffView(const Theme &theme, QWidget *parent)
    : QsciScintilla(parent)
    , theme_(theme)
{
    setUtf8(true);
    setReadOnly(true);
    setCaretLineVisible(false);
    setWrapMode(QsciScintilla::WrapNone);
    setScrollWidthTracking(true);
    setScrollWidth(1);
    setMarginType(numberMargin, QsciScintilla::TextMarginRightJustified);
    setMarginType(stripMargin, QsciScintilla::SymbolMargin);
    setMarginMarkerMask(numberMargin, 0);
    setMarginMarkerMask(stripMargin, (1 << AddedStrip) | (1 << RemovedStrip));
    setMarginWidth(stripMargin, 4);
    for (int margin = 2; margin < 5; ++margin)
        setMarginWidth(margin, 0);
    applyTheme(theme);
}

QList<DiffView::Line> DiffView::layout(const QList<DiffFile> &files)
{
    QList<Line> lines;
    for (int f = 0; f < files.size(); ++f) {
        const DiffFile &file = files[f];
        // A blank line before each file: they breathe.
        Line spacer;
        spacer.kind = LineKind::Spacer;
        lines.append(spacer);
        // "Name.cpp   src/folder · modified", the name brighter.
        const qsizetype slash = file.path.lastIndexOf(QLatin1Char('/'));
        const QString name = file.path.mid(slash + 1);
        QStringList details;
        if (slash >= 0)
            details.append(file.path.left(slash));
        if (!file.status.isEmpty())
            details.append(file.status);
        Line title;
        title.kind = LineKind::FileHeader;
        title.text = details.isEmpty() ? name : name + QStringLiteral("   ") + details.join(QStringLiteral("  ·  "));
        title.emphasis = static_cast<int>(name.size());
        title.location = {f, -1, -1};
        lines.append(title);

        const FilePatch &patch = file.patch;
        if (patch.hunks.isEmpty()) {
            const bool binary = std::any_of(patch.header.begin(), patch.header.end(), [](const QByteArray &line) {
                return line.startsWith("Binary files") || line.startsWith("GIT binary patch");
            });
            Line note;
            note.kind = LineKind::Note;
            note.text = binary ? tr("Binary file") : tr("No changes to the text");
            note.location = {f, -1, -1};
            lines.append(note);
            continue;
        }
        for (int h = 0; h < patch.hunks.size(); ++h) {
            const DiffHunk &hunk = patch.hunks[h];
            Line separator;
            separator.kind = LineKind::HunkHeader;
            // Numbers of the new version, unless the block only removes.
            separator.text = QStringLiteral("⋯ ")
                + (hunk.newCount > 0 ? linesLabel(hunk.newStart, hunk.newCount) : linesLabel(hunk.oldStart, hunk.oldCount));
            if (const QString heading = QString::fromUtf8(hunk.heading).trimmed(); !heading.isEmpty())
                separator.text += QStringLiteral("  ·  ") + heading;
            separator.location = {f, h, -1};
            lines.append(separator);

            int oldNumber = hunk.oldStart;
            int newNumber = hunk.newStart;
            for (int l = 0; l < hunk.lines.size(); ++l) {
                const QByteArray &raw = hunk.lines[l];
                Line line;
                line.location = {f, h, l};
                QString text = QString::fromUtf8(raw.mid(1));
                if (text.endsWith(QLatin1Char('\r')))
                    text.chop(1);
                line.text = text;
                if (raw.startsWith('+')) {
                    line.kind = LineKind::Added;
                    line.newNumber = newNumber++;
                } else if (raw.startsWith('-')) {
                    line.kind = LineKind::Removed;
                    line.oldNumber = oldNumber++;
                } else if (raw.startsWith('\\')) {
                    line.kind = LineKind::Note;
                    line.text = tr("No line break at the end of the file");
                } else {
                    line.kind = LineKind::Context;
                    line.oldNumber = oldNumber++;
                    line.newNumber = newNumber++;
                }
                lines.append(line);
            }
        }
    }
    return lines;
}

void DiffView::setFiles(const QList<DiffFile> &files)
{
    lines_ = layout(files);
    fileStarts_.clear();
    int largest = 0;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind == LineKind::FileHeader)
            fileStarts_.append(i);
        largest = std::max({largest, lines_[i].oldNumber, lines_[i].newNumber});
    }
    numberWidth_ = std::max(3, static_cast<int>(QString::number(largest).size()));
    render();
}

void DiffView::showMessage(const QString &message)
{
    lines_.clear();
    fileStarts_.clear();
    if (!message.isEmpty()) {
        Line note;
        note.kind = LineKind::Note;
        note.text = message;
        lines_.append(note);
    }
    render();
}

DiffView::Location DiffView::locate(int line) const
{
    return line >= 0 && line < lines_.size() ? lines_[line].location : Location{};
}

int DiffView::fileStart(int file) const
{
    return fileStarts_.value(file, 0);
}

void DiffView::render()
{
    QStringList texts;
    texts.reserve(lines_.size());
    for (const Line &line : std::as_const(lines_))
        texts.append(line.text);
    setReadOnly(false);
    setText(texts.join(QLatin1Char('\n')));
    setReadOnly(true);
    setModified(false);

    markerDeleteAll();
    clearMarginText();
    // Margin: "old new" numbers, and a sign the colors repeat.
    const QString blank(numberWidth_, QLatin1Char(' '));
    const auto number = [this, &blank](int value) {
        return value > 0 ? QString::number(value).rightJustified(numberWidth_) : blank;
    };
    for (int i = 0; i < lines_.size(); ++i) {
        const Line &line = lines_[i];
        const long start = SendScintilla(SCI_POSITIONFROMLINE, i);
        const long length = SendScintilla(SCI_LINELENGTH, i);
        int style = PlainStyle;
        switch (line.kind) {
        case LineKind::Spacer:
            break;
        case LineKind::FileHeader:
            style = FileHeaderStyle;
            markerAdd(i, FileHeaderBackground);
            break;
        case LineKind::HunkHeader:
            style = HunkHeaderStyle;
            markerAdd(i, HunkHeaderBackground);
            break;
        case LineKind::Note:
            style = NoteStyle;
            break;
        case LineKind::Added:
            markerAdd(i, AddedBackground);
            markerAdd(i, AddedStrip);
            setMarginText(i, QStringLiteral("%1 %2 +").arg(blank, number(line.newNumber)), AddedNumberStyle);
            break;
        case LineKind::Removed:
            markerAdd(i, RemovedBackground);
            markerAdd(i, RemovedStrip);
            setMarginText(i, QStringLiteral("%1 %2 −").arg(number(line.oldNumber), blank), RemovedNumberStyle);
            break;
        case LineKind::Context:
            setMarginText(i, QStringLiteral("%1 %2  ").arg(number(line.oldNumber), number(line.newNumber)), NumberStyle);
            break;
        }
        SendScintilla(SCI_STARTSTYLING, start);
        SendScintilla(SCI_SETSTYLING, length, style);
        if (line.emphasis > 0) {
            SendScintilla(SCI_STARTSTYLING, start);
            SendScintilla(SCI_SETSTYLING, line.text.left(line.emphasis).toUtf8().size(), FileNameStyle);
        }
    }
    // No numbers to show (a message): no margin.
    const bool numbered = std::any_of(lines_.begin(), lines_.end(),
                                      [](const Line &line) { return line.oldNumber > 0 || line.newNumber > 0; });
    const QFontMetrics metrics(theme_.font);
    setMarginWidth(numberMargin, numbered ? metrics.horizontalAdvance(QString(2 * numberWidth_ + 5, QLatin1Char('9'))) : 8);
}

void DiffView::defineStyles()
{
    const DiffColors colors = colorsFor(theme_);
    QFont bold = theme_.font;
    bold.setBold(true);
    QFont italic = theme_.font;
    italic.setItalic(true);
    const auto define = [this](int style, const QColor &foreground, const QColor &background, const QFont &font) {
        SendScintilla(SCI_STYLESETFORE, style, foreground);
        SendScintilla(SCI_STYLESETBACK, style, background);
        SendScintilla(SCI_STYLESETFONT, style, font.family().toUtf8().constData());
        SendScintilla(SCI_STYLESETSIZE, style, font.pointSize());
        SendScintilla(SCI_STYLESETBOLD, style, font.bold());
        SendScintilla(SCI_STYLESETITALIC, style, font.italic());
    };
    // The default style first: STYLECLEARALL copies it to every style.
    define(STYLE_DEFAULT, theme_.foreground, theme_.background, theme_.font);
    SendScintilla(SCI_STYLECLEARALL);
    define(PlainStyle, theme_.foreground, theme_.background, theme_.font);
    define(FileHeaderStyle, colors.muted, colors.fileHeader, theme_.font);
    define(FileNameStyle, theme_.foreground, colors.fileHeader, bold);
    define(HunkHeaderStyle, colors.muted, colors.hunkHeader, italic);
    define(NoteStyle, colors.muted, theme_.background, italic);
    define(NumberStyle, theme_.marginForeground, theme_.marginBackground, theme_.font);
    define(AddedNumberStyle, colors.addedStrong, colors.added, theme_.font);
    define(RemovedNumberStyle, colors.removedStrong, colors.removed, theme_.font);
    define(STYLE_LINENUMBER, theme_.marginForeground, theme_.marginBackground, theme_.font);

    const auto background = [this](int marker, const QColor &color) {
        markerDefine(QsciScintilla::Background, marker);
        setMarkerBackgroundColor(color, marker);
    };
    background(AddedBackground, colors.added);
    background(RemovedBackground, colors.removed);
    background(FileHeaderBackground, colors.fileHeader);
    background(HunkHeaderBackground, colors.hunkHeader);
    markerDefine(QsciScintilla::FullRectangle, AddedStrip);
    setMarkerBackgroundColor(colors.addedStrong, AddedStrip);
    markerDefine(QsciScintilla::FullRectangle, RemovedStrip);
    setMarkerBackgroundColor(colors.removedStrong, RemovedStrip);
}

void DiffView::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setPaper(theme.background);
    setColor(theme.foreground);
    setFont(theme.font);
    setCaretForegroundColor(theme.caret);
    setSelectionBackgroundColor(theme.selection);
    SendScintilla(SCI_SETSELFORE, false, 0L);
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    ScintillaStyles::applyTypography(*this, theme.font);
    defineStyles();
    render(); // the styles of the text are set line by line
}
