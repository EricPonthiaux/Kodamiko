// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSetupForm.h"
#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr auto setupRules = R"(
QLabel#languageSymbol { background: @slate; color: @accentText; border: 1px solid @line; border-radius: 8px; }
QLabel#activeCount, QLabel#recipeDescription,
QLabel#recipeTags, QLabel#toolDetail { color: @muted; }
QLabel#languageEvidence { color: @muted; background: @window; border-radius: 5px; padding: 7px 10px; }
QFrame#recipeCard { background: @window; border: 1px solid @line; border-radius: 7px; }
QLabel#recipeTags { background: @panel; border-radius: 4px; padding: 5px 8px; }
QFrame#toolSummary { background: @window; border: 1px solid @line; border-radius: 6px; }
QFrame#toolSummary[state="download"] { border-color: @warning; }
QFrame#toolSummary[state="missing"] { border-color: @error; }
QLabel#toolBadge { color: @accentText; }
QFrame#toolSummary[state="download"] QLabel#toolBadge { color: @warning; }
QFrame#toolSummary[state="missing"] QLabel#toolBadge { color: @error; }
QListWidget#languageNav::item { padding: 0; margin: 2px 0; border-radius: 6px; }
QListWidget#languageNav::item:selected { background: @slate; }
QLabel#languageNavStatus { color: @muted; }
QLabel#languageNavSymbol { color: @accentText; background: @panel; border: 1px solid @line; border-radius: 5px; }
QFrame#languageOff { border: 1px solid @line; border-radius: 7px; }
QComboBox { padding: 6px 10px; border-radius: 5px; }
)";

QString languageSymbol(const QString &family)
{
    if (family == QLatin1String("cpp"))
        return QStringLiteral("C++");
    if (family == QLatin1String("javascript"))
        return QStringLiteral("JS");
    if (family == QLatin1String("python"))
        return QStringLiteral("Py");
    return QStringLiteral("Jv");
}

QString languageDescription(const QString &family)
{
    if (family == QLatin1String("cpp"))
        return QObject::tr("Build, run and navigate your C++ code.");
    if (family == QLatin1String("javascript"))
        return QObject::tr("Set up your JavaScript and TypeScript environment.");
    if (family == QLatin1String("python"))
        return QObject::tr("Choose the tools for your Python project.");
    return QObject::tr("Configure the JDK and tools for your Java code.");
}

QString runtimeLabel(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("JDK");
    if (family == QLatin1String("javascript"))
        return QObject::tr("Node.js");
    if (family == QLatin1String("python"))
        return QObject::tr("Python interpreter");
    return QObject::tr("C++ standard");
}

QString runtimeDescription(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("The Java installation used by Maven.");
    if (family == QLatin1String("javascript"))
        return QObject::tr("The runtime used to run this project.");
    if (family == QLatin1String("python"))
        return QObject::tr("Its installed packages are used for completion.");
    return QObject::tr("Fallback for files without build configuration.");
}

QLabel *description(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(label, text);
    return label;
}

QWidget *heading(const QString &title, const QString &icon, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *mark = new QLabel(row);
    mark->setProperty("setupIcon", icon);
    auto *label = new QLabel(title, row);
    AppFonts::setInterfaceFont(label, 1, QFont::DemiBold);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(8);
    layout->addWidget(mark);
    layout->addWidget(label);
    layout->addStretch();
    return row;
}

QComboBox *settingCombo(const QString &name, QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setObjectName(name);
    combo->setMinimumWidth(UiMetrics::scaled(230));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(16);
    return combo;
}

QWidget *fieldWithState(QComboBox *combo, QLabel *&state)
{
    auto *field = new QWidget;
    state = new QLabel(field);
    state->setObjectName(QStringLiteral("settingState"));
    state->setTextFormat(Qt::PlainText);
    state->setWordWrap(true);
    state->setMaximumWidth(UiMetrics::scaled(270));
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->addWidget(combo);
    layout->addWidget(state);
    return field;
}

// QCheckBox keeps its keyboard and accessibility behavior; only its drawing changes.
class LanguageSwitch : public QCheckBox {
public:
    explicit LanguageSwitch(QWidget *parent)
        : QCheckBox(QObject::tr("Enabled"), parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        return {fontMetrics().horizontalAdvance(text()) + UiMetrics::scaled(56),
                qMax(fontMetrics().height(), UiMetrics::scaled(26))};
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int trackWidth = UiMetrics::scaled(36);
        const int trackHeight = UiMetrics::scaled(20);
        const QRect track(width() - trackWidth - 2, (height() - trackHeight) / 2, trackWidth, trackHeight);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect().adjusted(0, 0, -trackWidth - 12, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(isChecked() ? QPalette::Accent : QPalette::Mid));
        painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);
        const int diameter = trackHeight - 6;
        const int knobX = isChecked() ? track.right() - diameter - 2 : track.left() + 3;
        painter.setBrush(palette().color(QPalette::BrightText));
        painter.drawEllipse(QRect(knobX, track.top() + 3, diameter, diameter));
        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(palette().color(QPalette::Accent), 1, Qt::DotLine));
            painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
        }
    }
};

