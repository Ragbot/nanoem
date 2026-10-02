/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTPHYSICSENGINEDIALOG_H_
#define NANOEM_EMAPP_QT_QTPHYSICSENGINEDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui PhysicsEngineDialog (PhysicsSimulationDialog).
 *
 *       Non-modal like its ImGui counterpart: acceleration (QDoubleSpinBox),
 *       direction (3× QDoubleSpinBox with range -1..1), time step factor (QDoubleSpinBox).
 *       Changes apply live to the PhysicsEngine; OK calls engine->apply(),
 *       Cancel restores all values and calls apply().
 */
class QtPhysicsEngineDialog final : public QDialog {
public:
    QtPhysicsEngineDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtPhysicsEngineDialog() override = default;

protected:
    void reject() override;

private:
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QDoubleSpinBox *m_accelerationSpinBox;
    QDoubleSpinBox *m_directionSpinBox[3];
    QDoubleSpinBox *m_timeStepFactorSpinBox;
    Vector3 m_lastDirection;
    nanoem_f32_t m_lastAcceleration;
    nanoem_f32_t m_lastTimeStepFactor;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTPHYSICSENGINEDIALOG_H_ */