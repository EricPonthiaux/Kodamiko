// SPDX-License-Identifier: GPL-3.0-only

#include "Inspector.h"

#include "Board.h"
#include "Canvas.h"
#include "Document.h"
#include "Family.h"
#include "ui/AppFonts.h"
#include "ui/UiMetrics.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <optional>

namespace diagram {

namespace {

// Keeps the color buttons from reading as one more row of swatches.
constexpr int swatchButtonsGapPx = 8;

const QStringList directions{QStringLiteral("TB"), QStringLiteral("LR"), QStringLiteral("BT"), QStringLiteral("RL")};

QString directionLabel(const QString &direction)
{
    if (direction == QLatin1String("LR"))
        return Inspector::tr("Left to right");
    if (direction == QLatin1String("BT"))
        return Inspector::tr("Bottom to top");
    if (direction == QLatin1String("RL"))
        return Inspector::tr("Right to left");
    return Inspector::tr("Top to bottom");
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setForegroundRole(QPalette::PlaceholderText);
    AppFonts::setInterfaceFont(label, 0.85, QFont::DemiBold, 0.6);
    return label;
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setContentsMargins(0, 6, 0, 0);
    return label;
}

QWidget *swatchGrid(const QStringList &colors, QWidget *parent, const std::function<void(const QString &)> &choose)
{
    auto *grid = new QWidget(parent);
    auto *layout = new QGridLayout(grid);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    for (int i = 0; i < int(colors.size()); ++i) {
        auto *swatch = new QToolButton(grid);
        swatch->setFixedSize(22, 22);
        swatch->setToolTip(colors.at(i));
        swatch->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid rgba(127,127,127,0.5); "
                                             "border-radius: 4px; }")
                                  .arg(colors.at(i)));
        QObject::connect(swatch, &QToolButton::clicked, grid, [choose, color = colors.at(i)] { choose(color); });
        layout->addWidget(swatch, i / 6, i % 6);
    }
    return grid;
}

QString typeLabel(const QString &type)
{
    // "freeNode" → "Free node".
    QString label;
    for (const QChar c : type) {
        if (c.isUpper() && !label.isEmpty())
            label += QLatin1Char(' ') + c.toLower();
        else
            label += c;
    }
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

} // namespace

Inspector::Inspector(Board *board, Canvas *canvas, QWidget *parent)
    : QWidget(parent)
    , board_(board)
    , canvas_(canvas)
    , properties_(new QWidget(this))
    , propertiesLayout_(new QVBoxLayout(properties_))
    , source_(new QPlainTextEdit(this))
    , sourceError_(new QLabel(this))
    , sourceTimer_(new QTimer(this))
{
    propertiesLayout_->setContentsMargins(12, 12, 12, 12);
    propertiesLayout_->setSpacing(4);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(properties_);

    // Source.
    auto *sourcePage = new QWidget(this);
    auto *sourceLayout = new QVBoxLayout(sourcePage);
    sourceLayout->setContentsMargins(8, 8, 8, 8);
    source_->setFont(AppFonts::codeFont(QApplication::font().pointSizeF() * 0.95));
    source_->setLineWrapMode(QPlainTextEdit::NoWrap);
    source_->setTabChangesFocus(false);
    sourceError_->setWordWrap(true);
    sourceError_->setForegroundRole(QPalette::PlaceholderText);
    sourceError_->hide();
    auto *apply = new QPushButton(tr("Apply"), sourcePage);
    apply->setToolTip(tr("Replace the diagram with this source; elements keep their place by id"));
    connect(apply, &QPushButton::clicked, this, &Inspector::applySource);
    auto *copy = new QPushButton(tr("Copy"), sourcePage);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(source_->toPlainText()); });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(apply);
    buttons->addWidget(copy);
    buttons->addStretch();
    sourceLayout->addWidget(source_, 1);
    sourceLayout->addWidget(sourceError_);
    sourceLayout->addLayout(buttons);

    auto *pages = new QStackedWidget(this);
    pages->addWidget(scroll);
    pages->addWidget(sourcePage);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createModeSelector(pages));
    layout->addWidget(pages, 1);

    sourceTimer_->setSingleShot(true);
    sourceTimer_->setInterval(250);
    connect(sourceTimer_, &QTimer::timeout, this, &Inspector::refreshSource);
    connect(pages, &QStackedWidget::currentChanged, this, [this](int) { refreshSource(); });
    connect(board_, &Board::selectionChanged, this, [this] {
        endTyping();
        rebuild();
    });
    connect(board_, &Board::changed, this, [this] {
        if (!updating_ && typingBase_.isEmpty())
            rebuild();
        sourceTimer_->start();
    });
    rebuild();
}

