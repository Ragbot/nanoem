/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPORTOVERLAY_H_
#define NANOEM_EMAPP_QT_QTVIEWPORTOVERLAY_H_

#include "emapp/Forward.h"

#include <QWidget>

class QPaintEvent;
class QTimer;

namespace nanoem {
namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the viewport HUD ImGui draws over the GL viewport
 *        (ImGuiWindow::drawFPSCounter and drawPerformanceMonitor).
 *
 *       Paints the FPS badge at the bottom-left of the viewport layout and the CPU/MEM box at
 *       its top-left, positioned from Project::logicalScaleUniformedViewportLayoutRect so they
 *       land exactly where the ImGui versions did; both honor the same project toggles and the
 *       ImGui side is masked out via kNativePanelTypeViewportHUD. The widget sits above the GL
 *       widget and is transparent for the mouse so the ImGui gizmo and viewport input keep
 *       working. The CPU/MEM values are sampled by this widget itself (the other platform
 *       shells feed them through sendUpdatePerformanceMonitorMessage).
 */
class QtViewportOverlay final : public QWidget {
public:
    QtViewportOverlay(QtApplicationService *service, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refresh();
    void samplePerformanceCounters();

    QtApplicationService *m_service;
    QTimer *m_timer;
    nanoem_f32_t m_cpuPercentage = 0;
    nanoem_u64_t m_memoryBytes = 0;
    double m_lastCPUTimeSeconds = 0;
    double m_lastWallSeconds = 0;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPORTOVERLAY_H_ */
