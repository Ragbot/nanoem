/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTLIGHTPANEL_H_
#define NANOEM_EMAPP_QT_QTLIGHTPANEL_H_

#include <QGroupBox>

class QPushButton;
class QDoubleSpinBox;
class QTimer;
class QLabel;

namespace nanoem {

class Project;

namespace internal {
class LightDirectionVectorValueState;
} /* namespace internal */

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui light panel (ImGuiWindow::drawLightPanel).
 *
 *       Provides the reset button, the light color swatch (edited through the native QColorDialog
 *       and committed as one undo entry through LightColorVectorValueState), the direction editor
 *       (LightDirectionVectorValueState), the self shadow configuration opener (still rendered by
 *       ImGui) and the register keyframe button. Shown only while no model is active.
 */
class QtLightPanel final : public QGroupBox {
public:
    QtLightPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    Project *currentProject();
    void refresh();
    void pickColor();
    void handleDirectionChanged(double value);
    void commitDirection();
    void validateStates(Project *project);

    QtApplicationService *m_service;
    QTimer *m_timer;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_colorButton = nullptr;
    QLabel *m_directionLabel = nullptr;
    QDoubleSpinBox *m_directionSpinBoxes[3] = { nullptr, nullptr, nullptr };
    QPushButton *m_selfShadowButton = nullptr;
    QPushButton *m_registerButton = nullptr;
    internal::LightDirectionVectorValueState *m_directionState = nullptr;
    const Project *m_stateProject = nullptr;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTLIGHTPANEL_H_ */
