/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPORT3D_H_
#define NANOEM_EMAPP_QT_QTVIEWPORT3D_H_

#include "emapp/Forward.h"
#include "emapp/IState.h"
#include "emapp/Project.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>

namespace nanoem {
namespace qt {

class QtApplicationService;
class QtApplicationClient;
class IState;

/**
 * @brief Native Qt 3D viewport widget - replaces ImGui-based viewport rendering.
 *
 *        Renders the 3D scene directly using sokol on the widget's framebuffer,
 *        and draws gizmos/primitives using native Qt OpenGL.
 *        Zero ImGui dependency.
 */
class QtViewport3D final : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    QtViewport3D(QtApplicationService *service, QtApplicationClient *client, QWidget *parent = nullptr);
    ~QtViewport3D() override;

    void setProject(Project *project);
    void setState(IState *state);
    void setFlags(nanoem_u32_t flags);

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
    void setupSokol();
    void renderScene();
    void drawGizmos();
    void drawPrimitives();
    void drawViewportImage();

    QtApplicationService *m_service = nullptr;
    QtApplicationClient *m_client = nullptr;
    Project *m_project = nullptr;
    IState *m_state = nullptr;
    nanoem_u32_t m_flags = 0;
    bool m_sokolInitialized = false;
    Vector2SI32 m_lastCursorPosition = Vector2SI32(0);
    int m_pressedCursorType = 0;
    int m_pressedCursorModifiers = 0;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPORT3D_H_ */