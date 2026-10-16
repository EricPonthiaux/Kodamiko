#pragma once

#include <QSplitter>

// Splitter whose handles show where they are: a thin line along the whole
// handle, and a small grip in its middle to take hold of, in the accent
// color under the mouse.
class GripSplitter : public QSplitter {
    Q_OBJECT

public:
    explicit GripSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    QSplitterHandle *createHandle() override;
};
