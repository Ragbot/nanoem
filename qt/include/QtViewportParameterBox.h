/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPORTPARAMETERBOX_H_
#define NANOEM_EMAPP_QT_QTVIEWPORTPARAMETERBOX_H_

#include "emapp/Forward.h"

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QTimer;

namespace nanoem {

class IVectorValueState;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui viewport parameter box (the bottom row of
 *        ImGuiWindow::drawViewport).
 *
 *       With an active model it edits the active bone translation and orientation, otherwise the
 *       active camera look-at, angle and distance, using the same emapp value state classes the
 *       ImGui side used: edits preview live through setValue and a whole editing session (a drag
 *       or a focused spin box edit) collapses into a single undo entry on commit; a lone arrow
 *       click commits immediately so it stays undoable too. The row is pinned at the bottom of
 *       the viewport widget where ImGui's io-clipped version often did not even fit, and the
 *       ImGui side is masked out via kNativePanelTypeViewportParameterBox.
 */
class QtViewportParameterBox final : public QWidget {
public:
    QtViewportParameterBox(QtApplicationService *service, QWidget *parent);
    ~QtViewportParameterBox() override;

private:
    Project *currentProject() const;
    QDoubleSpinBox *createSpinBox(double minimum, double maximum, int decimals, double singleStep);
    void refresh();
    void applyBoneTranslation(const nanoem_f32_t *value);
    void applyBoneOrientation(const nanoem_f32_t *value);
    void applyCameraLookAt(const nanoem_f32_t *value);
    void applyCameraAngle(const nanoem_f32_t *value);
    void applyCameraDistance(const nanoem_f32_t *value);
    void commitPendingStates();
    void deletePendingStates();

    QtApplicationService *m_service;
    QLabel *m_translationLabel;
    QDoubleSpinBox *m_translationSpinBox[3];
    QLabel *m_orientationLabel;
    QDoubleSpinBox *m_orientationSpinBox[3];
    QLabel *m_lookAtLabel;
    QDoubleSpinBox *m_lookAtSpinBox[3];
    QLabel *m_angleLabel;
    QDoubleSpinBox *m_angleSpinBox[3];
    QLabel *m_distanceLabel;
    QDoubleSpinBox *m_distanceSpinBox;
    QWidget *m_modelRow;
    QWidget *m_cameraRow;
    IVectorValueState *m_boneTranslationState;
    IVectorValueState *m_boneOrientationState;
    IVectorValueState *m_cameraLookAtState;
    IVectorValueState *m_cameraAngleState;
    IVectorValueState *m_cameraDistanceState;
    QTimer *m_timer;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPORTPARAMETERBOX_H_ */
