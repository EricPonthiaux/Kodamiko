#include "DiagramView.h"

#include "terminal/TerminalWidget.h"

// Flowboard
#include "app.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "style.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QSurfaceFormat>
#include <QTimer>

#include <cfloat>

namespace {

// Makes a view's ImGui context current for the duration of a scope: several
// diagrams can be open at once, each with its own context.
class ContextScope {
public:
    explicit ContextScope(ImGuiContext *context)
        : previous_(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(context);
    }
    ~ContextScope() { ImGui::SetCurrentContext(previous_); }

private:
    ImGuiContext *previous_;
};

ImGuiKey toImGuiKey(const QKeyEvent &event)
{
    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return ImGuiKey(ImGuiKey_A + (key - Qt::Key_A));
    const bool keypad = event.modifiers() & Qt::KeypadModifier;
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return ImGuiKey((keypad ? ImGuiKey_Keypad0 : ImGuiKey_0) + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return ImGuiKey(ImGuiKey_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return ImGuiKey_Tab;
    case Qt::Key_Left: return ImGuiKey_LeftArrow;
    case Qt::Key_Right: return ImGuiKey_RightArrow;
    case Qt::Key_Up: return ImGuiKey_UpArrow;
    case Qt::Key_Down: return ImGuiKey_DownArrow;
    case Qt::Key_PageUp: return ImGuiKey_PageUp;
    case Qt::Key_PageDown: return ImGuiKey_PageDown;
    case Qt::Key_Home: return ImGuiKey_Home;
    case Qt::Key_End: return ImGuiKey_End;
    case Qt::Key_Insert: return ImGuiKey_Insert;
    case Qt::Key_Delete: return ImGuiKey_Delete;
    case Qt::Key_Backspace: return ImGuiKey_Backspace;
    case Qt::Key_Space: return ImGuiKey_Space;
    case Qt::Key_Return: return ImGuiKey_Enter;
    case Qt::Key_Enter: return keypad ? ImGuiKey_KeypadEnter : ImGuiKey_Enter;
    case Qt::Key_Escape: return ImGuiKey_Escape;
    case Qt::Key_Minus: return keypad ? ImGuiKey_KeypadSubtract : ImGuiKey_Minus;
    case Qt::Key_Plus: return keypad ? ImGuiKey_KeypadAdd : ImGuiKey_Equal;
    case Qt::Key_Equal: return ImGuiKey_Equal;
    case Qt::Key_Comma: return ImGuiKey_Comma;
    case Qt::Key_Period: return keypad ? ImGuiKey_KeypadDecimal : ImGuiKey_Period;
    case Qt::Key_Slash: return keypad ? ImGuiKey_KeypadDivide : ImGuiKey_Slash;
    case Qt::Key_Asterisk: return ImGuiKey_KeypadMultiply;
    case Qt::Key_Semicolon: return ImGuiKey_Semicolon;
    case Qt::Key_Apostrophe: return ImGuiKey_Apostrophe;
    case Qt::Key_Backslash: return ImGuiKey_Backslash;
    case Qt::Key_BracketLeft: return ImGuiKey_LeftBracket;
    case Qt::Key_BracketRight: return ImGuiKey_RightBracket;
    case Qt::Key_QuoteLeft: return ImGuiKey_GraveAccent;
    default: return ImGuiKey_None;
    }
}

int toImGuiButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return ImGuiMouseButton_Left;
    case Qt::RightButton: return ImGuiMouseButton_Right;
    case Qt::MiddleButton: return ImGuiMouseButton_Middle;
    default: return -1;
    }
}

Qt::CursorShape toQtCursor(ImGuiMouseCursor cursor)
{
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: return Qt::IBeamCursor;
    case ImGuiMouseCursor_ResizeAll: return Qt::SizeAllCursor;
    case ImGuiMouseCursor_ResizeNS: return Qt::SizeVerCursor;
    case ImGuiMouseCursor_ResizeEW: return Qt::SizeHorCursor;
    case ImGuiMouseCursor_ResizeNESW: return Qt::SizeBDiagCursor;
    case ImGuiMouseCursor_ResizeNWSE: return Qt::SizeFDiagCursor;
    case ImGuiMouseCursor_Hand: return Qt::PointingHandCursor;
    case ImGuiMouseCursor_Wait: return Qt::WaitCursor;
    case ImGuiMouseCursor_Progress: return Qt::BusyCursor;
    case ImGuiMouseCursor_NotAllowed: return Qt::ForbiddenCursor;
    default: return Qt::ArrowCursor;
    }
}

