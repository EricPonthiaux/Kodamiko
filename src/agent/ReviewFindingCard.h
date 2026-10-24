// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewReport.h"

#include <QFrame>

// A finding in a review's tab (ReviewView): its place, its severity, what
// is wrong and when it fails, with Send to <agent> and Dismiss.
class ReviewFindingCard : public QFrame {
    Q_OBJECT

public:
    // `location` is the finding's place as shown ("src/a.cpp:12");
    // `agentName` names the Send button.
    ReviewFindingCard(const ReviewReport::Finding &finding,
                      const QString &location,
                      const QString &agentName,
                      QWidget *parent);

signals:
    // A click on the place.
    void openRequested();
    void sendRequested();
    void dismissRequested();
};
