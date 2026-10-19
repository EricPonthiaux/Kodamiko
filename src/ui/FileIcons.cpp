#include "FileIcons.h"

#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>
#include <array>
#include <utility>

namespace {

struct Badge {
    QString label; // empty: no badge
    QColor color;
};

// Kind → badge. Colors chosen to stay readable on both backgrounds.
Badge badgeOf(const QString &kind)
{
    static const QHash<QString, Badge> badges = {
        {"c", {"C", QColor("#5c8fd6")}},           {"cpp", {"C+", QColor("#5c8fd6")}},
        {"header", {"H", QColor("#a97bd8")}},      {"java", {"J", QColor("#e0763b")}},
        {"javascript", {"JS", QColor("#d6b42c")}}, {"jsx", {"JX", QColor("#d6b42c")}},
        {"typescript", {"TS", QColor("#3d8fd6")}}, {"tsx", {"TX", QColor("#3d8fd6")}},
        {"json", {"{}", QColor("#d68a3d")}},       {"markdown", {"M", QColor("#6aa0c8")}},
        {"cmake", {"▲", QColor("#4caf7d")}},       {"python", {"Py", QColor("#4f86c6")}},
        {"shell", {"$", QColor("#7fae5a")}},       {"yaml", {"Y", QColor("#c46a8f")}},
        {"markup", {"<>", QColor("#e06c55")}},     {"css", {"#", QColor("#5a8fd6")}},
        {"config", {"≡", QColor("#8a8fa3")}},      {"diagram", {"◇", QColor("#b07be0")}},
        {"lock", {"L", QColor("#8a8fa3")}},        {"git", {"G", QColor("#e0613f")}},
    };
    return badges.value(kind);
}

QPainterPath pageOutline(const QRectF &rect, qreal fold)
{
    QPainterPath path;
    path.moveTo(rect.left(), rect.top() + 1);
    path.quadTo(rect.left(), rect.top(), rect.left() + 1, rect.top());
    path.lineTo(rect.right() - fold, rect.top());
    path.lineTo(rect.right(), rect.top() + fold);
    path.lineTo(rect.right(), rect.bottom() - 1);
    path.quadTo(rect.right(), rect.bottom(), rect.right() - 1, rect.bottom());
    path.lineTo(rect.left() + 1, rect.bottom());
    path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - 1);
    path.closeSubpath();
    return path;
}

// The icons are drawn on a 16-unit square.
constexpr qreal iconUnits = 16;

// An icon drawn at the size asked for: sharp at the tree's size (the
// text's height), where a 16-pixel picture would be scaled and blurred.
class PaintedIconEngine : public QIconEngine {
public:
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

protected:
    // `painter` set up to draw on the 16-unit square, centered in `rect`.
    static void enterUnits(QPainter &painter, const QRect &rect)
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const qreal side = std::min(rect.width(), rect.height());
        painter.translate(rect.left() + (rect.width() - side) / 2, rect.top() + (rect.height() - side) / 2);
        painter.scale(side / iconUnits, side / iconUnits);
    }
};

using Draw = void (*)(QPainter &, const QString &, bool);

class DrawnIconEngine : public PaintedIconEngine {
public:
    DrawnIconEngine(QString kind, bool dark, Draw draw)
        : kind_(std::move(kind))
        , dark_(dark)
        , draw_(draw)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override
    {
        painter->save();
        enterUnits(*painter, rect);
        draw_(*painter, kind_, dark_);
        painter->restore();
    }

    QIconEngine *clone() const override { return new DrawnIconEngine(*this); }

private:
    QString kind_;
    bool dark_;
    Draw draw_;
};

// `base` with an orange dot (Claude's color) in its corner.
class ClaudeDotEngine : public PaintedIconEngine {
public:
    explicit ClaudeDotEngine(QIcon base)
        : base_(std::move(base))
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        base_.paint(painter, rect, Qt::AlignCenter, mode, state);
        painter->save();
        enterUnits(*painter, rect);
        painter->setPen(QPen(QGuiApplication::palette().color(QPalette::Base), 1.2));
        painter->setBrush(QColor("#d97757"));
        painter->drawEllipse(QRectF(8.5, 8.5, 7, 7));
        painter->restore();
    }

    QIconEngine *clone() const override { return new ClaudeDotEngine(*this); }

private:
    QIcon base_;
};

QIcon render(const QString &kind, bool dark, Draw draw)
{
    return QIcon(new DrawnIconEngine(kind, dark, draw)); // the icon owns its engine
}

// A neutral gray, readable on every theme's side bar.
QColor lineColor(bool dark)
{
    return dark ? QColor("#a1a5ae") : QColor("#6c707e");
}

