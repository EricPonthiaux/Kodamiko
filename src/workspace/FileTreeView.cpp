#include "FileTreeView.h"

#include <QFileSystemModel>
#include <QHeaderView>

FileTreeView::FileTreeView(QWidget *parent)
    : QTreeView(parent)
    , model_(new QFileSystemModel(this))
{
    model_->setReadOnly(true);
    model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    setModel(model_);

    // Seule la colonne « Nom » est utile ici (taille, type, date masqués).
    for (int column = 1; column < model_->columnCount(); ++column)
        hideColumn(column);
    header()->hide();
    setAnimated(false);
    setUniformRowHeights(true);

    connect(this, &QTreeView::activated, this, [this](const QModelIndex &index) {
        if (!model_->isDir(index))
            emit fileActivated(model_->filePath(index));
    });
}

void FileTreeView::setRootPath(const QString &path)
{
    setRootIndex(model_->setRootPath(path));
}
