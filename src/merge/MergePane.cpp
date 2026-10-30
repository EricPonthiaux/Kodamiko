// SPDX-License-Identifier: GPL-3.0-only

#include "MergePane.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"

#include <array>

namespace {

constexpr int numberMargin = 0;
constexpr int stripMargin = 1;
constexpr int stripWidthPx = 3;

// A background marker and a strip marker per shade but None, in the order
// of MergePane::Shade.
constexpr int shadeCount = 5;
constexpr int firstBackgroundMarker = 0;
constexpr int firstStripMarker = firstBackgroundMarker + shadeCount;

using Shade = MergePane::Shade;

QColor mix(const QColor &a, const QColor &b, double share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share);
}

// The color of each shade but None, strong as in a strip; blue and orange
// tell the sides apart in lightness too.
std::array<QColor, shadeCount> shadeColors(const Theme &theme)
{
    return {theme.isDark ? QColor("#6ea0ff") : QColor("#0969da"), theme.isDark ? QColor("#f0b45a") : QColor("#9a6700"),
            theme.isDark ? QColor("#39c5cf") : QColor("#1b7c83"), theme.isDark ? QColor("#ff6b6b") : QColor("#cf222e"),
            theme.accent};
}

int markerIndex(Shade shade)
{
    switch (shade) {
    case Shade::None:
        return -1;
    case Shade::Current:
        return 0;
    case Shade::Incoming:
        return 1;
    case Shade::Merged:
        return 2;
    case Shade::Conflict:
        return 3;
    case Shade::Edited:
        return 4;
    }
    return -1;
}

} // namespace

MergePane::MergePane(const Theme &theme, const QString &path, QWidget *parent)
    : QsciScintilla(parent)
    , theme_(theme)
    , highlighter_(HighlighterFactory::create(LanguageRegistry::languageForFile(path)))
{
    setUtf8(true);
    setReadOnly(true);
    setCaretLineVisible(false);
    setWrapMode(QsciScintilla::WrapNone);
    setScrollWidthTracking(true);
    setScrollWidth(1);
    setFrameShape(QFrame::NoFrame);
    setMarginType(numberMargin, QsciScintilla::NumberMargin);
    setMarginType(stripMargin, QsciScintilla::SymbolMargin);
    setMarginMarkerMask(numberMargin, 0);
    setMarginMarkerMask(stripMargin, ((1 << shadeCount) - 1) << firstStripMarker);
    setMarginWidth(stripMargin, stripWidthPx);
    paddingStyle_ = int(SendScintilla(SCI_ALLOCATEEXTENDEDSTYLES, 1L));
    SendScintilla(SCI_ANNOTATIONSETSTYLEOFFSET, long(paddingStyle_));
    SendScintilla(SCI_ANNOTATIONSETVISIBLE, long(ANNOTATION_STANDARD));
    if (highlighter_)
        highlighter_->attach(this);
    applyTheme(theme);
}

MergePane::~MergePane() = default;

void MergePane::showStretches(const QList<Stretch> &stretches)
{
    QByteArray text;
    for (const Stretch &stretch : stretches)
        text += stretch.lines.join();
    setReadOnly(false);
    SendScintilla(SCI_SETTEXT, text.constData());
    setReadOnly(true);
    SendScintilla(SCI_ANNOTATIONCLEARALL);
    markerDeleteAll();

    int line = 0;
    for (const Stretch &stretch : stretches) {
        const int count = int(stretch.lines.size());
        if (const int marker = markerIndex(stretch.shade); marker >= 0) {
            for (int i = line; i < line + count; ++i) {
                markerAdd(i, firstBackgroundMarker + marker);
                markerAdd(i, firstStripMarker + marker);
            }
        }
        // Under the stretch's last line; a stretch without lines here has
        // its rows under the line before it.
        addPadding(line + count - 1, stretch.rows - count);
        line += count;
    }
    const int digits = int(QString::number(std::max(lines(), 1)).size());
    setMarginWidth(numberMargin, QString(digits + 1, QLatin1Char('9')));
}

void MergePane::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setPaper(theme.background);
    setColor(theme.foreground);
    setFont(theme.font);
    setSelectionBackgroundColor(theme.selection);
    ScintillaStyles::applyTypography(*this, theme.font, theme.lineHeight);
    // The highlighter resets every style: the margins' come after.
    if (highlighter_)
        highlighter_->applyTheme(theme);
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    defineShades();
}

void MergePane::defineShades()
{
    const std::array<QColor, shadeCount> colors = shadeColors(theme_);
    const double backgroundShare = theme_.isDark ? 0.16 : 0.12;
    for (int i = 0; i < shadeCount; ++i) {
        markerDefine(QsciScintilla::Background, firstBackgroundMarker + i);
        setMarkerBackgroundColor(mix(theme_.background, colors[i], backgroundShare), firstBackgroundMarker + i);
        markerDefine(QsciScintilla::FullRectangle, firstStripMarker + i);
        setMarkerBackgroundColor(colors[i], firstStripMarker + i);
    }
    // The blank rows: the background a shade darker or lighter.
    const QColor padding = mix(theme_.background, theme_.foreground, theme_.isDark ? 0.035 : 0.03);
    SendScintilla(SCI_STYLESETBACK, long(paddingStyle_), padding);
    SendScintilla(SCI_STYLESETFORE, long(paddingStyle_), padding);
}

void MergePane::addPadding(int line, int rows)
{
    // At the top, a stretch without lines has no line above to hang its
    // rows under: the columns stay aligned from the next stretch on only.
    if (rows <= 0 || line < 0)
        return;
    const QByteArray existing = annotation(line).toUtf8();
    const int existingRows = existing.isEmpty() ? 0 : int(existing.count('\n')) + 1;
    const QByteArray blank = QByteArray(" ") + QByteArray("\n ").repeated(existingRows + rows - 1);
    SendScintilla(SCI_ANNOTATIONSETTEXT, long(line), blank.constData());
    SendScintilla(SCI_ANNOTATIONSETSTYLE, long(line), 0L);
}
