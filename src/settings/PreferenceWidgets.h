#pragma once

#include "syntax/Theme.h"

#include <QAbstractButton>
#include <QFrame>
#include <QStyledItemDelegate>

class QVariantAnimation;
class QVBoxLayout;

// Controls of the Preferences popup, drawn in the application's palette.

// On/off switch: a pill whose knob slides across.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &pos) const override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QVariantAnimation *animation_;
    qreal position_ = 0; // 0 off, 1 on
    bool hovered_ = false;
};

// A theme to choose: a miniature editor in its colors, with its name.
class ThemeCard : public QAbstractButton {
    Q_OBJECT

public:
    explicit ThemeCard(const Theme &theme, QWidget *parent = nullptr);
    QString themeName() const { return theme_.name; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Theme theme_;
    bool hovered_ = false;
};

// Rounded card holding rows of settings separated by thin lines; each row
// has a title, an optional description and its control on the right.
class SettingsCard : public QFrame {
    Q_OBJECT

public:
    explicit SettingsCard(QWidget *parent = nullptr);
    // Returns the row, to hide it with its control.
    QWidget *addRow(const QString &title, const QString &description, QWidget *control);
    // A row taking the card's whole width.
    void addWidget(QWidget *widget);

private:
    void addSeparator();
    QVBoxLayout *layout_;
};

// Small uppercase heading above a card.
QWidget *sectionHeading(const QString &text);

// Shortcuts drawn as keyboard keys ("Ctrl" "Shift" "F"). The item's
// Qt::UserRole + 1 holds the shortcuts as portable text, as
// QKeySequence::listToString writes them.
class KeycapDelegate : public QStyledItemDelegate {
public:
    static constexpr int shortcutsRole = Qt::UserRole + 1;

    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Keys of each shortcut: {{"Ctrl", "K"}, {"Ctrl", "O"}} for "Ctrl+K, Ctrl+O".
    static QList<QStringList> keysOf(const QKeySequence &shortcut);
};
