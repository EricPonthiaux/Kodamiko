#pragma once

#include <QFont>
#include <QList>
#include <QStringList>

class QWidget;

// Fonts embedded in Duet, so that it looks the same on every system:
// Inter for the interface, JetBrains Mono for code (editor, terminal,
// diffs), in Light, Regular, Medium and Bold. Each falls back on the
// system's font if it cannot be loaded.
namespace AppFonts {

// Registers the embedded fonts (once); needs the application to exist.
void load();

// The interface font at the system's size (not the application font's,
// which follows Preferences).
QFont interfaceFont();
// Gives `widget` the application font `scale` times larger, in `weight`
// and spaced by `letterSpacing` pixels; and again whenever the application
// font changes (Preferences ▸ Appearance), which a font set once would not
// follow.
void setInterfaceFont(QWidget *widget, qreal scale, QFont::Weight weight = QFont::DemiBold,
                      qreal letterSpacing = 0);

// The code font at `pointSize`, without programming ligatures.
QFont codeFont(qreal pointSize);
// Whether `family` is the embedded code font ("JetBrains Mono NL", or its
// version with ligatures).
bool isBuiltInCode(const QString &family);

// `font` with its programming ligatures ("->" drawn as an arrow) on or
// off. The embedded font comes in two versions: "NL" without them, the
// other with them. For other fonts, the OpenType features turn them on or
// off (Qt 6.7 and later).
QFont withLigatures(QFont font, bool ligatures);
// The family to give Scintilla for `font`: Scintilla builds its fonts from
// the family name alone, so "#liga" at its end turns the ligatures on (see
// cmake/QScintilla.cmake); they are off otherwise.
QByteArray scintillaFamily(const QFont &font, bool ligatures);

// Lines of code are set at a multiple of the font size (Zed's "standard"
// is 1.3, its "comfortable" 1.618), chosen in Preferences ▸ Appearance.
constexpr qreal defaultLineHeight = 1.5;
constexpr qreal minimumLineHeight = 1.0;
constexpr qreal maximumLineHeight = 2.2;

// Code fonts that read well, offered first in Preferences ▸ Appearance;
// none is embedded.
struct RecommendedFont {
    QString name;          // as shown
    QStringList families;  // the names it is installed under
    QString url;           // where to get it
};
QList<RecommendedFont> recommendedFonts();
// The family `font` is installed under, or an empty string.
QString installedFamily(const RecommendedFont &font);

} // namespace AppFonts
