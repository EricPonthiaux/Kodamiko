// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Document.h"

#include <QByteArray>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

// The panel beside a diagram: the selection's properties (those of its
// family, its color, its link style) or, with nothing selected, the
// document's (direction); and the diagram's source, editable.
namespace diagram {

class Board;
class Canvas;
class Codec;

class Inspector : public QWidget {
    Q_OBJECT

public:
    Inspector(Board *board, Canvas *canvas, QWidget *parent = nullptr);

    // The notation the source tab shows.
    void setCodec(const Codec *codec);

signals:
    // The source was applied: the view lays out what needs it.
    void sourceApplied(const diagram::document::Loaded &loaded);

private:
    QWidget *createModeSelector(QStackedWidget *pages);
    void rebuild();
    void refreshSource();
    void applySource();
    // One step of history for a run of typing in a field.
    void beginTyping();
    void endTyping();
    QWidget *buildShapeSection();
    QWidget *buildLinkSection();
    // Several elements selected: their count and a color for the shapes.
    QWidget *buildSelectionSection();
    // Whether the section already shows `key` and must be kept (a field
    // being typed in keeps its focus).
    bool showsAlready(const QString &key) const;
    QWidget *buildDocumentSection();

    Board *board_;
    Canvas *canvas_;
    const Codec *codec_ = nullptr;
    QWidget *properties_;
    QVBoxLayout *propertiesLayout_;
    QWidget *section_ = nullptr;
    QPlainTextEdit *source_;
    QLabel *sourceError_;
    QTimer *sourceTimer_;
    QByteArray typingBase_;
    bool updating_ = false;
    QString shown_; // the selection the section was built for
    quint64 sourceVersion_ = ~quint64(0);
};

} // namespace diagram