// Two segments in one frame, Properties and Source: the panel's modes, set
// apart from the document tabs above.
QWidget *Inspector::createModeSelector(QStackedWidget *pages)
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("diagramInspectorHeader"));
    bar->setAttribute(Qt::WA_StyledBackground);
    // The height of the diagram's toolbar beside it: one line under both.
    UiMetrics::setScaledHeight(bar, UiMetrics::panelHeaderHeight);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 0, 8, 0);
    auto *selector = new QFrame(bar);
    selector->setObjectName(QStringLiteral("modeSelector"));
    selector->setAccessibleName(tr("Panel mode"));
    auto *segments = new QHBoxLayout(selector);
    segments->setContentsMargins(2, 2, 2, 2);
    segments->setSpacing(2);
    auto *group = new QButtonGroup(this);
    // In the order of the pages, whose indexes are the buttons' ids.
    const QStringList names = {tr("Properties"), tr("Source")};
    for (int i = 0; i < names.size(); ++i) {
        auto *button = new QToolButton(selector);
        button->setText(names[i]);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(button, i);
        segments->addWidget(button);
    }
    connect(group, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);
    barLayout->addWidget(selector);
    return bar;
}

void Inspector::setCodec(const Codec *codec)
{
    codec_ = codec;
    sourceVersion_ = ~quint64(0);
    refreshSource();
    // A new document (its family, its direction): the section is rebuilt.
    shown_ = QStringLiteral("-");
    rebuild();
}

void Inspector::beginTyping()
{
    if (typingBase_.isEmpty())
        typingBase_ = board_->snapshot();
}

void Inspector::endTyping()
{
    if (typingBase_.isEmpty())
        return;
    const QByteArray base = typingBase_;
    typingBase_.clear();
    board_->resolveLabels();
    board_->pushHistory(base);
}

namespace {

// What the properties panel shows: one shape or link (by id and type),
// several elements, or the document (empty).
QString selectionKey(const Board &board)
{
    if (const Shape *s = board.selectedShape())
        return QStringLiteral("s:") + s->id + s->type;
    if (const Link *l = board.selectedLink())
        return QStringLiteral("l:") + l->id + l->type;
    if (board.hasSelection())
        return QStringLiteral("many");
    return {};
}

// Removes every spacer but the last item, so that only one stretch ends
// the layout.
void keepOnlyFinalStretch(QVBoxLayout *layout)
{
    for (int i = layout->count() - 2; i >= 0; --i) {
        if (layout->itemAt(i)->spacerItem())
            delete layout->takeAt(i);
    }
}

} // namespace

bool Inspector::showsAlready(const QString &key) const
{
    if (!section_ || key != shown_)
        return false;
    const bool showsOneElement = key.startsWith(QLatin1Char('s')) || key.startsWith(QLatin1Char('l'));
    if (!showsOneElement)
        return true;
    return section_->findChild<QWidget *>() && section_->isAncestorOf(QApplication::focusWidget());
}

void Inspector::rebuild()
{
    if (updating_)
        return;
    const QString key = selectionKey(*board_);
    // Rebuilt only when what it shows changes, so that a field being typed
    // in keeps its focus.
    if (showsAlready(key))
        return;
    shown_ = key;
    if (section_) {
        section_->hide();
        section_->deleteLater();
    }
    if (board_->selectedShape())
        section_ = buildShapeSection();
    else if (board_->selectedLink())
        section_ = buildLinkSection();
    else if (board_->hasSelection())
        section_ = buildSelectionSection();
    else
        section_ = buildDocumentSection();
    propertiesLayout_->addWidget(section_);
    propertiesLayout_->addStretch();
    keepOnlyFinalStretch(propertiesLayout_);
}

