#pragma once

#include <QFont>

class QWidget;

// Fonts embedded in Duet, so that it looks the same on every system:
// Inter for the interface, JetBrains Mono for code (editor, terminal,
// diffs). Each falls back on the system's font if it cannot be loaded.
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

// Lines of code are set at this multiple of the font size (Zed's
// "standard" is 1.3, its "comfortable" 1.618).
constexpr qreal codeLineHeight = 1.5;

} // namespace AppFonts
