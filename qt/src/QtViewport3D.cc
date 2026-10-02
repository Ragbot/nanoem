/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewport3D.h"
#include "QtApplicationService.h"
#include "QtApplicationClient.h"

#include "emapp/IPrimitive2D.h"
#include "emapp/IState.h"
#include "emapp/Project.h"
#include "emapp/Forward.h"
#include "emapp/BaseApplicationService.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

#include "sokol/sokol_gfx.h"

namespace nanoem {
namespace qt {

QtViewport3D::QtViewport3D(QtApplicationService *service, QtApplicationClient *client, QWidget *parent)
    : QOpenGLWidget(parent)
    , m_service(service)
    , m_client(client)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    m_service->setFrameBufferResolver([this]() -> nanoem_u32_t {
        return static_cast<nanoem_u32_t>(defaultFramebufferObject());
    });
}

QtViewport3D::~QtViewport3D()
{
    m_service->setFrameBufferResolver(nullptr);
}

void
QtViewport3D::setProject(Project *project)
{
    m_project = project;
}

void
QtViewport3D::setState(IState *state)
{
    m_state = state;
}

void
QtViewport3D::setFlags(nanoem_u32_t flags)
{
    m_flags = flags;
}

void
QtViewport3D::initializeGL()
{
    initializeOpenGLFunctions();
    setupSokol();
    m_sokolInitialized = true;
}

void
QtViewport3D::setupSokol()
{
    /* sokol is initialized by BaseApplicationService during startup.
       We just need to ensure the OpenGL context is current for sokol calls. */
    makeCurrent();
    
    /* The sokol context is already created by BaseApplicationService.
       We just need to rebind the framebuffer on each frame. */
    doneCurrent();
}

void
QtViewport3D::resizeGL(int w, int h)
{
    if (m_client) {
        m_client->sendResizeWindowMessage(Vector2UI32(static_cast<nanoem_u32_t>(w), static_cast<nanoem_u32_t>(h)));
    }
}

void
QtViewport3D::paintGL()
{
    if (!m_sokolInitialized || !m_project) {
        return;
    }

    makeCurrent();
    
    /* Rebind the widget's framebuffer for sokol rendering */
    if (m_service) {
        m_service->rebindFrameBuffer();
    }

    /* Clear the widget's framebuffer */
    glClearColor(0.118f, 0.118f, 0.118f, 1.0f); /* #1E1E1E */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    /* Render the 3D scene using sokol (emapp engine) */
    renderScene();

    /* Draw gizmos and primitives using native Qt OpenGL */
    drawGizmos();
    drawPrimitives();

    /* Display the rendered viewport image */
    drawViewportImage();

    doneCurrent();
}

void
QtViewport3D::renderScene()
{
    if (!m_project || !m_state) {
        return;
    }

    /* The emapp engine renders the 3D scene via sokol in drawDefaultPass().
       We need to trigger that rendering. The BaseApplicationService::drawDefaultPass()
       will call draw() on the IUIWindow, which for Qt is our QtUIWindow decorator.
       But we want to bypass ImGui entirely and render directly.
       
       For now, we'll let the emapp engine render via its normal pipeline.
       The key is that the framebuffer is already bound to our widget's framebuffer
       via m_service->rebindFrameBuffer(). */
    
    /* Trigger the emapp rendering pipeline */
    if (m_service) {
        m_service->draw();
        m_service->consumeDispatchAllCommandMessages();
    }
}

void
QtViewport3D::drawGizmos()
{
    if (!m_project || !m_state) {
        return;
    }

    /* TODO: Implement native Qt gizmo rendering.
       The gizmo controller currently uses ImDrawList via ImGui.
       Need to reimplement using native Qt OpenGL or sokol directly. */
}

void
QtViewport3D::drawPrimitives()
{
    if (!m_project || !m_state) {
        return;
    }

    /* TODO: Implement native Qt primitive 2D rendering.
       Currently uses ImDrawList via ImGuiWindow::m_primitive2D. */
}

void
QtViewport3D::drawViewportImage()
{
    /* The viewport image is already rendered to the framebuffer by sokol.
       Nothing additional needed here - the framebuffer is already our widget's. */
}

void
QtViewport3D::mousePressEvent(QMouseEvent *event)
{
    const Vector2SI32 position(static_cast<int>(event->position().x()), static_cast<int>(event->position().y())),
        screenPosition(static_cast<int>(event->globalPosition().x()), static_cast<int>(event->globalPosition().y()));
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    const int cursorType = QtApplicationService::convertCursorType(event->button());
    if (cursorType != 0) {
        m_client->sendScreenCursorPressMessage(screenPosition, cursorType, modifiers);
        m_client->sendCursorPressMessage(position, cursorType, modifiers);
        m_pressedCursorType = cursorType;
        m_pressedCursorModifiers = modifiers;
    }
    m_lastCursorPosition = position;
    event->accept();
}

void
QtViewport3D::mouseReleaseEvent(QMouseEvent *event)
{
    const Vector2SI32 position(static_cast<int>(event->position().x()), static_cast<int>(event->position().y())),
        screenPosition(static_cast<int>(event->globalPosition().x()), static_cast<int>(event->globalPosition().y()));
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    const int cursorType = QtApplicationService::convertCursorType(event->button());
    if (cursorType != 0) {
        m_client->sendScreenCursorReleaseMessage(screenPosition, cursorType, modifiers);
        m_client->sendCursorReleaseMessage(position, cursorType, modifiers);
        m_pressedCursorType = 0;
        m_pressedCursorModifiers = 0;
    }
    m_lastCursorPosition = position;
    event->accept();
}

void
QtViewport3D::mouseMoveEvent(QMouseEvent *event)
{
    const Vector2SI32 position(static_cast<int>(event->position().x()), static_cast<int>(event->position().y())),
        delta(position - m_lastCursorPosition),
        screenPosition(static_cast<int>(event->globalPosition().x()), static_cast<int>(event->globalPosition().y()));
    m_client->sendScreenCursorMoveMessage(screenPosition, m_pressedCursorType, m_pressedCursorModifiers);
    m_client->sendCursorMoveMessage(position, delta, m_pressedCursorType, m_pressedCursorModifiers);
    m_lastCursorPosition = position;
    event->accept();
}

void
QtViewport3D::wheelEvent(QWheelEvent *event)
{
    QPoint delta(event->angleDelta());
    if (delta.isNull()) {
        delta = event->pixelDelta();
    }
    const Vector2SI32 position(static_cast<int>(event->position().x()), static_cast<int>(event->position().y())),
        scroll(delta.x() / 120, delta.y() / 120);
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    m_client->sendCursorScrollMessage(position, scroll, modifiers);
    event->accept();
}

void
QtViewport3D::keyPressEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        const nanoem_u32_t key = QtApplicationService::convertKeyType(static_cast<Qt::Key>(event->key()));
        if (key != static_cast<nanoem_u32_t>(QtApplicationService::kKeyType_UNKNOWN)) {
            m_client->sendKeyPressMessage(key);
        }
    }
    /* Handle unicode input */
    const QString text(event->text());
    const Qt::KeyboardModifiers modifiers(event->modifiers());
    if (!text.isEmpty() && !modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::MetaModifier)) {
        for (int i = 0, size = text.size(); i < size; i++) {
            const char16_t value = text.at(i).unicode();
            nanoem_u32_t codePoint = value;
            if (value >= 0xd800 && value < 0xdc00 && i + 1 < size) {
                const char16_t low = text.at(i + 1).unicode();
                if (low >= 0xdc00 && low < 0xe000) {
                    codePoint = 0x10000u + ((value - 0xd800u) << 10) + (low - 0xdc00u);
                    i++;
                }
            }
            if (codePoint >= 0x20u) {
                m_client->sendUnicodeInputMessage(codePoint);
            }
        }
    }
    event->accept();
}

void
QtViewport3D::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        const nanoem_u32_t key = QtApplicationService::convertKeyType(static_cast<Qt::Key>(event->key()));
        if (key != static_cast<nanoem_u32_t>(QtApplicationService::kKeyType_UNKNOWN)) {
            m_client->sendKeyReleaseMessage(key);
        }
    }
    event->accept();
}

} /* namespace qt */
} /* namespace nanoem */