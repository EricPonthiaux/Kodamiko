#pragma once

#include <QWidget>

class Toolbar : public QWidget {
    Q_OBJECT

public:
    explicit Toolbar(QWidget *parent = nullptr);

signals:
    void clicked();
};