QWidget *Inspector::buildSelectionSection()
{
    auto *section = new QWidget(properties_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(sectionTitle(
        tr("%n elements", nullptr, int(board_->selection().size() + board_->linkSelection().size())).toUpper(),
        section));
    if (!board_->selection().isEmpty()) {
        layout->addWidget(fieldLabel(tr("Color"), section));
        layout->addWidget(
            swatchGrid(swatches(), section, [this](const QString &color) { board_->paintSelection(color); }));
    }
    layout->addStretch();
    return section;
}

namespace {

using PropertyGetter = std::function<QVariant(const QString &)>;
using PropertySetter = std::function<void(const QString &, const QVariant &, bool typing)>;

// Where property fields go, and how they reach the element.
struct PropertyForm {
    QWidget *section;
    QVBoxLayout *layout;
    PropertyGetter get;
    PropertySetter set;
};

void addTextField(const PropertyForm &form, const Property &property)
{
    const QString id = property.id;
    const PropertySetter set = form.set;
    form.layout->addWidget(fieldLabel(property.label, form.section));
    auto *field = new QLineEdit(form.get(id).toString(), form.section);
    QObject::connect(field, &QLineEdit::textEdited, form.section,
                     [set, id](const QString &value) { set(id, value, true); });
    QObject::connect(field, &QLineEdit::editingFinished, form.section,
                     [set, id, field] { set(id, field->text(), false); });
    form.layout->addWidget(field);
}

void addLinesField(const PropertyForm &form, const Property &property)
{
    const QString id = property.id;
    const PropertySetter set = form.set;
    form.layout->addWidget(fieldLabel(property.label, form.section));
    auto *field = new QPlainTextEdit(form.get(id).toString(), form.section);
    field->setTabChangesFocus(true);
    field->setFixedHeight(field->fontMetrics().lineSpacing() * 5 + 12);
    QObject::connect(field, &QPlainTextEdit::textChanged, form.section,
                     [set, id, field] { set(id, field->toPlainText(), true); });
    form.layout->addWidget(field);
}

void addChoiceField(const PropertyForm &form, const Property &property)
{
    const QString id = property.id;
    const PropertySetter set = form.set;
    form.layout->addWidget(fieldLabel(property.label, form.section));
    auto *combo = new QComboBox(form.section);
    for (const auto &[value, label] : property.choices)
        combo->addItem(label, value);
    const int at = combo->findData(form.get(id).toString());
    if (at >= 0)
        combo->setCurrentIndex(at);
    QObject::connect(combo, &QComboBox::activated, form.section,
                     [set, id, combo](int index) { set(id, combo->itemData(index), false); });
    form.layout->addWidget(combo);
}

void addBoolField(const PropertyForm &form, const Property &property)
{
    const QString id = property.id;
    const PropertySetter set = form.set;
    auto *box = new QCheckBox(property.label, form.section);
    box->setChecked(form.get(id).toBool());
    QObject::connect(box, &QCheckBox::toggled, form.section, [set, id](bool checked) { set(id, checked, false); });
    form.layout->addWidget(box);
}

void addNumberField(const PropertyForm &form, const Property &property)
{
    const QString id = property.id;
    const PropertySetter set = form.set;
    form.layout->addWidget(fieldLabel(property.label, form.section));
    auto *spin = new QDoubleSpinBox(form.section);
    spin->setRange(property.minimum, property.maximum);
    spin->setValue(form.get(id).toDouble());
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, form.section,
                     [set, id](double value) { set(id, value, false); });
    form.layout->addWidget(spin);
}

// Widgets for the properties of a shape or a link; `get` and `set` reach the
// element (looked up again each time: it may have been rebuilt).
void addPropertyFields(const PropertyForm &form, const QList<Property> &properties)
{
    for (const Property &property : properties) {
        switch (property.kind) {
        case Property::Kind::Text:
            addTextField(form, property);
            break;
        case Property::Kind::Lines:
            addLinesField(form, property);
            break;
        case Property::Kind::Choice:
            addChoiceField(form, property);
            break;
        case Property::Kind::Bool:
            addBoolField(form, property);
            break;
        case Property::Kind::Number:
            addNumberField(form, property);
            break;
        }
    }
}

} // namespace

