/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTCAMERAPANEL_H_
#define NANOEM_EMAPP_QT_QTCAMERAPANEL_H_

#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QPushButton;
class QSpinBox;
class QTimer;

namespace nanoem {

class ICamera;
class Project;

namespace internal {
class CameraFovVectorValueState;
} /* namespace internal */

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui camera panel (ImGuiWindow::drawCameraPanel).
 *
 *       Provides the reset button, the perspective toggle, the FOV editor (committed through
 *       CameraFovVectorValueState so slider edits become a single undo entry), the two level
 *       outside parent selectors (model and bone) and the register keyframe button. Shown only
 *       while no model is active, mirroring the ImGui bottom row multiplexing.
 */
class QtCameraPanel final : public QGroupBox {
public:
    QtCameraPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    Project *currentProject();
    void refresh();
    void refreshModelComboBox(Project *project, const ICamera *camera);
    void refreshBoneComboBox(Project *project, int modelIndex, const ICamera *camera);
    void applyOutsideParentModel(int index);
    void applyOutsideParentBone(int index);
    void handleFovChanged(int value);
    void commitFov();
    void validateFovState(Project *project);

    QtApplicationService *m_service;
    QTimer *m_timer;
    QPushButton *m_resetButton = nullptr;
    QCheckBox *m_perspectiveCheckBox = nullptr;
    QSpinBox *m_fovSpinBox = nullptr;
    QComboBox *m_modelComboBox = nullptr;
    QComboBox *m_boneComboBox = nullptr;
    QPushButton *m_registerButton = nullptr;
    internal::CameraFovVectorValueState *m_fovState = nullptr;
    const Project *m_fovStateProject = nullptr;
    const ICamera *m_fovStateCamera = nullptr;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTCAMERAPANEL_H_ */
