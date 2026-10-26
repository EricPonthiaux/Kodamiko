// SPDX-License-Identifier: GPL-3.0-only

#include "AboutPopup.h"

#include "AppFonts.h"
#include "BrandMark.h"
#include "Motion.h"
#include "PopupFooter.h"
#include "PopupMetrics.h"
#include "PopupWidgets.h"
#include "RotatingQuote.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <array>
#include <chrono>

using namespace std::chrono_literals;

namespace {

constexpr int markSizePx = 64;
constexpr int heartSizePx = 16;
constexpr std::chrono::milliseconds markGrowDuration = 700ms;
// A heartbeat: two quick pulses, then a rest.
constexpr std::chrono::milliseconds heartbeatDuration = 1200ms;
constexpr qreal heartbeatPeak = 1.25;
const QColor heartColor{0xe5, 0x48, 0x5c};

constexpr std::array authorEmails{"ponthiaux.eric@protonmail.com", "ponthiaux.eric@protonmail.com"};

QString emailLinks()
{
    QStringList links;
    for (const char *email : authorEmails)
        links.append(QStringLiteral("<a href=\"mailto:%1\">%1</a>").arg(QLatin1String(email)));
    return links.join(QStringLiteral("<br>"));
}

// A heart filling the unit square, drawn with two cubic curves per side.
QPainterPath heartPath()
{
    QPainterPath path;
    path.moveTo(0.5, 0.95);
    path.cubicTo(0.1, 0.65, 0.0, 0.45, 0.0, 0.3);
    path.cubicTo(0.0, 0.12, 0.13, 0.02, 0.27, 0.02);
    path.cubicTo(0.38, 0.02, 0.46, 0.08, 0.5, 0.18);
    path.cubicTo(0.54, 0.08, 0.62, 0.02, 0.73, 0.02);
    path.cubicTo(0.87, 0.02, 1.0, 0.12, 1.0, 0.3);
    path.cubicTo(1.0, 0.45, 0.9, 0.65, 0.5, 0.95);
    return path;
}

} // namespace

// Duet's mark, which grows from the center to its full size.
class AboutPopup::Mark : public QWidget {
public:
    explicit Mark(QWidget *parent)
        : QWidget(parent)
        , grow_(new QVariantAnimation(this))
    {
        setFixedSize(markSizePx, markSizePx);
        grow_->setDuration(int(markGrowDuration.count()));
        grow_->setStartValue(0.0);
        grow_->setEndValue(1.0);
        grow_->setEasingCurve(Motion::enterCurve());
        connect(grow_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            scale_ = value.toReal();
            update();
        });
        Motion::track(grow_);
    }

    void setColors(const BrandMark::Colors &colors)
    {
        colors_ = colors;
        update();
    }

    // Grows the mark from nothing; shows it whole at once without motion.
    void play()
    {
        grow_->stop();
        if (!Motion::policy().isEnabled()) {
            scale_ = 1;
            update();
            return;
        }
        grow_->start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal side = width() * scale_;
        QRectF area(0, 0, side, side);
        area.moveCenter(QRectF(rect()).center());
        BrandMark::paint(&painter, area, colors_);
    }

private:
    QVariantAnimation *grow_;
    BrandMark::Colors colors_;
    qreal scale_ = 1;
};

// A red heart that beats while it is shown and motion is on.
class AboutPopup::Heart : public QWidget {
public:
    explicit Heart(QWidget *parent)
        : QWidget(parent)
        , beat_(new QVariantAnimation(this))
    {
        setFixedSize(heartSizePx, heartSizePx);
        beat_->setDuration(int(heartbeatDuration.count()));
        beat_->setLoopCount(-1);
        beat_->setKeyValueAt(0.0, 1.0);
        beat_->setKeyValueAt(0.12, heartbeatPeak);
        beat_->setKeyValueAt(0.24, 1.0);
        beat_->setKeyValueAt(0.36, heartbeatPeak);
        beat_->setKeyValueAt(0.5, 1.0);
        beat_->setKeyValueAt(1.0, 1.0);
        connect(beat_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            scale_ = value.toReal();
            update();
        });
        connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
    }

protected:
    void showEvent(QShowEvent *) override { updateMotion(); }

    void hideEvent(QHideEvent *) override { updateMotion(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // The heart at rest leaves room for its peak.
        const qreal side = width() * scale_ / heartbeatPeak;
        painter.translate(QRectF(rect()).center());
        painter.scale(side, side);
        painter.translate(-0.5, -0.5);
        painter.fillPath(heartPath(), heartColor);
    }

private:
    void updateMotion()
    {
        if (isVisible() && Motion::policy().isEnabled()) {
            if (beat_->state() != QAbstractAnimation::Running)
                beat_->start();
            return;
        }
        beat_->stop();
        scale_ = 1;
        update();
    }

    QVariantAnimation *beat_;
    qreal scale_ = 1;
};

AboutPopup::AboutPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("about"), QSizeF(0.3, 0.3), QSize(440, 400), parent)
    , header_(new PopupHeader(QStringLiteral("document"), tr("About Duet"), this))
    , mark_(new Mark(this))
{
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    auto *name = new QLabel(QStringLiteral("Duet"), this);
    AppFonts::setInterfaceFont(name, 1.4);
    auto *version = new QLabel(tr("Version %1").arg(QCoreApplication::applicationVersion()), this);
    version->setObjectName(QStringLiteral("settingsDescription"));
    auto *identity = new QVBoxLayout;
    identity->setSpacing(0);
    identity->addWidget(name);
    identity->addWidget(version);
    auto *brand = new QHBoxLayout;
    brand->setSpacing(16);
    brand->addWidget(mark_);
    brand->addLayout(identity, 1);

    auto *contact = new QLabel(tr("Contact") + QStringLiteral("<br>") + emailLinks(), this);
    contact->setTextFormat(Qt::RichText);
    contact->setTextInteractionFlags(Qt::TextBrowserInteraction);
    contact->setOpenExternalLinks(true);

    auto *love = new QHBoxLayout;
    love->setSpacing(6);
    love->addWidget(new QLabel(tr("Made with love"), this));
    love->addWidget(new Heart(this));
    love->addStretch();

    auto *body = new QVBoxLayout;
    body->setContentsMargins(PopupMetrics::bodyMargins);
    body->setSpacing(20);
    body->addLayout(brand);
    body->addWidget(contact);
    body->addWidget(new RotatingQuote(this));
    body->addStretch();
    body->addLayout(love);

    auto *footer = new PopupFooter(this);
    footer->setHint(tr("Enter or Esc to close"));
    auto *close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    footer->addButton(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void AboutPopup::popup()
{
    showCentered();
    setFocus();
    mark_->play();
}

void AboutPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme));
    header_->applyTheme(theme);
    mark_->setColors(BrandMark::colorsFor(theme));
}

void AboutPopup::keyPressEvent(QKeyEvent *event)
{
    const bool closes =
        event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (closes) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
