// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: Move Class to Its Own File.

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "refactor/EditPreview.h"
#include "refactor/MoveClass.h"
#include "refactor/RefactoringPreviewPopup.h"

#include <QFileInfo>

using namespace LspProtocol;

namespace {

// A file the move would create that exists already; empty if none.
QString existingNewFile(const MoveClass::Input &input, const MoveClass::Result &result)
{
    for (const QString &file : result.edit.filePaths()) {
        const bool isNew = file != input.path && file != input.sourcePath;
        if (isNew && QFileInfo::exists(file))
            return file;
    }
    return {};
}

} // namespace

MoveClass::Input CodeIntelligence::moveClassInput(EditorWidget *editor) const
{
    const bool isJava = editor->languageId() == QLatin1String("java");
    MoveClass::Input input{
        .path = editor->filePath(),
        .languageId = isJava ? QStringLiteral("java") : QStringLiteral("cpp"),
        .text = editor->text(),
        .cursor = lspPosition(editor, int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))),
    };
    if (isJava)
        return input;
    // The members' definitions, from the header's source.
    input.sourcePath = counterpartOf(input.path);
    if (input.sourcePath.isEmpty())
        return input;
    if (const EditorWidget *source = tabs_->editorFor(input.sourcePath)) {
        input.sourceText = source->text();
        return input;
    }
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(input.sourcePath, &error);
    input.sourceText = decoded ? decoded->text : QString();
    return input;
}

void CodeIntelligence::moveClassToOwnFile()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty() || editor->isReadOnly())
        return;
    const MoveClass::Input input = moveClassInput(editor);
    const MoveClass::Result result = MoveClass::plan(input);
    if (!result.error.isEmpty()) {
        emit statusMessage(result.error);
        return;
    }
    if (const QString taken = existingNewFile(input, result); !taken.isEmpty()) {
        emit statusMessage(tr("Cannot move “%1”: %2 already exists").arg(result.className, taken));
        return;
    }
    const QString title = tr("Move “%1” to %2").arg(result.className, QFileInfo(result.newPath).fileName());
    review({title, result.edit, {}, {}}, [this, newPath = result.newPath](const RefactoringOutcome &outcome) {
        if (outcome.status == RefactoringOutcome::Status::Applied)
            tabs_->openFile(newPath);
    });
}
