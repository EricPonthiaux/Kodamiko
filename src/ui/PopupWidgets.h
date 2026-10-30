// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HoverProgress.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QFrame>
#include <QStyledItemDelegate>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

// Controls of the popups built like Preferences (a header, pages of
// sections, a footer), drawn in the application's palette: square-cut, as
// the editor is.

// Style sheet of these popups' cards, headings, descriptions and footer,
// followed by `extra` (rules of one popup). Both can use the theme's colors
// as @accent, @accentText, @text, @muted, @window, @panel, @base, @line,
// @hover, @selection, @slate (ThemeStyle::selectionColor), @error and
// @warning. Its lists and trees (QListWidget, QTreeWidget) are flat, their
// rows selected and hovered as everywhere in the popups. Object names:
// "popupHeader", "settingsCard", "settingsSeparator", "settingsSection",
// "settingsDescription", "pageDescription", "footerHint", "popupFooter";
// "popupError" and "popupWarning" (or a label whose "error" property is
// true) for a message; for a popup in two columns, "popupSidebar" (the
// left one), "popupNav" (its list), "navHeading" (a title above it) and
// "settingState" (a line under a field).
QString popupStyleSheet(const Theme &theme, const QString &extra = {});

// A theme to choose: a miniature editor in its colors, with its name.
class ThemeCard : public QAbstractButton {
    Q_OBJECT

public:
    explicit ThemeCard(const Theme &theme, QWidget *parent = nullptr);

    QString themeName() const { return theme_.name; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Theme theme_;
    HoverProgress hover_{this};
};

// A section of settings: rows separated by thin lines, flush with the
// section's heading (sectionHeading(), underlined). Each row has a title, an optional
// description and its control: a check box before the title, any other
// control on the right. TogglePlacement::AfterTitle aligns switches on the right too.
class SettingsCard : public QFrame {
    Q_OBJECT

public:
    enum class TogglePlacement {
        BeforeTitle,
        AfterTitle
    };

    explicit SettingsCard(QWidget *parent = nullptr, TogglePlacement toggles = TogglePlacement::BeforeTitle);
    // Returns the row, to hide it with its control.
    QWidget *addRow(const QString &title, const QString &description, QWidget *control);
    // A row taking the card's whole width.
    void addWidget(QWidget *widget);

private:
    void addSeparator();
    QVBoxLayout *layout_;
    TogglePlacement toggles_;
};

// The header of a popup: a 16-pixel icon, the title in demibold, an
// optional line of text under it, the popup's own buttons, a close button;
// a line under it all.
class PopupHeader : public QFrame {
    Q_OBJECT

public:
    enum class Style {
        Standard,
        Compact
    };

    // Compact matches the welcome title bar; intended for a title without a subtitle.
    PopupHeader(QString iconName, const QString &title, QWidget *parent, Style style = Style::Standard);

    void setTitle(const QString &title);

    // Shown from the next applyTheme().
    void setIconName(const QString &iconName) { iconName_ = iconName; }

    // Shown under the title; hidden while empty.
    void setSubtitle(const QString &text);
    // Before the close button, in the order added.
    void addButton(QWidget *button);
    void applyTheme(const Theme &theme);

signals:
    void closeRequested();

private:
    QString iconName_;
    QLabel *icon_;
    QLabel *title_;
    QLabel *subtitle_;
    QHBoxLayout *buttons_;
    QToolButton *close_;
};

// The title of a section, in demibold.
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
