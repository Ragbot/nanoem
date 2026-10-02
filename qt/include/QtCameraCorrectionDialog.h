/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTCAMERACORRECTIONDIALOG_H_
#define NANOEM_EMAPP_QT_QTCAMERACORRECTIONDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui CameraCorrectionDialog.
 *
 *       Non-modal like its ImGui counterpart and independent of the active model (like the ImGui
 *       version, which ignores its Project argument at open time): OK applies the look-at,
 *       angle and distance mul/add factors through
 *       CommandRegistrator::registerCorrectAllSelectedCameraKeyframesCommand.
 */
class QtCameraCorrectionDialog final : public QDialog {
public:
    QtCameraCorrectionDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtCameraCorrectionDialog() override = default;

private:
    void handleAccepted();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QDoubleSpinBox *m_lookAtMul[3];
    QDoubleSpinBox *m_lookAtAdd[3];
    QDoubleSpinBox *m_angleMul[3];
    QDoubleSpinBox *m_angleAdd[3];
    QDoubleSpinBox *m_distanceMul;
    QDoubleSpinBox *m_distanceAdd;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTCAMERACORRECTIONDIALOG_H_ */
