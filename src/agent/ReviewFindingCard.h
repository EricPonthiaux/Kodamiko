// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewReport.h"

#include <QWidget>

class QPushButton;
class QCheckBox;
class QLabel;
class QToolButton;

// A finding in a list of findings (ReviewFindingsList): a stripe in the
// color of its severity, its severity and line, what is wrong, the code
// around it (ReviewExcerpt), an expandable scenario, and visible actions.
// A finding handed over to be fixed says so, with Dismiss only.
class ReviewFindingCard : public QWidget {
    Q_OBJECT

public:
    enum class Status {
        Open,
        Sent // handed over to be fixed (ReviewSession::isResolving())
    };

    // `agentName` names the Fix button and the handed-over mark.
    ReviewFindingCard(const ReviewReport::Finding &finding, Status status, const QString &agentName, QWidget *parent);

    const ReviewReport::Finding &finding() const { return finding_; }

    void setSelected(bool selected);
    // The current card has the keyboard: a frame in the accent color.
    void setCurrent(bool current);
    // Keeps the failure scenario open across list rebuilds.
    void setExpanded(bool expanded);

signals:
    // A click on the card, with the keys held.
    void pressed(Qt::KeyboardModifiers modifiers);
    // A double click, or a click on the line: the finding's place.
    void activated();
    void resolveRequested();
    void dismissRequested();
    void detailsToggled(bool expanded);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QLayout *createHeader(Status status, const QString &agentName);
    QWidget *createActions(Status status, const QString &agentName);
    QPushButton *createAction(const QString &text, const QString &toolTip);
    QColor severityColor() const;

    ReviewReport::Finding finding_;
    QWidget *actions_ = nullptr;
    QCheckBox *selection_ = nullptr;
    QLabel *scenario_ = nullptr;
    QToolButton *details_ = nullptr;
    bool isSelected_ = false;
    bool isCurrent_ = false;
    bool isHovered_ = false;
};
