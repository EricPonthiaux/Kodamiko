// SPDX-License-Identifier: GPL-3.0-only

#include "Graphviz.h"

#include "GraphvizFormat.h"
#include "Style.h"

#include <QFontMetricsF>

#include <gvc.h>
#ifdef DUET_GRAPHVIZ_PLUGINS
#include <gvplugin.h>

extern "C" {
extern gvplugin_library_t gvplugin_core_LTX_library;
extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
extern gvplugin_library_t gvplugin_neato_layout_LTX_library;
}
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace diagram::graphviz {

namespace {

int silent(char *)
{
    return 0;
}

GVC_t *context()
{
    static GVC_t *gvc = [] {
        agseterr(AGMAX);
        agseterrf(silent);
#ifdef DUET_GRAPHVIZ_PLUGINS
        // dot and twopi/neato, and the JSON output: nothing loaded from the
        // system's plugin configuration.
        static const std::array<lt_symlist_t, 4> builtins{{
            {"gvplugin_core_LTX_library", &gvplugin_core_LTX_library},
            {"gvplugin_dot_layout_LTX_library", &gvplugin_dot_layout_LTX_library},
            {"gvplugin_neato_layout_LTX_library", &gvplugin_neato_layout_LTX_library},
            {nullptr, nullptr},
        }};
        return gvContextPlugins(builtins.data(), 0);
#else
        return gvContext();
#endif
    }();
    return gvc;
}

} // namespace

QString dotEscape(const QString &text)
{
    QString result;
    for (const QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            result += QLatin1Char('\\');
        if (c == QLatin1Char('\n'))
            result += QStringLiteral("\\n");
        else
            result += c;
    }
    return result;
}

QString dotHtmlEscape(const QString &text)
{
    QString result = text;
    result.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    result.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    result.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    result.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return result;
}

qreal fontScale()
{
    // The gap between the board's font and Arial is not the same on lower
    // case, capitals, accents and digits: the sample mixes them.
    static const qreal scale = [] {
        const QString sample = QStringLiteral("Réorganiser le schéma ABCDEFGHIJ abcdefghijklm 0123456789");
        const qreal drawn = text::width(Font(100), sample);
        QFont arial(QString::fromLatin1(metricFont));
        arial.setPixelSize(100);
        const qreal estimated = QFontMetricsF(arial).horizontalAdvance(sample);
        if (!(drawn > 0) || !(estimated > 0))
            return 1.0;
        // Only ever reserve more, never less; bounded against odd fonts.
        return std::clamp(drawn / estimated, 1.0, 1.5);
    }();
    return scale;
}

QString dotFontSize(qreal px, qreal scale)
{
    if (!(px > 0))
        return QString::number(px);
    return QString::number(std::round(px * std::clamp(scale, 1.0, 1.5) * 100) / 100);
}

QByteArray layout(const QByteArray &dot, const char *engine, QString *error)
{
    GVC_t *gvc = context();
    Agraph_t *graph = agmemread(dot.constData());
    if (!graph) {
        if (error)
            *error = QStringLiteral("Graphviz could not read the graph");
        return {};
    }
    QByteArray result;
    if (gvLayout(gvc, graph, engine) == 0) {
        char *data = nullptr;
        size_t length = 0;
        if (gvRenderData(gvc, graph, "json", &data, &length) == 0)
            result = QByteArray(data, qsizetype(length));
        gvFreeRenderData(data);
        gvFreeLayout(gvc, graph);
    }
    agclose(graph);
    if (result.isEmpty() && error)
        *error = QStringLiteral("Graphviz could not lay the graph out");
    return result;
}

} // namespace diagram::graphviz
