// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramRequestPopup.h"

#include "AgentProvider.h"
#include "ui/PopupWidgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *diagramRules = R"(
QLabel#diagramError { color: #e06c75; }
)";

constexpr int descriptionLines = 5;

bool isControlEnter(const QKeyEvent *event)
{
    return (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
           event->modifiers().testFlag(Qt::ControlModifier);
}

// A label above its field, in the card's whole width.
QWidget *labeledBlock(const QString &label, QWidget *field, QWidget *parent)
{
    auto *block = new QWidget(parent);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(new QLabel(label, block));
    layout->addWidget(field);
    return block;
}

} // namespace

DiagramRequestPopup::DiagramRequestPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("diagramRequest"), QSizeF(0.4, 0.42), QSize(600, 420), parent)
    , header_(new PopupHeader(QString(), QString(), this))
    , kind_(new QComboBox(this))
    , description_(new QPlainTextEdit(this))
    , name_(new QLineEdit(this))
    , error_(new QLabel(this))
    , draw_(new QPushButton(tr("Draw"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    description_->setPlaceholderText(tr("What should it show? “How an order is returned”, "
                                        "“the login flow in src/auth”…"));
    description_->setTabChangesFocus(true);
    // Ctrl+Enter draws; Enter alone starts a new line.
    description_->installEventFilter(this);
    description_->setFixedHeight(description_->fontMetrics().lineSpacing() * descriptionLines + 12);
    connect(description_, &QPlainTextEdit::textChanged, this, [this] { showError({}); });

    name_->setPlaceholderText(tr("File name, with “/” to create subfolders"));
    connect(name_, &QLineEdit::textEdited, this, [this] { showError({}); });
    connect(name_, &QLineEdit::returnPressed, this, &DiagramRequestPopup::draw);

    error_->setObjectName(QStringLiteral("diagramError"));
    error_->setWordWrap(true);
    error_->hide();

    auto *card = new SettingsCard(this);
    card->addRow(tr("Kind"), tr("The notation of the diagram."), kind_);
    card->addWidget(labeledBlock(tr("Description"), description_, this));
    auto *fileBlock = labeledBlock(tr("File"), name_, this);
    fileBlock->layout()->addWidget(error_);
    card->addWidget(fileBlock);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 12, 16, 12);
    body->addWidget(card);
    body->addStretch();

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Ctrl+Enter to draw · Esc to cancel"), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    cancel->setMinimumWidth(84);
    draw_->setParent(footer);
    draw_->setMinimumWidth(84);
    draw_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(draw_, &QPushButton::clicked, this, &DiagramRequestPopup::draw);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(draw_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void DiagramRequestPopup::popup(const Form &form, Apply apply)
{
    apply_ = std::move(apply);
    header_->setSubtitle(tr("In %1").arg(form.location));
    kind_->clear();
    for (const DiagramKind &kind : form.kinds)
        kind_->addItem(kind.title, kind.id);
    description_->clear();
    name_->setText(form.initialName);
    showError({});
    showCentered();
    description_->setFocus();
}

void DiagramRequestPopup::setProvider(const AgentProvider &provider)
{
    header_->setTitle(tr("Diagram Drawn by %1").arg(provider.name()));
    header_->setIconName(provider.iconName());
}

void DiagramRequestPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(diagramRules)));
    header_->applyTheme(theme);
}

void DiagramRequestPopup::draw()
{
    if (!apply_)
        return;
    const QString message =
        apply_(kind_->currentData().toString(), name_->text().trimmed(), description_->toPlainText().trimmed());
    if (message.isEmpty())
        dismiss();
    else
        showError(message);
}

bool DiagramRequestPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == description_ && event->type() == QEvent::KeyPress &&
        isControlEnter(static_cast<QKeyEvent *>(event))) {
        draw();
        return true;
    }
    return OverlayPopup::eventFilter(watched, event);
}

void DiagramRequestPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        draw();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}

void DiagramRequestPopup::showError(const QString &message)
{
    error_->setText(message);
    error_->setVisible(!message.isEmpty());
}
