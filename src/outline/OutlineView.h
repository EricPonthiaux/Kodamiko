#pragma once

#include "OutlineSymbol.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <memory>

class EditorWidget;
class IOutlineProvider;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class TreeColumnFit;

// Outline pane: the symbols of the current editor's file (functions,
// classes, headings...). A click places the cursor on the symbol; the symbol
// containing the cursor is selected. The outline is recomputed shortly
// after each text change, only while the pane is visible.
class OutlineView : public QWidget {
    Q_OBJECT

public:
    explicit OutlineView(QWidget *parent = nullptr);
    ~OutlineView() override;

    // Editor to follow (nullptr: none).
    void setEditor(EditorWidget *editor);
    // Gives focus to the filter field.
    void focusFilter();

    QTreeWidget *tree() const { return tree_; }

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateProvider();
    void scheduleRefresh();
    void refresh();
    void populate(const std::vector<OutlineSymbol> &symbols);
    void applyFilter();
    void selectSymbolAtCursor();
    void jumpTo(QTreeWidgetItem *item);
    QTreeWidgetItem *firstMatchingItem() const;
    QString pathOf(QTreeWidgetItem *item) const;

    QPointer<EditorWidget> editor_;
    std::unique_ptr<IOutlineProvider> provider_;
    QString providerLanguage_;
    QList<QMetaObject::Connection> editorConnections_;
    QTimer refreshTimer_;
    bool needsRefresh_ = false;

    // Items collapsed by the user, per file, kept from one computation to the next.
    QHash<QString, QSet<QString>> collapsedPaths_;

    QLineEdit *filter_;
    QTreeWidget *tree_;
    TreeColumnFit *columnFit_;
    QLabel *placeholder_;
    QStackedWidget *stack_;
};
