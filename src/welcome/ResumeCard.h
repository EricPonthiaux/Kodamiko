// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "ui/HoverProgress.h"
#include "ui/Theme.h"

#include <QAbstractButton>

// The welcome window's card for the project opened last, above the others:
// "Continue where you left off", its initials, name and folder, then the
// languages it is set up for and when it was opened. A click (or Enter,
// Space) opens it, as the button's clicked() says.
class ResumeCard : public QAbstractButton {
    Q_OBJECT

public:
    ResumeCard(Theme theme, QWidget *parent);

    void setProject(const ProjectConfig &project);

    const ProjectConfig &project() const { return project_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void paintBackground(QPainter &painter) const;
    // Returns the bottom of what it painted, for the part below.
    qreal paintKicker(QPainter &painter, qreal top) const;
    qreal paintIdentity(QPainter &painter, qreal top) const;
    void paintDetails(QPainter &painter, qreal top) const;

    Theme theme_;
    ProjectConfig project_;
    HoverProgress hover_{this};
};
