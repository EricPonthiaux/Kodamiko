#pragma once

#include <QFrame>
#include <QPointer>

// Panel shown over the window, in the manner of IntelliJ's popups (Search
// Everywhere, Find in Files): centered in the window, with a soft shadow
// and rounded corners. Its edges and corners resize it, like a window's;
// until the user does, its size follows the window's. Dragged from a place
// without a control of its own (its header, its footer), it moves; both
// the size and the place are kept. A click elsewhere leaves it open: only
// Escape (handled by the subclass), its close button or its own buttons
// close it; the focus then goes back where it was.
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
    // Forgets the size and the place chosen by the user.
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
    // Window resized (seen by a separate object: the subclasses filter
    // their own children's events).
    void onWatchedEvent(QObject *watched, QEvent *event);
    // Room the window offers: its contents, less a margin.
    QRect availableArea() const;
    QSize panelSize() const;
    // Where the panel goes, shadow included: centered, then moved by the
    // user's offset, within the window.
    QRect placedGeometry() const;
    // `geometry` moved into the window's room.
    QRect keptInside(QRect geometry) const;
    void place();
    void saveOffset();
    // Where the user's place is kept, beside the size.
    QString offsetKey() const;
    // Room for the shadow around the contents.
    void applyMargins();

    QString settingsKey_;
    QSizeF share_;
    QSize minimum_;
    Watcher *watcher_;
    QPointer<QWidget> previousFocus_;
    // Resizing in progress: the edges dragged; or moving.
    Qt::Edges resizeEdges_;
    bool isMoving_ = false;
    QPoint dragStart_; // in the window
    QRect dragGeometry_;
};