// Keys the diagram handles itself, taken before the window's shortcuts.
bool isDiagramShortcut(const QKeyEvent &event)
{
    const Qt::KeyboardModifiers mods = event.modifiers() & ~Qt::KeypadModifier;
    const int key = event.key();
    if (mods == Qt::NoModifier || mods == Qt::ShiftModifier || mods == Qt::AltModifier)
        return key != Qt::Key_F11 && key != Qt::Key_F5;
    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_Z: case Qt::Key_Y: case Qt::Key_C: case Qt::Key_X: case Qt::Key_V:
        case Qt::Key_D: case Qt::Key_A: case Qt::Key_L: case Qt::Key_0: case Qt::Key_Equal:
        case Qt::Key_Plus: case Qt::Key_Minus: case Qt::Key_Return: case Qt::Key_Enter:
            return true;
        default: return false;
        }
    }
    return mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Z;
}

} // namespace

struct DiagramView::Impl {
    ImGuiContext *context = nullptr;
    App app;
    bool rendererReady = false;
    int settleFrames = 0;
    bool lastModified = false;
    QElapsedTimer clock;
    QByteArray clipboard; // keeps the text returned to ImGui alive
};

DiagramView::DiagramView(const Theme &theme, QWidget *parent)
    : QOpenGLWidget(parent)
    , d_(std::make_unique<Impl>())
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4); // on top of ImGui's anti-aliasing, as in Flowboard
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_InputMethodEnabled);

    d_->context = ImGui::CreateContext();
    ContextScope scope(d_->context);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "enlumine-qt";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    // The renderer is created with the GL context (initializeGL), but a file
    // can be loaded before: textures wait for it.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(1, 1);

    ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
    platform.Platform_ClipboardUserData = d_.get();
    platform.Platform_SetClipboardTextFn = [](ImGuiContext *, const char *text) {
        QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    };
    platform.Platform_GetClipboardTextFn = [](ImGuiContext *) -> const char * {
        // Called with this view's context current.
        auto *impl = static_cast<Impl *>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        impl->clipboard = QGuiApplication::clipboard()->text().toUtf8();
        return impl->clipboard.constData();
    };

    flowboardApplyStyle();
    flowboardSetupFont();
    d_->app.setEmbedded(true);
    // The native file chooser answers from another thread.
    d_->app.setWake([view = QPointer<DiagramView>(this)] {
        QMetaObject::invokeMethod(qApp, [view] {
            if (view)
                view->wake();
        });
    });
    d_->clock.start();
    applyTheme(theme);
}

DiagramView::~DiagramView()
{
    if (d_->rendererReady) {
        makeCurrent();
        ContextScope scope(d_->context);
        ImGui_ImplOpenGL3_Shutdown();
        doneCurrent();
    }
    ImGui::DestroyContext(d_->context);
}

bool DiagramView::isDiagramFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("flow"), Qt::CaseInsensitive) == 0;
}

bool DiagramView::load(const QString &path, QString *error)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    bool ok = false;
    // Loading measures text: it needs a frame (the font is prepared there).
    runFrame(false, [&] { ok = d_->app.load(absolutePath.toStdString()); });
    if (!ok) {
        if (error)
            *error = QString::fromStdString(d_->app.message());
        return false;
    }
    filePath_ = absolutePath;
    notifyModification();
    update();
    return true;
}

bool DiagramView::save(QString *error)
{
    bool ok = false;
    {
        ContextScope scope(d_->context);
        ok = d_->app.save();
    }
    if (!ok && error)
        *error = QString::fromStdString(d_->app.message());
    notifyModification();
    wake();
    return ok;
}

bool DiagramView::reloadFromDisk(QString *error)
{
    return load(filePath_, error);
}

bool DiagramView::isModified() const
{
    return d_->app.modified();
}

bool DiagramView::matchesDisk() const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray content = file.readAll();
    const std::string &known = d_->app.diskContent();
    return content.size() == qsizetype(known.size()) && std::equal(content.begin(), content.end(), known.begin());
}

void DiagramView::applyTheme(const Theme &)
{
    // The diagram keeps Flowboard's light palette: its colours are part of the
    // document (exported to SVG as shown).
}