QWidget *Inspector::buildShapeSection()
{
    const Shape *shape = board_->selectedShape();
    auto *section = new QWidget(properties_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(sectionTitle(typeLabel(shape->type).toUpper(), section));
    const QString id = shape->id;
    addPropertyFields({.section = section,
                       .layout = layout,
                       .get =
                           [this, id](const QString &property) {
                               const Shape *s = board_->shape(id);
                               return s ? s->propertyValue(property) : QVariant();
                           },
                       .set =
                           [this, id](const QString &property, const QVariant &value, bool typing) {
                               Shape *s = board_->shape(id);
                               if (!s)
                                   return;
                               beginTyping();
                               updating_ = true;
                               const QPointF center(s->cx(), s->cy());
                               s->setPropertyValue(property, value);
                               board_->measureShape(*s);
                               s->x = center.x() - s->w / 2;
                               s->y = center.y() - s->h / 2;
                               board_->containShape(*s, true);
                               board_->touch();
                               updating_ = false;
                               if (!typing) {
                                   endTyping();
                                   shown_.clear();
                                   QTimer::singleShot(0, this, &Inspector::rebuild);
                               }
                           }},
                      shape->properties());
    if (codec_ && codec_->writesCodeLinks())
        addCodeLinkField(section, layout, id);
    layout->addWidget(fieldLabel(tr("Color"), section));
    layout->addWidget(swatchGrid(swatches(), section, [this](const QString &color) { board_->paintSelection(color); }));
    auto *buttons = new QHBoxLayout;
    auto *other = new QPushButton(tr("Other…"), section);
    connect(other, &QPushButton::clicked, this, [this, id] {
        const Shape *s = board_->shape(id);
        const QColor initial = s ? parseColor(s->customFill) : QColor();
        const QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::white), this, tr("Color"));
        if (color.isValid())
            board_->paintSelection(hexColor(color));
    });
    auto *reset = new QPushButton(tr("Default"), section);
    connect(reset, &QPushButton::clicked, this, [this] { board_->paintSelection({}); });
    buttons->addWidget(other);
    buttons->addWidget(reset);
    buttons->addStretch();
    layout->addSpacing(swatchButtonsGapPx);
    layout->addLayout(buttons);
    return section;
}

void Inspector::addCodeLinkField(QWidget *section, QVBoxLayout *layout, const QString &shapeId)
{
    const Shape *shape = board_->shape(shapeId);
    layout->addWidget(fieldLabel(tr("Code"), section));
    auto *field = new QLineEdit(shape ? shape->codeLink : QString(), section);
    field->setPlaceholderText(tr("Found by its name"));
    field->setToolTip(tr("What Ctrl+click opens: a symbol (Git::run), a file (src/git/Git.h) "
                         "or a symbol in a file (src/git/Git.h#Git::run)"));
    field->setClearButtonEnabled(true);
    connect(field, &QLineEdit::editingFinished, this, [this, shapeId, field] {
        Shape *s = board_->shape(shapeId);
        const QString link = field->text().trimmed();
        if (!s || s->codeLink == link)
            return;
        beginTyping();
        updating_ = true;
        s->codeLink = link;
        board_->touch();
        updating_ = false;
        endTyping();
    });
    layout->addWidget(field);
}

