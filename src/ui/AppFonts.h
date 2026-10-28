// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFont>
#include <QList>
#include <QStringList>

#include <functional>

class QObject;
class QWidget;

// Fonts embedded in Kodamiko, so that it looks the same on every system:
// Ubuntu Sans for the interface (menus, panels, trees), Droid Sans Mono
// for code (editor, terminal, diffs); Ubuntu Sans Mono and JetBrains Mono
// are also offered, the latter with and without programming ligatures.
// Each default falls back on the system's font if it cannot be loaded.
namespace AppFonts {

// Registers the embedded fonts (once); needs the application to exist.
void load();

// The interface font's size unless Preferences ▸ Fonts sets one; the
// size UiMetrics gives IntelliJ's sizes for.
constexpr int defaultInterfaceSize = 11;

// The interface font at the system's size (not the application font's,
// which follows Preferences).
QFont interfaceFont();
// Gives `widget` the application font `scale` times larger, in `weight`
// and spaced by `letterSpacing` pixels; and again whenever the application
// font changes (Preferences ▸ Fonts), which a font set once would not
// follow.
void setInterfaceFont(QWidget *widget, qreal scale, QFont::Weight weight = QFont::DemiBold, qreal letterSpacing = 0);
// The font of what a mode's title bar names (its title, "History of a.txt",
// the chosen commit): the interface's size in demibold, as the file name of
// a diff's header, so that every bar at the top reads alike.
void setViewTitleFont(QWidget *widget);
// Calls `apply` now and whenever the application font changes, as long as
// `owner` lives: for what is sized from the font (UiMetrics).
void followApplicationFont(QObject *owner, std::function<void()> apply);

// The code font at `pointSize`, without programming ligatures.
QFont codeFont(qreal pointSize);

// The sizes of Preferences ▸ Fonts are points of 1/96 inch, as Linux and
// Windows draw them, so that Kodamiko looks the same on every system; macOS
// draws a point at 1/72 inch, a quarter smaller. The size Qt is given for
// `kdkPoints` on this screen, and back.
qreal systemPoints(qreal kdkPoints);
qreal kdkPoints(qreal systemPoints);
// Whether `family` is an embedded code font (Droid Sans Mono, Ubuntu Sans
// Mono, "JetBrains Mono NL" or its version with ligatures).
bool isBuiltInCode(const QString &family);
// The family of the embedded Ubuntu Sans Mono.
QString ubuntuSansMono();
// The family of the embedded JetBrains Mono, without ligatures.
QString jetBrainsMono();

// `font` with its programming ligatures ("->" drawn as an arrow) on or
// off. The embedded JetBrains Mono comes in two versions: "NL" without
// them, the other with them. For other fonts, the OpenType features turn
// them on or off (Qt 6.7 and later); Droid Sans Mono and Ubuntu Sans Mono
// have none.
QFont withLigatures(QFont font, bool ligatures);
// The family to give Scintilla for `font`: Scintilla builds its fonts from
// the family name alone, so "#liga" at its end turns the ligatures on (see
// cmake/QScintilla.cmake); they are off otherwise.
QByteArray scintillaFamily(const QFont &font, bool ligatures);

// Lines of code are set at a multiple of the font size (Zed's "standard"
// is 1.3, its "comfortable" 1.618), chosen in Preferences ▸ Fonts.
constexpr qreal defaultLineHeight = 1.5;
constexpr qreal minimumLineHeight = 1.0;
constexpr qreal maximumLineHeight = 2.2;

// Code fonts that read well, offered first in Preferences ▸ Fonts;
// none is embedded.
struct RecommendedFont {
    QString name;         // as shown
    QStringList families; // the names it is installed under
    QString url;          // where to get it
};

QList<RecommendedFont> recommendedFonts();
// The family `font` is installed under, or an empty string.
QString installedFamily(const RecommendedFont &font);

} // namespace AppFonts