// The embedded row supplies the text; the view still draws selection and keyboard focus.
class LanguageDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
    }
};

} // namespace

QWidget *ProjectSetupForm::createSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("popupSidebar"));
    AppFonts::followApplicationFont(sidebar, [sidebar] { sidebar->setFixedWidth(UiMetrics::scaled(232)); });
    auto *title = new QLabel(tr("LANGUAGES"), sidebar);
    title->setObjectName(QStringLiteral("navHeading"));
    AppFonts::setInterfaceFont(title, 0.8, QFont::DemiBold, 0.6);
    activeCount_ = new QLabel(sidebar);
    activeCount_->setObjectName(QStringLiteral("activeCount"));
    AppFonts::setInterfaceFont(activeCount_, 0.8, QFont::Normal);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(activeCount_);
    list_ = new QListWidget(sidebar);
    list_->setObjectName(QStringLiteral("languageNav"));
    list_->setAccessibleName(tr("Project languages"));
    list_->setItemDelegate(new LanguageDelegate(list_));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentItemChanged, this, [this](const QListWidgetItem *current) {
        if (current)
            selectLanguage(current->data(Qt::UserRole).toString());
    });
    detect_ = new QPushButton(tr("Detect again"), sidebar);
    detect_->setToolTip(tr("Scan the project's files again. Apply to save the detected settings."));
    connect(detect_, &QPushButton::clicked, this, &ProjectSetupForm::detectAgain);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 18, 12, 16);
    layout->setSpacing(12);
    layout->addLayout(titleRow);
    layout->addWidget(list_, 1);
    layout->addWidget(description(tr("Configured per project.\nAvailable in every session."), sidebar));
    layout->addWidget(detect_);
    layout->addWidget(description(tr("Scan the project for languages."), sidebar));
    return sidebar;
}

QWidget *ProjectSetupForm::createLanguageHeading(Page &page)
{
    auto *row = new QWidget;
    auto *symbol = new QLabel(languageSymbol(page.family), row);
    symbol->setObjectName(QStringLiteral("languageSymbol"));
    symbol->setAlignment(Qt::AlignCenter);
    UiMetrics::setScaledSize(symbol, QSize(46, 46));
    AppFonts::setInterfaceFont(symbol, 1.05, QFont::Bold);
    auto *title = new QLabel(ProjectLanguages::title(page.family), row);
    AppFonts::setInterfaceFont(title, 1.4, QFont::DemiBold);
    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    text->addWidget(title);
    text->addWidget(description(languageDescription(page.family), row));
    page.enabled = new LanguageSwitch(row);
    page.enabled->setObjectName(page.family + QStringLiteral("Enabled"));
    page.enabled->setAccessibleName(tr("Enable %1 for this project").arg(ProjectLanguages::title(page.family)));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(symbol);
    layout->addLayout(text, 1);
    layout->addWidget(page.enabled);
    return row;
}

QWidget *ProjectSetupForm::createRecipeCard(Page &page)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("recipeCard"));
    page.recipe = settingCombo(page.family + QStringLiteral("Recipe"), card);
    page.recipe->setAccessibleName(tr("Project recipe"));
    page.recipeState = description({}, card);
    page.recipeState->setObjectName(QStringLiteral("recipeDescription"));
    page.recipeTags = description({}, card);
    page.recipeTags->setObjectName(QStringLiteral("recipeTags"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(8);
    layout->addWidget(page.recipe);
    layout->addWidget(page.recipeState);
    layout->addWidget(page.recipeTags);
    return card;
}

QWidget *ProjectSetupForm::createSettings(Page &page)
{
    auto *settings = new QWidget;
    QWidget *recipeCard = createRecipeCard(page);
    page.runtime = settingCombo(page.family + QStringLiteral("Runtime"), settings);
    page.runtime->setAccessibleName(runtimeLabel(page.family));
    auto *runtimeCard = new SettingsCard(settings);
    runtimeCard->addRow(runtimeLabel(page.family), runtimeDescription(page.family),
                        fieldWithState(page.runtime, page.runtimeState));
    page.server = settingCombo(page.family + QStringLiteral("Server"), settings);
    page.server->setAccessibleName(tr("Language server"));
    page.serverVersion = settingCombo(page.family + QStringLiteral("ServerVersion"), settings);
    page.serverVersion->setAccessibleName(tr("Version & source"));
    auto *serverCard = new SettingsCard(settings);
    serverCard->addRow(tr("Language server"), tr("Completion, diagnostics and navigation."), page.server);
    page.versionRow = serverCard->addRow(tr("Version & source"), tr("Use a local tool or let Kodamiko install it."),
                                         fieldWithState(page.serverVersion, page.serverState));
    auto *layout = new QVBoxLayout(settings);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(heading(tr("Recipe"), QStringLiteral("files"), settings));
    layout->addWidget(recipeCard);
    layout->addSpacing(6);
    layout->addWidget(description(tr("Updates the environment, run configurations and agent instructions."), settings));
    layout->addSpacing(14);
    const bool builds = page.family == QLatin1String("cpp") || page.family == QLatin1String("java");
    layout->addWidget(heading(builds ? tr("Build") : tr("Runtime"), QStringLiteral("settings"), settings));
    layout->addWidget(runtimeCard);
    layout->addSpacing(12);
    layout->addWidget(heading(tr("Code intelligence"), QStringLiteral("code"), settings));
    layout->addWidget(serverCard);
    layout->addSpacing(12);
    layout->addWidget(createAvailability(page));
    return settings;
}

QWidget *ProjectSetupForm::createOffHint(const Page &page)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("languageOff"));
    auto *title = new QLabel(tr("%1 is not enabled").arg(ProjectLanguages::title(page.family)), card);
    title->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(title, 1.15, QFont::DemiBold);
    auto *hint = description(
        tr("Choose its recipe, runtime and language server.\nYour other project languages keep their settings."), card);
    hint->setAlignment(Qt::AlignCenter);
    auto *enable = new QPushButton(tr("Enable %1").arg(ProjectLanguages::title(page.family)), card);
    enable->setObjectName(page.family + QStringLiteral("EnableButton"));
    connect(enable, &QPushButton::clicked, page.enabled, [box = page.enabled] {
        box->setChecked(true);
        box->setFocus();
    });
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 40, 24, 40);
    layout->setSpacing(16);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(enable, 0, Qt::AlignHCenter);
    return card;
}

