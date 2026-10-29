// SPDX-License-Identifier: GPL-3.0-only

#include "NewProjectForm.h"
#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int templatePanelWidth = 272;
constexpr int templateIconSize = 32;
constexpr int templateRowPadding = 7;
constexpr int fieldHeight = 36;

constexpr auto formRules = R"(
QFrame#projectTemplatePanel { background: @window; border: none; border-right: 1px solid @line; }
QLabel#projectEyebrow { color: @accent; }
QLabel#projectCaption, QLabel#projectDestination { color: @muted; }
QLabel#projectRecipeTitle { color: @text; }
QFrame#projectRecipeDetails { background: @panel; border: 1px solid @line; border-radius: 6px; }
QFrame#projectGitOption { background: transparent; border: none; border-top: 1px solid @line; }
QLineEdit#projectName, QLineEdit#projectLocation { padding: 7px 10px; border-radius: 5px; }
QPushButton#projectBrowse { padding: 7px 10px; border-radius: 5px; }
QPushButton#projectCreate, QPushButton#projectCancel { padding: 8px 16px; border-radius: 5px; }
QListWidget#projectRecipes::item { padding: 0; margin: 0; border: none; background: transparent; }
QScrollArea#projectDetails { border: none; background: transparent; }
QWidget#projectDetailsContent { background: transparent; }
)";

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("projectCaption"));
    AppFonts::setInterfaceFont(label, 0.86, QFont::Normal);
    Paragraph::setText(label, text);
    return label;
}

QLabel *eyebrow(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("projectEyebrow"));
    AppFonts::setInterfaceFont(label, 0.72, QFont::DemiBold, 1.3);
    return label;
}

QLabel *fieldLabel(const QString &text, QWidget *field)
{
    auto *label = new QLabel(text, field->parentWidget());
    AppFonts::setInterfaceFont(label, 0.9, QFont::DemiBold);
    label->setBuddy(field);
    return label;
}

// Two lines keep the language and its build system legible in a narrow list.
// Painting from the palette also follows a theme changed while the form is open.
class TemplateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {0, std::max(templateIconSize, 2 * option.fontMetrics.height() + 2) + 2 * templateRowPadding};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect area = option.rect.adjusted(0, 3, -1, -3);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QColor accent = option.palette.color(QPalette::Link);
        const QColor muted = option.palette.color(QPalette::PlaceholderText);
        QColor tint = accent;
        tint.setAlpha(selected ? 24 : 12);
        painter->setPen(selected ? QPen(accent, 1) : QPen(Qt::NoPen));
        painter->setBrush(selected || hovered ? tint : QColor(Qt::transparent));
        painter->drawRoundedRect(area.adjusted(1, 1, -1, -1), 6, 6);

        const QRect icon(area.left() + 12, area.center().y() - templateIconSize / 2, templateIconSize,
                         templateIconSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(icon, 5, 5);
        const QString title = index.data().toString();
        const QStringList parts = title.split(QStringLiteral(" · "));
        const QString iconName = parts.size() > 1 ? QStringLiteral("code") : QStringLiteral("folder");
        UiIcons::icon(iconName, selected ? accent : muted).paint(painter, icon.adjusted(7, 7, -7, -7));
        paintText(painter, option, parts,
                  QRect(icon.right() + 12, area.top(), area.right() - icon.right() - 36, area.height()));
        if (selected) {
            const QRect check(area.right() - 22, area.center().y() - 7, 14, 14);
            UiIcons::icon(QStringLiteral("check"), accent).paint(painter, check);
        }
        painter->restore();
    }

private:
    static void paintText(QPainter *painter,
                          const QStyleOptionViewItem &option,
                          const QStringList &parts,
                          const QRect &area)
    {
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        QFont detail = option.font;
        detail.setPointSizeF(detail.pointSizeF() * 0.82);
        const int lineHeight = option.fontMetrics.height();
        const int top = area.center().y() - lineHeight - 1;
        painter->setFont(title);
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(QRect(area.left(), top, area.width(), lineHeight), Qt::AlignVCenter,
                          QFontMetrics(title).elidedText(parts.first(), Qt::ElideRight, area.width()));
        painter->setFont(detail);
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        const QString subtitle =
            parts.size() > 1 ? parts.sliced(1).join(QStringLiteral(" · ")) : QObject::tr("A blank canvas");
        painter->drawText(QRect(area.left(), top + lineHeight + 3, area.width(), lineHeight), Qt::AlignVCenter,
                          QFontMetrics(detail).elidedText(subtitle, Qt::ElideRight, area.width()));
    }
};

} // namespace

QWidget *NewProjectForm::createBody()
{
    auto *body = new QWidget(this);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createTemplatePanel());
    details_ = new QScrollArea(body);
    details_->setObjectName(QStringLiteral("projectDetails"));
    details_->setWidgetResizable(true);
    details_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    details_->setWidget(createDetails());
    layout->addWidget(details_, 1);
    setTabOrder(name_, location_);
    setTabOrder(location_, browse_);
    setTabOrder(browse_, recipeList_);
    setTabOrder(recipeList_, git_);
    return body;
}

