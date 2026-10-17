#include "ClaudeLinter.h"

#include "ClaudeLint.h"
#include "editor/EditorWidget.h"

#include <QPointer>
#include <QTimer>

namespace {

constexpr int checkDelayMs = 300;

} // namespace

void ClaudeLinter::attach(EditorWidget *editor)
{
    const ClaudeLint::Kind kind = ClaudeLint::kindOf(editor->filePath());
    if (kind == ClaudeLint::Kind::None)
        return;
    const auto check = [kind, guard = QPointer<EditorWidget>(editor)] {
        if (guard)
            guard->setDiagnostics(ClaudeLint::check(kind, guard->utf8Bytes()));
    };
    auto *timer = new QTimer(editor);
    timer->setSingleShot(true);
    timer->setInterval(checkDelayMs);
    connect(timer, &QTimer::timeout, editor, check);
    connect(editor, &QsciScintilla::textChanged, timer, qOverload<>(&QTimer::start));
    if (kind == ClaudeLint::Kind::Settings) {
        editor->setHoverHelp([guard = QPointer<EditorWidget>(editor)](int position) {
            return guard ? ClaudeLint::settingsHelp(guard->utf8Bytes(), position) : QString();
        });
    }
    check();
}
