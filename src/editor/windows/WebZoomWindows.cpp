// SPDX-License-Identifier: GPL-3.0-only

#include "editor/WebZoom.h"

// Chromium zooms on Ctrl+wheel by itself here.
void WebZoom::attach(QWebEngineView * /*view*/)
{
}
