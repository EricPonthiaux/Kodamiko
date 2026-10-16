#pragma once

#include <QFrame>
#include <QPointer>

// Panel shown over the window, in the manner of IntelliJ's popups (Search
// Everywhere, Find in Files): centered in the window, with a soft shadow
// and rounded corners. Its edges and corners resize it, like a window's;
// until the user does, its size follows the window's. Escape (handled by
// the subclass) or a click elsewhere in the window closes it; the focus
// then goes back where it was.
//
// It is a child of the window rather than a window of its own: Wayland
// does not let an application place its windows, so only a child is
// reliably centered.
class OverlayPopup : public QFrame {
    Q_OBJECT

public:
    // Width of the shadow, around the panel; it also takes the resizing.
    static constexpr int shadowWidth = 16;

    // `settingsKey` keeps the size chosen by the user. Until then, the panel
    // takes `share` of the window's width and height, at least `minimum`
    // (shadow excluded).
    OverlayPopup(const QString &settingsKey, const QSizeF &share, const QSize &minimum, QWidget *parent);

    // Shows it centered in the parent's window.
    void showCentered();
    // Hides it and gives the focus back.
    void dismiss();
    // Forgets the size chosen by the user.
    void resetSize();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    class Watcher;
    // Window resized, click elsewhere (seen by a separate object: the
    // subclasses filter their own children's events).
    void onWatchedEvent(QObject *watched, QEvent *event);
    // Room the window offers: its contents, less a margin.
    QRect availableArea() const;
    QSize panelSize() const;
    void place();
    // Room for the shadow around the contents.
    void applyMargins();

    QString settingsKey_;
    QSizeF share_;
    QSize minimum_;
    Watcher *watcher_;
    QPointer<QWidget> previousFocus_;
    // Resizing in progress: the edges dragged, from where.
    Qt::Edges resizeEdges_;
    QPoint resizeStart_; // in the window
    QRect resizeGeometry_;
};
