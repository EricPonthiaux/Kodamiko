// SPDX-License-Identifier: GPL-3.0-only

#include "PrerequisitesPopup.h"

#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int statusIconSize = 16;

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *prerequisiteRules = R"(
QFrame#prerequisiteCard { border: 1px solid @line; border-radius: 0; background: @panel; }
QLineEdit#installCommand { background: @base; border: 1px solid @line; padding: 3px 6px; }
)";

QString statusText(const Prerequisite &prerequisite)
{
    switch (prerequisite.status) {
    case Prerequisite::Status::Missing:
        return PrerequisitesPopup::tr("Not found");
    case Prerequisite::Status::Outdated:
        return PrerequisitesPopup::tr("Version %1 — version %2 or newer is needed")
            .arg(prerequisite.version.toString(), prerequisite.minimumVersion.toString());
    case Prerequisite::Status::Ok:
        return prerequisite.version.isNull()
                   ? QDir::toNativeSeparators(prerequisite.path)
                   : PrerequisitesPopup::tr("Version %1 — %2")
                         .arg(prerequisite.version.toString(), QDir::toNativeSeparators(prerequisite.path));
    }
    return {};
}

QIcon statusIcon(const Theme &theme, Prerequisite::Status status)
{
    switch (status) {
    case Prerequisite::Status::Missing:
        return UiIcons::icon(QStringLiteral("problems"), theme.error);
    case Prerequisite::Status::Outdated:
        return UiIcons::icon(QStringLiteral("problems"), theme.warning);
    case Prerequisite::Status::Ok:
        return UiIcons::icon(QStringLiteral("check"), theme.accent);
    }
    return {};
}

} // namespace

PrerequisitesPopup::PrerequisitesPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("prerequisites"), QSizeF(0.5, 0.6), QSize(640, 420), parent)
    , theme_(theme)
    , header_(new PopupHeader(QStringLiteral("check"), tr("Prerequisites"), this))
    , cardsLayout_(new QVBoxLayout)
{
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    // The programs, one card each.
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("pageContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(PopupMetrics::bodyMargins);
    cardsLayout_->setSpacing(PopupMetrics::bodySpacing);
    contentLayout->addLayout(cardsLayout_);
    contentLayout->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    // The footer.
    auto *footer = new PopupFooter(this);
    Paragraph::setText(footer->hint(), tr("Installations run in a terminal tab, where you can follow them and "
                                          "answer their questions."));
    auto *checkAgain = new QPushButton(tr("Check Again"), footer);
    auto *close = new QPushButton(tr("Close"), footer);
    close->setDefault(true);
    connect(checkAgain, &QPushButton::clicked, this, &PrerequisitesPopup::recheck);
    connect(close, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    footer->addButton(checkAgain);
    footer->addButton(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(scroll, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void PrerequisitesPopup::popup()
{
    recheck();
    showCentered();
}

bool PrerequisitesPopup::allOk() const
{
    return std::all_of(prerequisites_.begin(), prerequisites_.end(),
                       [](const Prerequisite &p) { return p.status == Prerequisite::Status::Ok; });
}

void PrerequisitesPopup::recheck()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    prerequisites_ = Prerequisites::check();
    QApplication::restoreOverrideCursor();
    rebuild();
    emit checked();
}

void PrerequisitesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(prerequisiteRules)));
    header_->applyTheme(theme);
    rebuild(); // status icons in the theme's colors
}

void PrerequisitesPopup::rebuild()
{
    while (const QLayoutItem *item = cardsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    header_->setSubtitle(allOk() ? tr("Everything the editor needs is installed.")
                                 : tr("Some programs the editor relies on are missing or too old. Install them, "
                                      "then click Check Again."));

    for (const Prerequisite &prerequisite : std::as_const(prerequisites_))
        cardsLayout_->addWidget(createCard(prerequisite));
}

QFrame *PrerequisitesPopup::createCard(const Prerequisite &prerequisite)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("prerequisiteCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(6);

    auto *icon = new QLabel(card);
    icon->setPixmap(
        statusIcon(theme_, prerequisite.status).pixmap(UiMetrics::scaledSquare(statusIconSize), devicePixelRatioF()));
    auto *name = new QLabel(prerequisite.name, card);
    AppFonts::setInterfaceFont(name, 1.0, QFont::DemiBold);
    auto *status = new QLabel(statusText(prerequisite), card);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (prerequisite.status == Prerequisite::Status::Ok)
        status->setObjectName(QStringLiteral("settingsDescription")); // a path: secondary text
    auto *title = new QHBoxLayout;
    title->setSpacing(8);
    title->addWidget(icon);
    title->addWidget(name);
    title->addWidget(status, 1);
    cardLayout->addLayout(title);

    auto *purpose = new QLabel(card);
    purpose->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(purpose, prerequisite.purpose);
    cardLayout->addWidget(purpose);

    if (prerequisite.status == Prerequisite::Status::Ok)
        return card;
    if (!prerequisite.installCommand.isEmpty())
        cardLayout->addLayout(createInstallRow(prerequisite, card));
    auto *guide = new QLabel(tr("<a href=\"%1\">Installation guide</a>").arg(prerequisite.helpUrl.toString()), card);
    guide->setOpenExternalLinks(true);
    cardLayout->addWidget(guide);
    return card;
}

QHBoxLayout *PrerequisitesPopup::createInstallRow(const Prerequisite &prerequisite, QWidget *card)
{
    auto *command = new QLineEdit(prerequisite.installCommand, card);
    command->setObjectName(QStringLiteral("installCommand"));
    command->setReadOnly(true);
    command->setFont(AppFonts::codeFont(font().pointSizeF() * 0.95));
    command->setCursorPosition(0);
    auto *copy = new QPushButton(tr("Copy"), card);
    connect(copy, &QPushButton::clicked, this,
            [text = prerequisite.installCommand] { QApplication::clipboard()->setText(text); });
    auto *install =
        new QPushButton(prerequisite.status == Prerequisite::Status::Outdated ? tr("Update") : tr("Install"), card);
    connect(install, &QPushButton::clicked, this, [this, prerequisite] {
        dismiss();
        emit installRequested(tr("Install %1").arg(prerequisite.name),
                              Prerequisites::installProcess(prerequisite, QDir::homePath()));
    });
    auto *commandRow = new QHBoxLayout;
    commandRow->addWidget(command, 1);
    commandRow->addWidget(copy);
    commandRow->addWidget(install);
    return commandRow;
}

void PrerequisitesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
