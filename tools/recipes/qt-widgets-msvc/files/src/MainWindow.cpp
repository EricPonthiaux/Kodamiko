#include "MainWindow.h"

#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("{{identifier}}"));
    auto *greeting = new QLabel(tr("Hello from {{identifier}}!"), this);
    greeting->setAlignment(Qt::AlignCenter);
    setCentralWidget(greeting);
    resize(640, 400);
}
