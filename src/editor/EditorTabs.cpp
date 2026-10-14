#include "EditorTabs.h"

#include "EditorWidget.h"
#include "ImageView.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QTabBar>

EditorTabs::EditorTabs(const Theme &theme, QWidget *parent)
    : QTabWidget(parent)
    , theme_(theme)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    connect(this, &QTabWidget::tabCloseRequested, this, &EditorTabs::closeTab);
    connect(&watcher_, &FileWatcher::fileChanged, this, &EditorTabs::syncWithDisk);
}

QWidget *EditorTabs::openFile(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (EditorWidget *existing = editorFor(absolutePath)) {
        setCurrentWidget(existing);
        return existing;
    }
    if (ImageView *existing = imageViewFor(absolutePath)) {
        setCurrentWidget(existing);
        return existing;
    }
    if (ImageView::isImageFile(absolutePath))
        return openImage(absolutePath);

    auto *editor = new EditorWidget(theme_, this);
    QString error;
    if (!editor->load(absolutePath, &error)) {
        delete editor;
        QMessageBox::warning(this, tr("Ouverture impossible"),
                             tr("Impossible d'ouvrir %1 :\n%2").arg(absolutePath, error));
        return nullptr;
    }

    const int index = addTab(editor, QFileInfo(absolutePath).fileName());
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(editor, &EditorWidget::modificationChanged, this, [this, editor] { updateTabTitle(editor); });
    watcher_.watch(absolutePath);
    editor->setFocus();
    return editor;
}

QWidget *EditorTabs::openImage(const QString &absolutePath)
{
    auto *view = new ImageView(theme_, this);
    QString error;
    if (!view->load(absolutePath, &error)) {
        delete view;
        QMessageBox::warning(this, tr("Ouverture impossible"),
                             tr("Impossible d'ouvrir %1 :\n%2").arg(absolutePath, error));
        return nullptr;
    }

    const int index = addTab(view, QFileInfo(absolutePath).fileName());
    setTabToolTip(index, absolutePath);
    setCurrentIndex(index);
    connect(view, &ImageView::zoomChanged, this, [this, view] {
        if (currentWidget() == view)
            emit statusMessage(view->description());
    });
    emit statusMessage(view->description());
    watcher_.watch(absolutePath);
    view->setFocus();
    return view;
}

void EditorTabs::showDiff(const QString &title, const QString &diffText)
{
    auto *view = new EditorWidget(theme_, this);
    view->showReadOnlyText(diffText.isEmpty() ? tr("(aucune différence textuelle)") : diffText,
                           QStringLiteral("diff"));
    const int index = addTab(view, title);
    setCurrentIndex(index);
}

EditorWidget *EditorTabs::currentEditor() const
{
    return qobject_cast<EditorWidget *>(currentWidget());
}

EditorWidget *EditorTabs::editorFor(const QString &path) const
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (EditorWidget *editor : editors()) {
        if (editor->filePath() == absolutePath)
            return editor;
    }
    return nullptr;
}

ImageView *EditorTabs::imageViewFor(const QString &path) const
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < count(); ++i) {
        auto *view = qobject_cast<ImageView *>(widget(i));
        if (view && view->filePath() == absolutePath)
            return view;
    }
    return nullptr;
}

QList<EditorWidget *> EditorTabs::editors() const
{
    QList<EditorWidget *> result;
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(widget(i)))
            result.append(editor);
    }
    return result;
}

bool EditorTabs::saveCurrent()
{
    EditorWidget *editor = currentEditor();
    return !editor || saveEditor(editor);
}

bool EditorTabs::saveAll()
{
    bool allSaved = true;
    for (EditorWidget *editor : editors()) {
        if (editor->isModified())
            allSaved = saveEditor(editor) && allSaved;
    }
    return allSaved;
}

bool EditorTabs::saveEditor(EditorWidget *editor)
{
    QString error;
    if (!editor->save(&error)) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
                             tr("Impossible d'enregistrer %1 :\n%2").arg(editor->filePath(), error));
        return false;
    }
    emit statusMessage(tr("%1 enregistré").arg(QFileInfo(editor->filePath()).fileName()));
    return true;
}

void EditorTabs::closeCurrent()
{
    if (currentIndex() >= 0)
        closeTab(currentIndex());
}

bool EditorTabs::closeTab(int index)
{
    auto *editor = qobject_cast<EditorWidget *>(widget(index));
    if (editor && editor->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Modifications non enregistrées"),
            tr("Enregistrer les modifications de %1 ?").arg(QFileInfo(editor->filePath()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveEditor(editor)))
            return false;
    }
    if (editor && !editor->filePath().isEmpty())
        watcher_.unwatch(editor->filePath());
    if (auto *image = qobject_cast<ImageView *>(widget(index)))
        watcher_.unwatch(image->filePath());
    QWidget *page = widget(index);
    removeTab(index);
    page->deleteLater();
    return true;
}

bool EditorTabs::closeAll()
{
    while (count() > 0) {
        if (!closeTab(count() - 1))
            return false;
    }
    return true;
}

void EditorTabs::syncWithDisk(const QString &path)
{
    if (ImageView *image = imageViewFor(path)) {
        // Une image n'est jamais modifiée dans l'éditeur : on la relit simplement.
        QString error;
        if (QFileInfo::exists(path) && !image->load(image->filePath(), &error))
            emit statusMessage(tr("Rechargement de %1 impossible : %2").arg(QFileInfo(path).fileName(), error));
        return;
    }

    EditorWidget *editor = editorFor(path);
    if (!editor || pathsBeingResolved_.contains(editor->filePath()))
        return;
    const QString name = QFileInfo(path).fileName();

    if (!QFileInfo::exists(path)) {
        emit statusMessage(tr("%1 a été supprimé du disque").arg(name));
        return;
    }
    if (editor->matchesDisk())
        return; // notre propre enregistrement, ou contenu identique

    if (editor->isModified()) {
        pathsBeingResolved_.insert(editor->filePath());
        const auto answer = QMessageBox::question(
            this, tr("Fichier modifié sur le disque"),
            tr("%1 a été modifié sur le disque, mais vous avez des modifications non enregistrées.\n\n"
               "Recharger le fichier ? Vos modifications seront perdues (Ctrl+Z permet de les retrouver).")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No);
        pathsBeingResolved_.remove(editor->filePath());
        if (answer != QMessageBox::Yes)
            return;
    }

    QString error;
    if (editor->reloadFromDisk(&error))
        emit statusMessage(tr("%1 rechargé depuis le disque").arg(name));
    else
        emit statusMessage(tr("Rechargement de %1 impossible : %2").arg(name, error));
}

void EditorTabs::updateTabTitle(EditorWidget *editor)
{
    const int index = indexOf(editor);
    if (index < 0)
        return;
    const QString name = QFileInfo(editor->filePath()).fileName();
    setTabText(index, editor->isModified() ? QStringLiteral("● ") + name : name);
}

void EditorTabs::applyTheme(const Theme &theme)
{
    theme_ = theme;
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(widget(i)))
            editor->applyTheme(theme);
        else if (auto *image = qobject_cast<ImageView *>(widget(i)))
            image->applyTheme(theme);
    }
}