void drawPage(QPainter &painter, const QString &kind, bool dark)
{
    const Badge badge = badgeOf(kind);
    const QRectF page(2.5, 1.5, 10, 13);
    painter.setPen(QPen(lineColor(dark), 1));
    painter.setBrush(dark ? QColor(255, 255, 255, 14) : QColor(255, 255, 255, 200));
    painter.drawPath(pageOutline(page, 3));
    painter.drawLine(QPointF(page.right() - 3, page.top()), QPointF(page.right() - 3, page.top() + 3));
    painter.drawLine(QPointF(page.right() - 3, page.top() + 3), QPointF(page.right(), page.top() + 3));

    if (kind == QLatin1String("image")) {
        // Mountain and sun.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#4caf7d"));
        const std::array<QPointF, 5> mountain{{{4, 13}, {7, 8.5}, {9, 11}, {10.5, 9.5}, {12, 13}}};
        painter.drawPolygon(mountain.data(), int(mountain.size()));
        painter.setBrush(QColor("#e5b73b"));
        painter.drawEllipse(QPointF(6, 6), 1.4, 1.4);
        return;
    }
    if (badge.label.isEmpty()) {
        // Plain text: lines.
        painter.setPen(QPen(lineColor(dark), 1));
        for (const qreal y : {6.5, 9.0, 11.5})
            painter.drawLine(QPointF(4.5, y), QPointF(y == 11.5 ? 8.5 : 10.5, y));
        return;
    }
    // Badge across the bottom right corner.
    const bool wide = badge.label.size() > 1;
    const QRectF rect(wide ? 5 : 7, 7.5, wide ? 11 : 9, 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(badge.color);
    painter.drawRoundedRect(rect, 2, 2);
    QFont font;
    font.setPixelSize(wide ? 6 : 7);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, -0.3);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(rect.adjusted(0, -0.5, 0, 0), Qt::AlignCenter, badge.label);
}

void drawFolder(QPainter &painter, const QString &, bool dark)
{
    const QColor fill = lineColor(dark);
    QPainterPath path;
    path.moveTo(1.5, 4);
    path.quadTo(1.5, 2.5, 3, 2.5);
    path.lineTo(6, 2.5);
    path.lineTo(7.5, 4);
    path.lineTo(13, 4);
    path.quadTo(14.5, 4, 14.5, 5.5);
    path.lineTo(14.5, 12);
    path.quadTo(14.5, 13.5, 13, 13.5);
    path.lineTo(3, 13.5);
    path.quadTo(1.5, 13.5, 1.5, 12);
    path.closeSubpath();
    painter.setPen(QPen(fill, 1));
    QColor inside = fill;
    inside.setAlpha(dark ? 70 : 90);
    painter.setBrush(inside);
    painter.drawPath(path);
    painter.drawLine(QPointF(1.5, 6), QPointF(14.5, 6));
}

} // namespace

namespace FileIcons {

QString kindOf(const QString &fileName)
{
    const QString name = fileName.toLower();
    static const QHash<QString, QString> names = {
        {"cmakelists.txt", "cmake"}, {"makefile", "config"}, {"dockerfile", "config"},      {".gitignore", "git"},
        {".gitattributes", "git"},   {".gitmodules", "git"}, {"package-lock.json", "lock"}, {"yarn.lock", "lock"},
        {"pnpm-lock.yaml", "lock"},  {"cargo.lock", "lock"}, {"poetry.lock", "lock"},       {"go.sum", "lock"},
    };
    if (const auto it = names.constFind(name); it != names.cend())
        return *it;
    static const QHash<QString, QString> extensions = {
        {"c", "c"},
        {"cpp", "cpp"},
        {"cc", "cpp"},
        {"cxx", "cpp"},
        {"c++", "cpp"},
        {"h", "header"},
        {"hpp", "header"},
        {"hh", "header"},
        {"hxx", "header"},
        {"java", "java"},
        {"js", "javascript"},
        {"mjs", "javascript"},
        {"cjs", "javascript"},
        {"jsx", "jsx"},
        {"ts", "typescript"},
        {"mts", "typescript"},
        {"tsx", "tsx"},
        {"json", "json"},
        {"md", "markdown"},
        {"markdown", "markdown"},
        {"cmake", "cmake"},
        {"py", "python"},
        {"sh", "shell"},
        {"bash", "shell"},
        {"zsh", "shell"},
        {"yml", "yaml"},
        {"yaml", "yaml"},
        {"xml", "markup"},
        {"html", "markup"},
        {"htm", "markup"},
        {"svg", "image"},
        {"css", "css"},
        {"scss", "css"},
        {"less", "css"},
        {"toml", "config"},
        {"ini", "config"},
        {"cfg", "config"},
        {"conf", "config"},
        {"desktop", "config"},
        {"png", "image"},
        {"jpg", "image"},
        {"jpeg", "image"},
        {"gif", "image"},
        {"bmp", "image"},
        {"webp", "image"},
        {"ico", "image"},
        {"flow", "diagram"},
        {"mmd", "diagram"},
        {"mermaid", "diagram"},
        {"puml", "diagram"},
        {"plantuml", "diagram"},
        {"board", "diagram"},
        {"lock", "lock"},
    };
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? extensions.value(name.mid(dot + 1), QStringLiteral("text")) : QStringLiteral("text");
}

QIcon file(const QString &fileName, bool dark)
{
    static QHash<QString, QIcon> cache;
    const QString kind = kindOf(fileName);
    const QString key = kind + (dark ? QStringLiteral(":dark") : QStringLiteral(":light"));
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    const QIcon icon = render(kind, dark, drawPage);
    cache.insert(key, icon);
    return icon;
}

QIcon folder(bool dark)
{
    static std::array<QIcon, 2> icons;
    QIcon &icon = icons.at(dark ? 1 : 0);
    if (icon.isNull())
        icon = render(QString(), dark, drawFolder);
    return icon;
}

QIcon withClaudeDot(const QIcon &icon)
{
    static QHash<qint64, QIcon> cache;
    const qint64 key = icon.cacheKey();
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    const QIcon result(new ClaudeDotEngine(icon)); // the icon owns its engine
    cache.insert(key, result);
    return result;
}

} // namespace FileIcons
