// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QCheckBox>

// A compact switch with QCheckBox's keyboard and accessibility behavior.
// Its containing settings row supplies the label and accessible name.
class SettingsSwitch : public QCheckBox {
public:
    explicit SettingsSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool hitButton(const QPoint &point) const override;
    void paintEvent(QPaintEvent *event) override;
};
