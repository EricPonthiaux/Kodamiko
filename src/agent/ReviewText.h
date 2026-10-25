// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewRequest.h"

#include <QPalette>
#include <QString>
#include <QStringList>

#include <chrono>

class ReviewSession;

// What the views of a review (ReviewView, ReviewBar) say alike.
namespace ReviewText {

// The color of an error's text on `palette`'s background.
QString errorColor(const QPalette &palette);
// The color of a warning's text on `palette`'s background.
QString warningColor(const QPalette &palette);

// "`make check` failed with exit code 2: fix the code, or review it anyway."
QString checkFailure(const ReviewRequest::Check &check);

// The reviewer's summary, then "No problems found." when it found none: for
// a session just finished, before a finding is dismissed.
QString summary(const ReviewSession &session);

// The last `count` of `steps` as rich text, one per line, the earlier ones
// muted, the current one with an ellipsis.
QString lastSteps(const QStringList &steps, int count, const QPalette &palette);

// "2:05".
QString elapsed(std::chrono::milliseconds time);

} // namespace ReviewText
