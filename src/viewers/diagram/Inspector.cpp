#include "Inspector.h"

#include "Board.h"
#include "Canvas.h"
#include "Document.h"
#include "Family.h"
#include "ui/AppFonts.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace diagram {

namespace {

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

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(scroll, tr("Properties"));
    tabs->addTab(sourcePage, tr("Source"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);

    sourceTimer_->setSingleShot(true);
    sourceTimer_->setInterval(250);
    connect(sourceTimer_, &QTimer::timeout, this, &Inspector::refreshSource);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int) { refreshSource(); });
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

void Inspector::rebuild()
{
    if (updating_)
        return;
    QString key;
    if (const Shape *s = board_->selectedShape())
        key = QStringLiteral("s:") + s->id + s->type;
    else if (const Link *l = board_->selectedLink())
        key = QStringLiteral("l:") + l->id + l->type;
    else if (board_->hasSelection())
        key = QStringLiteral("many");
    // Rebuilt only when what it shows changes, so that a field being typed
    // in keeps its focus.
    if (section_ && key == shown_ && !key.startsWith(QLatin1Char('s')) && !key.startsWith(QLatin1Char('l')))
        return;
    if (section_ && key == shown_ && section_->findChild<QWidget *>() &&
        section_->isAncestorOf(QApplication::focusWidget()))
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
    else if (board_->hasSelection()) {
        section_ = new QWidget(properties_);
        auto *layout = new QVBoxLayout(section_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(sectionTitle(
            tr("%n elements", nullptr, int(board_->selection().size() + board_->linkSelection().size())).toUpper(),
            section_));
        if (!board_->selection().isEmpty()) {
            layout->addWidget(fieldLabel(tr("Color"), section_));
            layout->addWidget(
                swatchGrid(swatches(), section_, [this](const QString &color) { board_->paintSelection(color); }));
        }
        layout->addStretch();
    } else {
        section_ = buildDocumentSection();
    }
    propertiesLayout_->addWidget(section_);
    propertiesLayout_->addStretch();
    // Only one stretch at the end.
    for (int i = propertiesLayout_->count() - 2; i >= 0; --i) {
        if (propertiesLayout_->itemAt(i)->spacerItem())
            delete propertiesLayout_->takeAt(i);
    }
}

namespace {

// Widgets for the properties of a shape or a link; `get` and `set` reach the
// element (looked up again each time: it may have been rebuilt).
void addPropertyFields(QWidget *section, QVBoxLayout *layout, const QList<Property> &properties,
                       const std::function<QVariant(const QString &)> &get,
                       const std::function<void(const QString &, const QVariant &, bool typing)> &set)
{
    for (const Property &property : properties) {
        const QString id = property.id;
        switch (property.kind) {
        case Property::Kind::Text: {
            layout->addWidget(fieldLabel(property.label, section));
            auto *field = new QLineEdit(get(id).toString(), section);
            QObject::connect(field, &QLineEdit::textEdited, section,
                             [set, id](const QString &value) { set(id, value, true); });
            QObject::connect(field, &QLineEdit::editingFinished, section,
                             [set, id, field] { set(id, field->text(), false); });
            layout->addWidget(field);
            break;
        }
        case Property::Kind::Lines: {
            layout->addWidget(fieldLabel(property.label, section));
            auto *field = new QPlainTextEdit(get(id).toString(), section);
            field->setTabChangesFocus(true);
            field->setFixedHeight(field->fontMetrics().lineSpacing() * 5 + 12);
            QObject::connect(field, &QPlainTextEdit::textChanged, section,
                             [set, id, field] { set(id, field->toPlainText(), true); });
            layout->addWidget(field);
            break;
        }
        case Property::Kind::Choice: {
            layout->addWidget(fieldLabel(property.label, section));
            auto *combo = new QComboBox(section);
            for (const auto &[value, label] : property.choices)
                combo->addItem(label, value);
            const int at = combo->findData(get(id).toString());
            if (at >= 0)
                combo->setCurrentIndex(at);
            QObject::connect(combo, &QComboBox::activated, section,
                             [set, id, combo](int index) { set(id, combo->itemData(index), false); });
            layout->addWidget(combo);
            break;
        }
        case Property::Kind::Bool: {
            auto *box = new QCheckBox(property.label, section);
            box->setChecked(get(id).toBool());
            QObject::connect(box, &QCheckBox::toggled, section, [set, id](bool checked) { set(id, checked, false); });
            layout->addWidget(box);
            break;
        }
        case Property::Kind::Number: {
            layout->addWidget(fieldLabel(property.label, section));
            auto *spin = new QDoubleSpinBox(section);
            spin->setRange(property.minimum, property.maximum);
            spin->setValue(get(id).toDouble());
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, section,
                             [set, id](double value) { set(id, value, false); });
            layout->addWidget(spin);
            break;
        }
        }
    }
}

} // namespace

QWidget *Inspector::buildShapeSection()
{
    Shape *shape = board_->selectedShape();
    auto *section = new QWidget(properties_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(sectionTitle(typeLabel(shape->type).toUpper(), section));
    const QString id = shape->id;
    addPropertyFields(
        section, layout, shape->properties(),
        [this, id](const QString &property) {
            const Shape *s = board_->shape(id);
            return s ? s->propertyValue(property) : QVariant();
        },
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
        });
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
    layout->addLayout(buttons);
    return section;
}

QWidget *Inspector::buildLinkSection()
{
    Link *link = board_->selectedLink();
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
    addPropertyFields(
        section, layout, properties,
        [this, id](const QString &property) {
            const Link *l = board_->link(id);
            if (!l)
                return QVariant();
            return property == QLatin1String("label") ? QVariant(l->label) : l->propertyValue(property);
        },
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
        });

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
    QStringList colors = lineColors();
    layout->addWidget(swatchGrid(colors, section, [this](const QString &color) {
        board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), color}});
    }));
    layout->addWidget(fieldLabel(tr("Line width"), section));
    auto *width = new QComboBox(section);
    width->addItem(tr("Default"), 0);
    for (int value : {1, 2, 3, 4, 6, 8})
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
    document::Loaded loaded;
    QString error;
    if (!document::applySource(*board_, *codec_, source_->toPlainText(), loaded, &error)) {
        sourceError_->setText(error);
        sourceError_->show();
        return;
    }
    if (loaded.codec)
        codec_ = loaded.codec;
    sourceError_->setVisible(!loaded.warnings.isEmpty());
    sourceError_->setText(loaded.warnings.join(QLatin1Char('\n')));
    sourceVersion_ = ~quint64(0);
    emit sourceApplied(loaded);
}

} // namespace diagram
