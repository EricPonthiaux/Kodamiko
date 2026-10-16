#pragma once

#include <QFont>

// Fonts embedded in Enlumine, so that it looks the same on every system:
// Inter for the interface, JetBrains Mono for code (editor, terminal,
// diffs). Each falls back on the system's font if it cannot be loaded.
namespace AppFonts {

// Registers the embedded fonts (once); needs the application to exist.
void load();

// The interface font at the system's size.
QFont interfaceFont();
// The code font at `pointSize`, without programming ligatures.
QFont codeFont(qreal pointSize);

// Lines of code are set at this multiple of the font size (Zed's
// "standard" is 1.3, its "comfortable" 1.618).
constexpr qreal codeLineHeight = 1.5;

} // namespace AppFonts
