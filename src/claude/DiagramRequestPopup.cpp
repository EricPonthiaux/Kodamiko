#include "DiagramRequestPopup.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

DiagramRequestPopup::DiagramRequestPopup(const Form &form, Apply apply, QWidget *parent)
    : EntryPopup(tr("Diagram Drawn by Claude"), form.location, parent)
    , apply_(std::move(apply))
    , kind_(new QComboBox(this))
    , description_(new QPlainTextEdit(this))
    , name_(new QLineEdit(this))
    , draw_(new QPushButton(tr("Draw"), this))
{
    for (const DiagramKind &kind : form.kinds)
        kind_->addItem(kind.title, kind.id);

    description_->setPlaceholderText(tr("What should it show? “How an order is returned”, "
                                        "“the login flow in src/auth”…"));
    description_->setTabChangesFocus(true);
    // Ctrl+Enter draws; Enter alone starts a new line.
    description_->installEventFilter(this);
    description_->setFixedHeight(description_->fontMetrics().lineSpacing() * 5 + 12);

    name_->setPlaceholderText(tr("File name, with “/” to create subfolders"));
    name_->setText(form.initialName);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    draw_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QWidget::close);
    connect(draw_, &QPushButton::clicked, this, &DiagramRequestPopup::accept);
    connect(name_, &QLineEdit::returnPressed, this, &DiagramRequestPopup::accept);
    connect(name_, &QLineEdit::textEdited, this, [this] { showError({}); });
    connect(description_, &QPlainTextEdit::textChanged, this, [this] { showError({}); });

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(draw_);
    content()->addWidget(kind_);
    content()->addWidget(description_);
    content()->addWidget(name_);
    content()->addLayout(buttons);
}

namespace {

bool isControlEnter(const QKeyEvent *event)
{
    return (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
           event->modifiers().testFlag(Qt::ControlModifier);
}

} // namespace

bool DiagramRequestPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == description_ && event->type() == QEvent::KeyPress &&
        isControlEnter(static_cast<QKeyEvent *>(event))) {
        accept();
        return true;
    }
    return EntryPopup::eventFilter(watched, event);
}

void DiagramRequestPopup::keyPressEvent(QKeyEvent *event)
{
    if (isControlEnter(event)) {
        accept();
        return;
    }
    EntryPopup::keyPressEvent(event);
}

void DiagramRequestPopup::accept()
{
    const QString message =
        apply_(kind_->currentData().toString(), name_->text().trimmed(), description_->toPlainText().trimmed());
    if (message.isEmpty())
        close();
    else
        showError(message);
}

void DiagramRequestPopup::focusContent()
{
    description_->setFocus();
}