void DiagramView::runFrame(bool render, const std::function<void()> &beforeFrame)
{
    ContextScope scope(d_->context);
    ImGuiIO &io = ImGui::GetIO();
    if (width() > 0 && height() > 0)
        io.DisplaySize = ImVec2(float(width()), float(height()));
    const float ratio = float(devicePixelRatioF());
    io.DisplayFramebufferScale = ImVec2(ratio, ratio);
    io.DeltaTime = std::max(float(d_->clock.restart()) / 1000.f, 1.f / 1000.f);

    if (render)
        ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    if (beforeFrame)
        beforeFrame();
    d_->app.frame();
    if (!render) {
        ImGui::EndFrame();
        return;
    }
    ImGui::Render();

    QOpenGLFunctions *gl = context()->functions();
    gl->glClearColor(0.98f, 0.98f, 0.98f, 1.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (cursor == ImGuiMouseCursor_None)
        setCursor(Qt::BlankCursor);
    else
        setCursor(toQtCursor(cursor));
}

void DiagramView::initializeGL()
{
    ContextScope scope(d_->context);
    d_->rendererReady = ImGui_ImplOpenGL3_Init("#version 330");
}

void DiagramView::paintGL()
{
    if (!d_->rendererReady)
        return;
    runFrame(true);
    notifyModification();

    // Keep producing frames while something moves (animation, drag, text
    // cursor) and for a few frames after each event; otherwise stay idle.
    if (d_->settleFrames > 0)
        --d_->settleFrames;
    bool animating;
    {
        ContextScope scope(d_->context);
        animating = d_->app.animating();
    }
    if (animating || d_->settleFrames > 0)
        QMetaObject::invokeMethod(this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
}

void DiagramView::wake()
{
    d_->settleFrames = 3;
    update();
}

void DiagramView::notifyModification()
{
    const bool modified = isModified();
    if (modified != d_->lastModified) {
        d_->lastModified = modified;
        emit modificationChanged(modified);
    }
}

bool DiagramView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        bool typing;
        {
            ContextScope scope(d_->context);
            typing = ImGui::GetIO().WantTextInput;
        }
        // While a label is being edited, every key goes to it, except the
        // window's mode switching, palette and save.
        const bool palette = key->key() == Qt::Key_P && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);
        const bool saveKey = key->matches(QKeySequence::Save);
        const bool windowKey = key->key() == Qt::Key_F11 || TerminalWidget::isModeShortcut(*key) || palette || saveKey;
        if ((typing && !windowKey) || isDiagramShortcut(*key)) {
            event->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(event);
}

void DiagramView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    const int button = toImGuiButton(event->button());
    if (button >= 0) {
        ContextScope scope(d_->context);
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()), float(event->position().y()));
        io.AddMouseButtonEvent(button, true);
    }
    wake();
}

void DiagramView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // ImGui detects double clicks itself: this is just a second press.
    mousePressEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent *event)
{
    const int button = toImGuiButton(event->button());
    if (button >= 0) {
        ContextScope scope(d_->context);
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()), float(event->position().y()));
        io.AddMouseButtonEvent(button, false);
    }
    wake();
}

void DiagramView::mouseMoveEvent(QMouseEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGui::GetIO().AddMousePosEvent(float(event->position().x()), float(event->position().y()));
    }
    wake();
}

void DiagramView::wheelEvent(QWheelEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()), float(event->position().y()));
        const QPoint angle = event->angleDelta();
        io.AddMouseWheelEvent(float(angle.x()) / 120.f, float(angle.y()) / 120.f);
    }
    event->accept();
    wake();
}

void DiagramView::keyPressEvent(QKeyEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGuiIO &io = ImGui::GetIO();
        const Qt::KeyboardModifiers mods = event->modifiers();
        io.AddKeyEvent(ImGuiMod_Ctrl, mods & Qt::ControlModifier);
        io.AddKeyEvent(ImGuiMod_Shift, mods & Qt::ShiftModifier);
        io.AddKeyEvent(ImGuiMod_Alt, mods & Qt::AltModifier);
        io.AddKeyEvent(ImGuiMod_Super, mods & Qt::MetaModifier);
        const ImGuiKey key = toImGuiKey(*event);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, true);
        // Typed text (not shortcuts): ImGui keeps only printable characters.
        const QString text = event->text();
        if (!text.isEmpty() && !(mods & Qt::ControlModifier) && text.at(0).isPrint())
            io.AddInputCharactersUTF8(text.toUtf8().constData());
    }
    event->accept();
    wake();
}

void DiagramView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
        return;
    {
        ContextScope scope(d_->context);
        ImGuiIO &io = ImGui::GetIO();
        const Qt::KeyboardModifiers mods = event->modifiers();
        io.AddKeyEvent(ImGuiMod_Ctrl, mods & Qt::ControlModifier);
        io.AddKeyEvent(ImGuiMod_Shift, mods & Qt::ShiftModifier);
        io.AddKeyEvent(ImGuiMod_Alt, mods & Qt::AltModifier);
        io.AddKeyEvent(ImGuiMod_Super, mods & Qt::MetaModifier);
        const ImGuiKey key = toImGuiKey(*event);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, false);
    }
    event->accept();
    wake();
}

void DiagramView::inputMethodEvent(QInputMethodEvent *event)
{
    // Composed characters (dead keys, input methods).
    if (!event->commitString().isEmpty()) {
        ContextScope scope(d_->context);
        ImGui::GetIO().AddInputCharactersUTF8(event->commitString().toUtf8().constData());
    }
    event->accept();
    wake();
}

QVariant DiagramView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
        return true;
    return QOpenGLWidget::inputMethodQuery(query);
}

void DiagramView::focusInEvent(QFocusEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGui::GetIO().AddFocusEvent(true);
    }
    QOpenGLWidget::focusInEvent(event);
    wake();
}

void DiagramView::focusOutEvent(QFocusEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGui::GetIO().AddFocusEvent(false);
    }
    QOpenGLWidget::focusOutEvent(event);
    wake();
}

void DiagramView::leaveEvent(QEvent *event)
{
    {
        ContextScope scope(d_->context);
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    QOpenGLWidget::leaveEvent(event);
    wake();
}