QWidget *NewProjectForm::createTemplatePanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("projectTemplatePanel"));
    panel->setFixedWidth(templatePanelWidth);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 24, 18, 18);
    layout->setSpacing(6);
    layout->addWidget(eyebrow(tr("01 / START WITH"), panel));
    auto *title = new QLabel(tr("Choose a template"), panel);
    title->setWordWrap(true);
    AppFonts::setInterfaceFont(title, 1.15, QFont::DemiBold);
    layout->addWidget(title);
    layout->addSpacing(10);
    recipeList_->setObjectName(QStringLiteral("projectRecipes"));
    recipeList_->setAccessibleName(tr("Project templates"));
    recipeList_->setItemDelegate(new TemplateDelegate(recipeList_));
    recipeList_->setMouseTracking(true);
    recipeList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    recipeList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recipeList_->installEventFilter(this);
    layout->addWidget(recipeList_, 1);
    layout->addSpacing(8);
    layout->addWidget(caption(tr("A starting point. Make it your own."), panel));
    return panel;
}

QWidget *NewProjectForm::createDetails()
{
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("projectDetailsContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 24, 28, 16);
    layout->setSpacing(6);
    layout->addWidget(eyebrow(tr("02 / MAKE IT YOURS"), content));
    auto *title = new QLabel(tr("Your next big idea."), content);
    title->setWordWrap(true);
    AppFonts::setInterfaceFont(title, 1.9, QFont::Normal);
    layout->addWidget(title);
    layout->addWidget(caption(tr("Give it a name. We'll set up the starting point."), content));
    layout->addSpacing(12);
    name_->setObjectName(QStringLiteral("projectName"));
    name_->setAccessibleName(tr("Project name"));
    name_->setMinimumHeight(fieldHeight);
    name_->installEventFilter(this);
    layout->addWidget(fieldLabel(tr("Project name"), name_));
    layout->addWidget(name_);
    layout->addSpacing(6);
    layout->addWidget(createLocationFields());
    layout->addSpacing(8);
    layout->addWidget(createRecipeDetails());
    layout->addStretch(1);
    layout->addSpacing(8);
    layout->addWidget(createGitOption());
    return content;
}

QWidget *NewProjectForm::createLocationFields()
{
    auto *fields = new QWidget(this);
    auto *layout = new QVBoxLayout(fields);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    location_->setObjectName(QStringLiteral("projectLocation"));
    location_->setAccessibleName(tr("Project location"));
    location_->setMinimumHeight(fieldHeight);
    location_->setMinimumWidth(0);
    location_->installEventFilter(this);
    browse_ = new QPushButton(tr("Browse…"), fields);
    browse_->setObjectName(QStringLiteral("projectBrowse"));
    browse_->setMinimumHeight(fieldHeight);
    browse_->installEventFilter(this);
    connect(browse_, &QPushButton::clicked, this, &NewProjectForm::browse);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(location_, 1);
    row->addWidget(browse_);
    layout->addWidget(fieldLabel(tr("Location"), location_));
    layout->addLayout(row);
    destination_->setObjectName(QStringLiteral("projectDestination"));
    destination_->setTextFormat(Qt::PlainText);
    destination_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    AppFonts::setInterfaceFont(destination_, 0.8, QFont::Normal);
    destination_->installEventFilter(this);
    layout->addWidget(destination_);
    return fields;
}

QWidget *NewProjectForm::createRecipeDetails()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("projectRecipeDetails"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);
    recipeTitle_->setObjectName(QStringLiteral("projectRecipeTitle"));
    recipeTitle_->setTextFormat(Qt::PlainText);
    recipeTitle_->setWordWrap(true);
    AppFonts::setInterfaceFont(recipeTitle_, 0.92, QFont::DemiBold);
    description_->setObjectName(QStringLiteral("projectCaption"));
    AppFonts::setInterfaceFont(description_, 0.84, QFont::Normal);
    layout->addWidget(recipeTitle_);
    layout->addWidget(description_);
    auto *parameterRow = new QHBoxLayout(parameters_);
    parameterRow->setContentsMargins(0, 4, 0, 0);
    parameterRow->setSpacing(8);
    parameterRow->setAlignment(Qt::AlignLeft);
    layout->addWidget(parameters_);
    return card;
}

QWidget *NewProjectForm::createGitOption()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("projectGitOption"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 16, 0, 0);
    layout->setSpacing(4);
    AppFonts::setInterfaceFont(git_, 0.9, QFont::DemiBold);
    git_->installEventFilter(this);
    layout->addWidget(git_);
    auto *text = caption(tr("Track changes from your very first commit."), card);
    text->setContentsMargins(20, 0, 0, 0);
    layout->addWidget(text);
    return card;
}

QWidget *NewProjectForm::createFooter()
{
    auto *footer = new PopupFooter(this);
    footer->layout()->setContentsMargins(24, 14, 24, 14);
    summary_ = footer->hint();
    AppFonts::setInterfaceFont(summary_, 0.85, QFont::Normal);
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    cancel->setObjectName(QStringLiteral("projectCancel"));
    create_ = new QPushButton(tr("Create project"), footer);
    create_->setObjectName(QStringLiteral("projectCreate"));
    create_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &NewProjectForm::cancelled);
    connect(create_, &QPushButton::clicked, this, &NewProjectForm::create);
    footer->addButton(cancel);
    footer->addButton(create_);
    return footer;
}

void NewProjectForm::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(formRules)));
    header_->applyTheme(theme);
    browse_->setIcon(UiIcons::icon(QStringLiteral("folder"), theme.mutedText));
    recipeList_->viewport()->update();
    updateDestination();
}
