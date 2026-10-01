/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtApplicationService.h"

#include "emapp/EnumUtils.h"
#include "emapp/IProjectHolder.h"
#include "emapp/Project.h"
#include "emapp/StringUtils.h"
#include "emapp/private/CommonInclude.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include "imgui/imgui.h"

namespace nanoem {
namespace qt {
namespace {

static const nanoem_u32_t kKeyTypeUnknown = static_cast<nanoem_u32_t>(BaseApplicationService::kKeyType_UNKNOWN);

static nanoem_u32_t
convertSpecialKeyType(Qt::Key key)
{
    typedef BaseApplicationService Base;
    switch (key) {
    case Qt::Key_Escape:
        return Base::kKeyType_ESCAPE;
    case Qt::Key_Return:
        return Base::kKeyType_ENTER;
    case Qt::Key_Enter:
        return Base::kKeyType_KP_ENTER;
    case Qt::Key_Tab:
        return Base::kKeyType_TAB;
    case Qt::Key_Backtab:
        return Base::kKeyType_TAB;
    case Qt::Key_Backspace:
        return Base::kKeyType_BACKSPACE;
    case Qt::Key_Insert:
        return Base::kKeyType_INSERT;
    case Qt::Key_Delete:
        return Base::kKeyType_DELETE;
    case Qt::Key_Right:
        return Base::kKeyType_RIGHT;
    case Qt::Key_Left:
        return Base::kKeyType_LEFT;
    case Qt::Key_Down:
        return Base::kKeyType_DOWN;
    case Qt::Key_Up:
        return Base::kKeyType_UP;
    case Qt::Key_PageUp:
        return Base::kKeyType_PAGE_UP;
    case Qt::Key_PageDown:
        return Base::kKeyType_PAGE_DOWN;
    case Qt::Key_Home:
        return Base::kKeyType_HOME;
    case Qt::Key_End:
        return Base::kKeyType_END;
    case Qt::Key_CapsLock:
        return Base::kKeyType_CAPS_LOCK;
    case Qt::Key_ScrollLock:
        return Base::kKeyType_SCROLL_LOCK;
    case Qt::Key_NumLock:
        return Base::kKeyType_NUM_LOCK;
    case Qt::Key_Print:
        return Base::kKeyType_PRINT_SCREEN;
    case Qt::Key_Pause:
        return Base::kKeyType_PAUSE;
    case Qt::Key_Shift:
        return Base::kKeyType_LEFT_SHIFT;
    case Qt::Key_Control:
        return Base::kKeyType_LEFT_CONTROL;
    case Qt::Key_Alt:
        return Base::kKeyType_LEFT_ALT;
    case Qt::Key_Meta:
        return Base::kKeyType_LEFT_SUPER;
    case Qt::Key_Menu:
        return Base::kKeyType_MENU;
    case Qt::Key_F1:
        return Base::kKeyType_F1;
    case Qt::Key_F2:
        return Base::kKeyType_F2;
    case Qt::Key_F3:
        return Base::kKeyType_F3;
    case Qt::Key_F4:
        return Base::kKeyType_F4;
    case Qt::Key_F5:
        return Base::kKeyType_F5;
    case Qt::Key_F6:
        return Base::kKeyType_F6;
    case Qt::Key_F7:
        return Base::kKeyType_F7;
    case Qt::Key_F8:
        return Base::kKeyType_F8;
    case Qt::Key_F9:
        return Base::kKeyType_F9;
    case Qt::Key_F10:
        return Base::kKeyType_F10;
    case Qt::Key_F11:
        return Base::kKeyType_F11;
    case Qt::Key_F12:
        return Base::kKeyType_F12;
    default:
        break;
    }
    return kKeyTypeUnknown;
}

} /* namespace anonymous */

int
QtApplicationService::convertCursorModifier(Qt::KeyboardModifiers value)
{
    int cursorModifiers = 0;
    if (value.testFlag(Qt::AltModifier)) {
        cursorModifiers |= Project::kCursorModifierTypeAlt;
    }
    if (value.testFlag(Qt::ShiftModifier)) {
        cursorModifiers |= Project::kCursorModifierTypeShift;
    }
    if (value.testFlag(Qt::ControlModifier) || value.testFlag(Qt::MetaModifier)) {
        cursorModifiers |= Project::kCursorModifierTypeControl;
    }
    return cursorModifiers;
}

int
QtApplicationService::convertCursorType(Qt::MouseButton value)
{
    int cursorType = 0;
    switch (value) {
    case Qt::LeftButton:
        cursorType = Project::kCursorTypeMouseLeft;
        break;
    case Qt::MiddleButton:
        cursorType = Project::kCursorTypeMouseMiddle;
        break;
    case Qt::RightButton:
        cursorType = Project::kCursorTypeMouseRight;
        break;
    case Qt::BackButton:
        cursorType = Project::kCursorTypeMouseUndo;
        break;
    case Qt::ForwardButton:
        cursorType = Project::kCursorTypeMouseRedo;
        break;
    default:
        break;
    }
    return cursorType;
}

nanoem_u32_t
QtApplicationService::convertKeyType(Qt::Key value)
{
    const int key = static_cast<int>(value);
    if (key > 0 && key <= 0xff) {
        /* printable ASCII and kKeyType_WORLD_1/kKeyType_WORLD_2 share the same values as Qt::Key */
        return static_cast<nanoem_u32_t>(key);
    }
    return convertSpecialKeyType(value);
}

QtApplicationService::QtApplicationService(const JSON_Value *config, QtApplicationClient::Bridge *bridge)
    : BaseApplicationService(config)
    , m_menubarApplicationClient(bridge)
    , m_bridge(bridge)
{
}

QtApplicationService::~QtApplicationService()
{
}

void
QtApplicationService::draw()
{
    drawDefaultPass();
}

void
QtApplicationService::consumeDispatchAllCommandMessages()
{
    Project *project = projectHolder()->currentProject();
    ByteArrayList &commands = m_bridge->m_commands;
    ByteArrayList::iterator begin = commands.begin(), end = commands.end();
    for (ByteArrayList::const_iterator it = begin; it != end; ++it) {
        dispatchCommandMessage(it->data(), it->size(), project, false);
    }
    commands.erase(begin, end);
}

void
QtApplicationService::dispatchAllEventMessages()
{
    m_menubarApplicationClient.dispatchAllEventMessages();
}

void
QtApplicationService::setFrameBufferResolver(std::function<nanoem_u32_t()> value)
{
    m_frameBufferResolver = std::move(value);
}

BaseApplicationClient *
QtApplicationService::menubarApplicationClient()
{
    return &m_menubarApplicationClient;
}

void
QtApplicationService::sendEventMessage(const Nanoem__Application__Event *event)
{
    ByteArray bytes(sizeofEventMessage(event));
    packEventMessage(event, bytes.data());
    m_bridge->m_events.push_back(bytes);
}

void
QtApplicationService::handleInitializeApplication()
{
    BaseApplicationService::handleInitializeApplication();
    ImGuiIO &io = ImGui::GetIO();
    io.ClipboardUserData = nullptr;
    io.GetClipboardTextFn = [](void * /* userData */) -> const char * {
        static QByteArray buffer;
        buffer = QGuiApplication::clipboard()->text(QClipboard::Clipboard).toUtf8();
        return buffer.constData();
    };
    io.SetClipboardTextFn = [](void * /* userData */, const char *value) {
        QGuiApplication::clipboard()->setText(QString::fromUtf8(value), QClipboard::Clipboard);
    };
}

void
QtApplicationService::beginDefaultPass(
    nanoem_u32_t windowID, const sg_pass_action &pa, int width, int height, int &sampleCount)
{
    BaseApplicationService::beginDefaultPass(windowID, pa, width, height, sampleCount);
    rebindFrameBuffer();
}

void
QtApplicationService::endDefaultPass()
{
    BaseApplicationService::endDefaultPass();
    rebindFrameBuffer();
}

void
QtApplicationService::rebindFrameBuffer()
{
    /* QOpenGLWidget renders into its own framebuffer object which sokol captured at sg_setup time.
       The widget recreates its framebuffer on resize, so rebind the current one on every default
       pass boundary to keep rendering into the visible widget. */
    if (m_frameBufferResolver) {
        if (QOpenGLContext *context = QOpenGLContext::currentContext()) {
            context->functions()->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_frameBufferResolver()));
        }
    }
}

} /* namespace qt */
} /* namespace nanoem */