QWidget *ProjectSetupForm::createAvailability(Page &page)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("toolSummary"));
    page.availability = card;
    page.toolTitle = new QLabel(card);
    page.toolTitle->setWordWrap(true);
    AppFonts::setInterfaceFont(page.toolTitle, 1, QFont::DemiBold);
    page.toolDetail = description({}, card);
    page.toolDetail->setObjectName(QStringLiteral("toolDetail"));
    page.toolBadge = new QLabel(card);
    page.toolBadge->setObjectName(QStringLiteral("toolBadge"));
    auto *text = new QVBoxLayout;
    text->setSpacing(5);
    text->addWidget(page.toolTitle);
    text->addWidget(page.toolDetail);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);
    layout->addLayout(text, 1);
    layout->addWidget(page.toolBadge);
    return card;
}

QWidget *ProjectSetupForm::createPage(Page &page)
{
    auto *content = new QWidget;
    page.evidence = description({}, content);
    page.evidence->setObjectName(QStringLiteral("languageEvidence"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(12);
    layout->addWidget(createLanguageHeading(page));
    layout->addWidget(page.evidence);
    page.settings = createSettings(page);
    page.offHint = createOffHint(page);
    layout->addWidget(page.settings);
    layout->addWidget(page.offHint);
    return content;
}

void ProjectSetupForm::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(setupRules)));
    detect_->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.foreground));
    for (QLabel *label : findChildren<QLabel *>()) {
        const QString icon = label->property("setupIcon").toString();
        if (!icon.isEmpty())
            label->setPixmap(
                UiIcons::icon(icon, theme.mutedText).pixmap(UiMetrics::scaledSquare(16), devicePixelRatioF()));
    }
}

void ProjectSetupForm::updateListItem(const Page &page)
{
    const QString title = ProjectLanguages::title(page.family);
    activeCount_->setText(tr("%1 active").arg(std::ranges::count_if(pages_, [](const Page &entry) {
        return entry.enabled->isChecked();
    })));
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *entry = list_->item(row);
        if (entry->data(Qt::UserRole).toString() != page.family)
            continue;
        entry->setText(page.enabled->isChecked() ? title : tr("%1 · Off").arg(title));
        entry->setData(Qt::AccessibleTextRole, tr("%1, %2").arg(title, page.status));
        entry->setToolTip(page.status);
        if (const QWidget *widget = list_->itemWidget(entry)) {
            widget->findChild<QLabel *>(QStringLiteral("languageNavStatus"))->setText(page.status);
            continue;
        }
        auto *widget = new QWidget(list_);
        widget->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *symbol = new QLabel(languageSymbol(page.family), widget);
        symbol->setObjectName(QStringLiteral("languageNavSymbol"));
        symbol->setAlignment(Qt::AlignCenter);
        UiMetrics::setScaledSize(symbol, QSize(32, 32));
        AppFonts::setInterfaceFont(symbol, 0.85, QFont::DemiBold);
        const QString navTitle = page.family == QLatin1String("javascript") ? tr("JavaScript / TS") : title;
        auto *name = new QLabel(navTitle, widget);
        AppFonts::setInterfaceFont(name, 0.95, QFont::DemiBold);
        auto *status = new QLabel(page.status, widget);
        status->setObjectName(QStringLiteral("languageNavStatus"));
        AppFonts::setInterfaceFont(status, 0.8, QFont::Normal);
        auto *copy = new QVBoxLayout;
        copy->setSpacing(3);
        copy->addWidget(name);
        copy->addWidget(status);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(8, 10, 8, 10);
        layout->setSpacing(10);
        layout->addWidget(symbol);
        layout->addLayout(copy, 1);
        entry->setSizeHint(QSize(0, UiMetrics::scaled(68)));
        list_->setItemWidget(entry, widget);
    }
}