QWidget *Inspector::buildLinkSection()
{
    const Link *link = board_->selectedLink();
    auto *section = new QWidget(properties_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const Shape *from = board_->shape(link->from);
    const Shape *to = board_->shape(link->to);
    layout->addWidget(sectionTitle(tr("LINK"), section));
    auto *ends = new QLabel(
        QStringLiteral("%1 → %2").arg(from ? from->displayText() : QString(), to ? to->displayText() : QString()),
        section);
    ends->setWordWrap(true);
    layout->addWidget(ends);
    const QString id = link->id;
    QList<Property> properties{{QStringLiteral("label"), tr("Label"), Property::Kind::Text, {}}};
    properties += link->properties();
    addPropertyFields({.section = section,
                       .layout = layout,
                       .get =
                           [this, id](const QString &property) {
                               const Link *l = board_->link(id);
                               if (!l)
                                   return QVariant();
                               return property == QLatin1String("label") ? QVariant(l->label)
                                                                         : l->propertyValue(property);
                           },
                       .set =
                           [this, id](const QString &property, const QVariant &value, bool typing) {
                               Link *l = board_->link(id);
                               if (!l)
                                   return;
                               beginTyping();
                               updating_ = true;
                               if (property == QLatin1String("label"))
                                   l->label = value.toString();
                               else
                                   l->setPropertyValue(property, value);
                               board_->touch();
                               updating_ = false;
                               if (!typing) {
                                   endTyping();
                                   shown_.clear();
                                   QTimer::singleShot(0, this, &Inspector::rebuild);
                               }
                           }},
                      properties);

    layout->addWidget(fieldLabel(tr("Line style"), section));
    auto *line = new QComboBox(section);
    line->addItem(tr("Default"), QString());
    line->addItem(tr("Solid"), QStringLiteral("solid"));
    line->addItem(tr("Dashed"), QStringLiteral("dashed"));
    line->addItem(tr("Dotted"), QStringLiteral("dotted"));
    line->setCurrentIndex(std::max(0, line->findData(link->customLineStyle)));
    connect(line, &QComboBox::activated, this, [this, line](int index) {
        board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customLineStyle"), line->itemData(index).toString()}});
    });
    layout->addWidget(line);
    layout->addWidget(fieldLabel(tr("Line color"), section));
    const QStringList &colors = lineColors();
    layout->addWidget(swatchGrid(colors, section, [this](const QString &color) {
        board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), color}});
    }));
    layout->addWidget(fieldLabel(tr("Line width"), section));
    auto *width = new QComboBox(section);
    width->addItem(tr("Default"), 0);
    for (const int value : {1, 2, 3, 4, 6, 8})
        width->addItem(QString::number(value), value);
    width->setCurrentIndex(std::max(0, width->findData(int(link->customWidth.value_or(0)))));
    connect(width, &QComboBox::activated, this, [this, width](int index) {
        const int value = width->itemData(index).toInt();
        board_->styleSelectedLinks(
            QJsonObject{{QStringLiteral("customWidth"), value ? QJsonValue(value) : QJsonValue()}});
    });
    layout->addWidget(width);
    auto *reset = new QPushButton(tr("Default Appearance"), section);
    connect(reset, &QPushButton::clicked, this, [this] {
        board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customLineStyle"), QString()},
                                               {QStringLiteral("customColor"), QString()},
                                               {QStringLiteral("customWidth"), QJsonValue()}});
    });
    layout->addWidget(reset, 0, Qt::AlignLeft);
    return section;
}

QWidget *Inspector::buildDocumentSection()
{
    auto *section = new QWidget(properties_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const Family *family = board_->family();
    layout->addWidget(sectionTitle((family ? family->title : tr("Diagram")).toUpper(), section));
    auto *hint = new QLabel(tr("Double-click in the diagram to add an element; drag from an element's handle to "
                               "link it."),
                            section);
    hint->setWordWrap(true);
    // A family with a fixed skeleton (product vision) adds nothing.
    hint->setVisible(family && !family->defaultShape.isEmpty());
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);
    if (family && family->hasDirection) {
        layout->addWidget(fieldLabel(tr("Direction"), section));
        auto *direction = new QComboBox(section);
        for (const QString &value : directions)
            direction->addItem(directionLabel(value), value);
        direction->setCurrentIndex(std::max(
            0, direction->findData(board_->meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB")))));
        connect(direction, &QComboBox::activated, this, [this, direction](int index) {
            board_->meta.insert(QStringLiteral("rankdir"), direction->itemData(index).toString());
            canvas_->arrange(true);
        });
        layout->addWidget(direction);
    }
    return section;
}

void Inspector::refreshSource()
{
    if (!codec_ || !isVisible() || source_->hasFocus())
        return;
    if (sourceVersion_ == board_->version())
        return;
    sourceVersion_ = board_->version();
    const QString text = document::source(*board_, *codec_);
    if (text != source_->toPlainText()) {
        const int position = source_->textCursor().position();
        source_->setPlainText(text);
        QTextCursor cursor = source_->textCursor();
        cursor.setPosition(std::min(position, int(text.size())));
        source_->setTextCursor(cursor);
    }
    sourceError_->hide();
}

void Inspector::applySource()
{
    if (!codec_)
        return;
    QString error;
    const std::optional<document::Loaded> loaded =
        document::applySource(*board_, *codec_, source_->toPlainText(), &error);
    if (!loaded) {
        sourceError_->setText(error);
        sourceError_->show();
        return;
    }
    if (loaded->codec)
        codec_ = loaded->codec;
    sourceError_->setVisible(!loaded->warnings.isEmpty());
    sourceError_->setText(loaded->warnings.join(QLatin1Char('\n')));
    sourceVersion_ = ~quint64(0);
    emit sourceApplied(*loaded);
}

} // namespace diagram
