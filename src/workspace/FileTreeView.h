#pragma once

#include <QTreeView>

class QFileSystemModel;

// Arborescence du dossier de travail ; un double-clic ouvre le fichier.
class FileTreeView : public QTreeView {
    Q_OBJECT

public:
    explicit FileTreeView(QWidget *parent = nullptr);

    void setRootPath(const QString &path);

signals:
    void fileActivated(const QString &path);

private:
    QFileSystemModel *model_;
};
