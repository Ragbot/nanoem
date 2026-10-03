/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTCAMERAPARAMETERSDIALOG_H_
#define NANOEM_EMAPP_QT_QTCAMERAPARAMETERSDIALOG_H_

#include "emapp/Forward.h"
#include "emapp/Project.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class ICamera;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui CameraParametersDialog.
 *
 *       Non-modal like its ImGui counterpart: look-at/angle/distance are applied live while
 *       editing, OK pushes command::UpdateCameraCommand and dismissing the window (Cancel,
 *       Escape or the window close button, all of which reach reject()) restores the values
 *       captured when the dialog opened.
 */
class QtCameraParametersDialog final : public QDialog {
public:
    QtCameraParametersDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtCameraParametersDialog() override = default;

    void reject() override;

private:
    void handleLookAtChanged();
    void handleAngleChanged();
    void handleDistanceChanged();
    void handleAccepted();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    ICamera *m_camera;
    Vector3 m_lookAt;
    Vector3 m_angle;
    nanoem_f32_t m_distance;
    QDoubleSpinBox *m_lookAtSpinBox[3];
    QDoubleSpinBox *m_angleSpinBox[3];
    QDoubleSpinBox *m_distanceSpinBox;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTCAMERAPARAMETERSDIALOG_H_ */
