// SPDX-License-Identifier: GPL-3.0-only

#include "AppFonts.h"

#include "system/Host.h"

#include <QEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <functional>
#include <utility>

namespace AppFonts {
namespace {

// The families of the embedded fonts, empty until load() registers them.
struct Families {
    QString interface;
    QString code;
    QString ubuntuMono;
    QString jetBrains;         // without ligatures
    QString jetBrainsLigature; // the same with them
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

// Calls `apply` each time the application font changes, as long as its
// parent lives. QGuiApplication sends itself ApplicationFontChange, so the
// filter watches the application object.
class ApplicationFontFollower : public QObject {
public:
    ApplicationFontFollower(QObject *parent, std::function<void()> apply)
        : QObject(parent)
        , apply_(std::move(apply))
    {
        qGuiApp->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const bool isApplicationFontChange = event->type() == QEvent::ApplicationFontChange && watched == qGuiApp;
        if (isApplicationFontChange)
            apply_();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> apply_;
};

} // namespace

void load()
{
    static bool loaded = false;
    if (loaded || !QGuiApplication::instance())
        return;
    loaded = true;
    embedded().interface = addFont(QStringLiteral(":/fonts/UbuntuSans-Variable.ttf"));
    addFont(QStringLiteral(":/fonts/UbuntuSans-VariableItalic.ttf"));
    embedded().code = addFont(QStringLiteral(":/fonts/DroidSansMono.ttf"));
    embedded().ubuntuMono = addFont(QStringLiteral(":/fonts/UbuntuSansMono-Variable.ttf"));
    addFont(QStringLiteral(":/fonts/UbuntuSansMono-VariableItalic.ttf"));
    // When JetBrains Mono is selected, the terminal and, by default, the
    // editor use the "NL" version without programming ligatures. The other
    // one is for the editor when the ligatures are on.
    embedded().jetBrains = addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Regular.ttf"));
    embedded().jetBrainsLigature = addFont(QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));
    for (const char *style : {"Italic", "Light", "LightItalic", "Medium", "MediumItalic", "Bold", "BoldItalic"}) {
        addFont(QStringLiteral(":/fonts/JetBrainsMonoNL-%1.ttf").arg(QLatin1String(style)));
        addFont(QStringLiteral(":/fonts/JetBrainsMono-%1.ttf").arg(QLatin1String(style)));
    }
}

int defaultFontSize()
{
    switch (Host::operatingSystem()) {
    case OperatingSystem::MacOS:
        return 14;
    case OperatingSystem::Windows:
    case OperatingSystem::Linux:
        break;
    }
    return 11;
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
    followApplicationFont(widget, apply);
}

void setViewTitleFont(QWidget *widget)
{
    setInterfaceFont(widget, 1, QFont::DemiBold);
}

void followApplicationFont(QObject *owner, std::function<void()> apply)
{
    apply();
    new ApplicationFontFollower(owner, std::move(apply)); // owned by `owner`
}

namespace {

// The points of 1/96 inch in one of the system's: the screen's logical
// DPI is the one Qt draws text at (72 on macOS, 96 elsewhere and in the
// offscreen platform the tests run on).
qreal systemPointsPerKDKPoint()
{
    constexpr qreal kdkDpi = 96;
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal systemDpi = screen ? screen->logicalDotsPerInchY() : kdkDpi;
    return systemDpi > 0 ? kdkDpi / systemDpi : 1;
}

} // namespace

qreal systemPoints(qreal kdkPoints)
{
    return kdkPoints * systemPointsPerKDKPoint();
}

qreal kdkPoints(qreal systemPoints)
{
    return systemPoints / systemPointsPerKDKPoint();
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
    const Families &fonts = embedded();
    return !family.isEmpty() && (family == fonts.code || family == fonts.ubuntuMono || family == fonts.jetBrains ||
                                 family == fonts.jetBrainsLigature);
}

QString ubuntuSansMono()
{
    load();
    return embedded().ubuntuMono;
}

QString jetBrainsMono()
{
    load();
    return embedded().jetBrains;
}

QFont withLigatures(QFont font, bool ligatures)
{
    load();
    const bool isJetBrains = !font.family().isEmpty() &&
                             (font.family() == embedded().jetBrains || font.family() == embedded().jetBrainsLigature);
    if (isJetBrains)
        font.setFamily(ligatures ? embedded().jetBrainsLigature : embedded().jetBrains);
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
