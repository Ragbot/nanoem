/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewport3D.h"
#include "QtApplicationService.h"
#include "QtApplicationClient.h"

#include "emapp/Forward.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>

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
    /* the widget always paints itself (the engine clears the whole framebuffer every frame), so Qt
       never has to fall back to the translucent system background behind the GL surface */
    setAutoFillBackground(false);

    m_service->setFrameBufferResolver([this]() -> nanoem_u32_t {
        return static_cast<nanoem_u32_t>(defaultFramebufferObject());
    });
}

QtViewport3D::~QtViewport3D()
{
    m_service->setFrameBufferResolver(nullptr);
}

bool
QtViewport3D::makeContextCurrent()
{
    if (!context()) {
        /* QOpenGLWidget discards its GL context when the widget is (re)shown; grabFramebuffer()
           is the supported way to force Qt to recreate the context (and the FBO) on demand. */
        grabFramebuffer();
    }
    if (context()) {
        makeCurrent();
    }
    return QOpenGLContext::currentContext() != nullptr;
}

void
QtViewport3D::initializeGL()
{
    initializeOpenGLFunctions();
    /* sokol itself (sg_setup) was already performed by BaseApplicationService during startup; the
       widget only owns the context and the framebuffer everything is drawn into */
    m_sokolInitialized = true;
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
    if (!m_sokolInitialized) {
        return;
    }
    makeCurrent();
    /* QOpenGLWidget renders into its own FBO; make sure sokol (and our own glClear below) target it */
    if (m_service) {
        m_service->rebindFrameBuffer();
    }
    /* Fallback background: drawDefaultPass() early-outs when no project is loaded, and the widget
       must never stay transparent (that is what made the window behind show through) */
    glClearColor(0.118f, 0.118f, 0.118f, 1.0f); /* #1E1E1E: matches the Visual Studio editor background */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    /* drives the whole emapp frame: offscreen/shadow/viewport passes, then ImGuiWindow (whose
       viewport image entry is replaced by Project::presentViewportImage for this shell) */
    if (m_service) {
        m_service->draw();
    }
    /* NOTE: do not call doneCurrent() here. QOpenGLWidget::paintEvent() dereferences
       QOpenGLContext::currentContext() once paintGL() returns (to blit the FBO onto the widget),
       and it relies on the context still being current. */
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
