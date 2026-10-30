// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

// Under the merge view's columns (MergeTextPanel), the agent's proposal for
// the conflict the view is at: why, and what to do with it. The lines
// themselves show in the result's column.
class MergeProposalBar : public QFrame {
    Q_OBJECT

public:
    explicit MergeProposalBar(QWidget *parent);

    // Proposal `index` (from 0) of `count`, from `agentName`, because of
    // `reason`.
    void showProposal(int index, int count, const QString &agentName, const QString &reason);

signals:
    void acceptRequested();
    void rejectRequested();
    void acceptAllRequested();
    void previousRequested();
    void nextRequested();

private:
    QLabel *title_;
    QLabel *reason_;
    QPushButton *previous_;
    QPushButton *next_;
    QPushButton *acceptAll_;
};
