#pragma once

#include <QWidget>

class Panel : public QWidget {
    Q_OBJECT
};

class §Toolbar : public QWidget {
    Q_OBJECT

public:
    explicit Toolbar(QWidget *parent = nullptr);

signals:
    void clicked();
};
