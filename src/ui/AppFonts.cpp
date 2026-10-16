#include "AppFonts.h"

#include <QFontDatabase>
#include <QGuiApplication>

namespace AppFonts {
namespace {

QString interfaceFamily_;
QString codeFamily_;

QString addFont(const QString &path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    return id >= 0 ? QFontDatabase::applicationFontFamilies(id).value(0) : QString();
}

} // namespace

void load()
{
    static bool loaded = false;
    if (loaded || !QGuiApplication::instance())
        return;
    loaded = true;
    interfaceFamily_ = addFont(QStringLiteral(":/fonts/Inter-Variable.ttf"));
    // The "NL" version has no programming ligatures: each character keeps
    // its own cell, which the editor's caret and the terminal rely on.
    codeFamily_ = addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Regular.ttf"));
    for (const char *style : {"Italic", "Bold", "BoldItalic"})
        addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-%1.ttf").arg(QLatin1String(style)));
}

QFont interfaceFont()
{
    load();
    QFont font = QGuiApplication::font();
    if (!interfaceFamily_.isEmpty())
        font.setFamilies({interfaceFamily_, font.family()});
    return font;
}

QFont codeFont(qreal pointSize)
{
    load();
    QFont font = codeFamily_.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(codeFamily_);
    font.setPointSizeF(pointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

} // namespace AppFonts
