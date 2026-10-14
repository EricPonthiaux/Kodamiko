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

// Volet Structure : les symboles du fichier de l'éditeur courant (fonctions,
// classes, titres...). Un clic place le curseur sur le symbole ; le symbole
// qui contient le curseur est sélectionné. La structure est recalculée peu
// après chaque modification du texte, seulement quand le volet est visible.
class OutlineView : public QWidget {
    Q_OBJECT

public:
    explicit OutlineView(QWidget *parent = nullptr);
    ~OutlineView() override;

    // Éditeur à suivre (nullptr : aucun).
    void setEditor(EditorWidget *editor);
    // Donne le focus au champ de filtre.
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

    // Éléments repliés par l'utilisateur, par fichier, conservés d'un calcul à l'autre.
    QHash<QString, QSet<QString>> collapsedPaths_;

    QLineEdit *filter_;
    QTreeWidget *tree_;
    QLabel *placeholder_;
    QStackedWidget *stack_;
};
