#pragma once

#include <QColor>
#include <QWidget>

class QTabBar;

// The line under the tabs of a document-mode tab bar, but under the current
// tab, which opens onto its document. In place of the style's base line,
// which some styles (Fusion) draw at the top of the bar; drawn over the bar,
// which would cover a line drawn by its tab widget.
class TabBarBaseLine : public QWidget {
    Q_OBJECT

public:
    // Over `bar`, which owns it; turns the bar's own base off.
    explicit TabBarBaseLine(QTabBar *bar);

    void setColor(const QColor &color);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTabBar *bar_;
    QColor color_;
};
