// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class QWebEngineView;

// The zoom of a web view, as in the editors: Ctrl+wheel over it (Command on
// macOS) enlarges or shrinks the page.
namespace WebZoom {

// Makes `view` zoomable with the wheel, where the web engine does not
// already do it on its own.
void attach(QWebEngineView *view);

} // namespace WebZoom
