#include "AppFonts.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QWidget>

namespace AppFonts {
namespace {

// The families of the embedded fonts, empty until load() registers them.
struct Families {
    QString interface;
    QString code;     // without ligatures
    QString ligature; // the same with them
};

Families &embedded()
{
    static Families families;
    return families;
}

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
    embedded().interface = addFont(QStringLiteral(":/fonts/Inter-Variable.ttf"));
    // The "NL" version has no programming ligatures: the terminal and, by
    // default, the editor use it. The other one is for the editor when the
    // ligatures are on.
    embedded().code = addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Regular.ttf"));
    embedded().ligature = addFont(QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));
    for (const char *style : {"Italic", "Light", "LightItalic", "Medium", "MediumItalic", "Bold", "BoldItalic"}) {
        addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-%1.ttf").arg(QLatin1String(style)));
        addFont(QStringLiteral(":/fonts/JetBrainsMono-%1.ttf").arg(QLatin1String(style)));
    }
}

QFont interfaceFont()
{
    load();
    QFont font = QGuiApplication::font();
    if (const qreal systemSize = QFontDatabase::systemFont(QFontDatabase::GeneralFont).pointSizeF(); systemSize > 0)
        font.setPointSizeF(systemSize);
    if (const QString &family = embedded().interface; !family.isEmpty())
        font.setFamilies({family, font.family()});
    return font;
}

void setInterfaceFont(QWidget *widget, qreal scale, QFont::Weight weight, qreal letterSpacing)
{
    const auto apply = [widget, scale, weight, letterSpacing] {
        QFont font = QGuiApplication::font();
        font.setPointSizeF(font.pointSizeF() * scale);
        font.setWeight(weight);
        if (letterSpacing != 0)
            font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
        widget->setFont(font);
    };
    apply();
    QObject::connect(qGuiApp, &QGuiApplication::fontChanged, widget, apply);
}

QFont codeFont(qreal pointSize)
{
    load();
    const QString &family = embedded().code;
    QFont font = family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(family);
    font.setPointSizeF(pointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

bool isBuiltInCode(const QString &family)
{
    load();
    return !family.isEmpty() && (family == embedded().code || family == embedded().ligature);
}

QFont withLigatures(QFont font, bool ligatures)
{
    load();
    if (isBuiltInCode(font.family()))
        font.setFamily(ligatures ? embedded().ligature : embedded().code);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setFeature(QFont::Tag("liga"), ligatures ? 1 : 0);
    font.setFeature(QFont::Tag("calt"), ligatures ? 1 : 0);
#endif
    return font;
}

QByteArray scintillaFamily(const QFont &font, bool ligatures)
{
    QByteArray family = withLigatures(font, ligatures).family().toUtf8();
    if (ligatures)
        family += "#liga";
    return family;
}

QList<RecommendedFont> recommendedFonts()
{
    const QString monaspace = QStringLiteral("https://monaspace.githubnext.com");
    return {
        {QStringLiteral("Geist Mono"), {QStringLiteral("Geist Mono")}, QStringLiteral("https://vercel.com/font")},
        {QStringLiteral("Commit Mono"),
         {QStringLiteral("CommitMono"), QStringLiteral("Commit Mono")},
         QStringLiteral("https://commitmono.com")},
        {QStringLiteral("Monaspace Neon"),
         {QStringLiteral("Monaspace Neon"), QStringLiteral("Monaspace Neon Var")},
         monaspace},
        {QStringLiteral("Monaspace Argon"),
         {QStringLiteral("Monaspace Argon"), QStringLiteral("Monaspace Argon Var")},
         monaspace},
        {QStringLiteral("Monaspace Xenon"),
         {QStringLiteral("Monaspace Xenon"), QStringLiteral("Monaspace Xenon Var")},
         monaspace},
        {QStringLiteral("Monaspace Radon"),
         {QStringLiteral("Monaspace Radon"), QStringLiteral("Monaspace Radon Var")},
         monaspace},
        {QStringLiteral("Monaspace Krypton"),
         {QStringLiteral("Monaspace Krypton"), QStringLiteral("Monaspace Krypton Var")},
         monaspace},
        {QStringLiteral("Iosevka"),
         {QStringLiteral("Iosevka"), QStringLiteral("Iosevka Term")},
         QStringLiteral("https://typeof.net/Iosevka/")},
    };
}

QString installedFamily(const RecommendedFont &font)
{
    const QStringList installed = QFontDatabase::families();
    for (const QString &family : font.families) {
        for (const QString &candidate : installed) {
            if (candidate.compare(family, Qt::CaseInsensitive) == 0)
                return candidate;
        }
    }
    return {};
}

} // namespace AppFonts
