/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPORT3D_H_
#define NANOEM_EMAPP_QT_QTVIEWPORT3D_H_

#include "emapp/Forward.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>

namespace nanoem {
namespace qt {

class QtApplicationService;
class QtApplicationClient;

/**
 * @brief Native Qt 3D viewport widget.
 *
 *        Owns the OpenGL context the emapp engine renders into: every frame it binds the widget
 *        framebuffer and drives BaseApplicationService::drawDefaultPass(). The viewport image
 *        itself is composited by Project::presentViewportImage() (see kNativePanelTypeViewport),
 *        so no ImGui draw list is involved in putting the rendered scene on screen.
 */
class QtViewport3D final : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    QtViewport3D(QtApplicationService *service, QtApplicationClient *client, QWidget *parent = nullptr);
    ~QtViewport3D() override;

    /**
     * @brief Makes the widget's GL context current, recreating it if Qt dropped it.
     *
     *        QOpenGLWidget discards its GL context when the widget is (re)shown, while command
     *        dispatch (sg_setup, model/effect loading) uploads GPU resources. Calling this before
     *        dispatching guarantees a current context.
     *
     * @return true when a GL context is current afterwards.
     */
    bool makeContextCurrent();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QtApplicationService *m_service = nullptr;
    QtApplicationClient *m_client = nullptr;
    bool m_sokolInitialized = false;
    Vector2SI32 m_lastCursorPosition = Vector2SI32(0);
    int m_pressedCursorType = 0;
    int m_pressedCursorModifiers = 0;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPORT3D_H_ */
