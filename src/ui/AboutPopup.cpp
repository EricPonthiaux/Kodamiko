// SPDX-License-Identifier: GPL-3.0-only

#include "AboutPopup.h"

#include "AppFonts.h"
#include "Motion.h"
#include "Paragraph.h"
#include "PopupFooter.h"
#include "PopupWidgets.h"
#include "RotatingQuote.h"
#include "ThemeStyle.h"
#include "UiMetrics.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <chrono>

using namespace std::chrono_literals;

namespace {

constexpr int heartSizePx = 16;
// A heartbeat: two quick pulses, then a rest.
constexpr std::chrono::milliseconds heartbeatDuration = 1200ms;
constexpr qreal heartbeatPeak = 1.25;
const QColor heartColor{0xe5, 0x48, 0x5c};

constexpr auto authorEmail = "ponthiaux.eric@protonmail.com";

constexpr auto aboutRules = R"(
QWidget#pageContent { background: @window; }
QFrame#aboutDetails, QFrame#aboutIdentity, QFrame#aboutContact { background: transparent; border: none; }
QLabel#aboutVersion { color: @muted; background: transparent; border: 1px solid @line; border-radius: 3px; padding: 3px 6px; }
QLabel#aboutEyebrow { color: @accent; }
QFrame#aboutQuote { background: @quoteTint; border: 1px solid @quoteBorder; border-radius: 7px; }
QPushButton { padding: 7px 18px; border-radius: 5px; }
)";

QString emailLink()
{
    return QStringLiteral("<a href=\"mailto:%1\">%1</a>").arg(QLatin1String(authorEmail));
}

QLabel *eyebrow(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("aboutEyebrow"));
    AppFonts::followApplicationFont(label, [label] {
        QFont font = AppFonts::codeFont(QApplication::font().pointSizeF() * 0.72);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
        label->setFont(font);
    });
    return label;
}

QFrame *quoteCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("aboutQuote"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    layout->addWidget(eyebrow(AboutPopup::tr("A THOUGHT TO CODE BY"), card));
    layout->addWidget(new RotatingQuote(card));
    return card;
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

// A red heart that beats while it is shown and motion is on.
class AboutPopup::Heart : public QWidget {
public:
    explicit Heart(QWidget *parent)
        : QWidget(parent)
        , beat_(new QVariantAnimation(this))
    {
        UiMetrics::setScaledSize(this, QSize(heartSizePx, heartSizePx));
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
    : OverlayPopup(QStringLiteral("about"), QSizeF(0.6, 0.6), QSize(800, 540), parent)
    , header_(new PopupHeader(QStringLiteral("document"), tr("About Kodamiko"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(createBody());

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
    layout->addWidget(scroll, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

QFrame *AboutPopup::createIdentity()
{
    auto *identity = new QFrame(this);
    identity->setObjectName(QStringLiteral("aboutIdentity"));
    auto *name = new QLabel(QStringLiteral("Kodamiko"), identity);
    AppFonts::setInterfaceFont(name, 2.2, QFont::Normal, -0.7);
    auto *description = new QLabel(identity);
    description->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(description, tr("Your editor. Your agent. In sync."));
    auto *version = new QLabel(tr("Version %1").arg(QCoreApplication::applicationVersion()), identity);
    version->setObjectName(QStringLiteral("aboutVersion"));
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    AppFonts::followApplicationFont(
        version, [version] { version->setFont(AppFonts::codeFont(QApplication::font().pointSizeF() * 0.75)); });
    auto *license = new QLabel(tr("Open source · GPL v3"), identity);
    license->setObjectName(QStringLiteral("settingsDescription"));
    AppFonts::setInterfaceFont(license, 0.85, QFont::Normal);
    auto *metadata = new QHBoxLayout;
    metadata->setSpacing(10);
    metadata->addWidget(version, 0, Qt::AlignLeft);
    metadata->addWidget(license);
    metadata->addStretch();

    auto *layout = new QVBoxLayout(identity);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(eyebrow(tr("ABOUT THE EDITOR"), identity));
    layout->addWidget(name);
    layout->addWidget(description);
    layout->addSpacing(6);
    layout->addLayout(metadata);
    return identity;
}

QFrame *AboutPopup::createContact()
{
    auto *contact = new QFrame(this);
    contact->setObjectName(QStringLiteral("aboutContact"));
    auto *email = new QLabel(emailLink(), contact);
    email->setTextFormat(Qt::RichText);
    email->setTextInteractionFlags(Qt::TextBrowserInteraction);
    email->setOpenExternalLinks(true);
    email->setWordWrap(true);
    auto *layout = new QVBoxLayout(contact);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(eyebrow(tr("GET IN TOUCH"), contact));
    layout->addWidget(email);
    return contact;
}

QFrame *AboutPopup::createDetails()
{
    auto *content = new QFrame(this);
    content->setObjectName(QStringLiteral("aboutDetails"));
    auto *love = new QHBoxLayout;
    love->setSpacing(6);
    auto *madeWithLove = new QLabel(tr("Made with love"), content);
    madeWithLove->setObjectName(QStringLiteral("settingsDescription"));
    love->addWidget(madeWithLove);
    love->addWidget(new Heart(content));
    love->addStretch();

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(24);
    layout->addWidget(createIdentity());
    layout->addWidget(createContact());
    layout->addWidget(quoteCard(content));
    layout->addStretch();
    layout->addLayout(love);
    return content;
}

QWidget *AboutPopup::createBody()
{
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("pageContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(0);
    layout->addWidget(createDetails(), 1);
    return content;
}

void AboutPopup::popup()
{
    showCentered();
    setFocus();
}

void AboutPopup::applyTheme(const Theme &theme)
{
    QString rules = QString::fromLatin1(aboutRules);
    rules.replace(QStringLiteral("@quoteTint"), ThemeStyle::mixed(theme.window, theme.accent, 0.06).name());
    rules.replace(QStringLiteral("@quoteBorder"), ThemeStyle::mixed(theme.window, theme.accent, 0.25).name());
    setStyleSheet(popupStyleSheet(theme, rules));
    header_->applyTheme(theme);
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
