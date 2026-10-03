/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTBONEPARAMETERSDIALOG_H_
#define NANOEM_EMAPP_QT_QTBONEPARAMETERSDIALOG_H_

#include "emapp/Forward.h"
#include "emapp/Project.h"
#include "emapp/model/BindPose.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class Model;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui BoneParameterDialog.
 *
 *       Non-modal like its ImGui counterpart: the active bone's translation/orientation are
 *       applied live, and dismissing the window (Cancel, Escape or the window close button, all
 *       of which reach reject()) restores the bind pose captured when the dialog opened. OK
 *       pushes Model::registerUpdateActiveBoneTransformCommand.
 *
 *       The dialog tracks the active bone: it closes when the active model changes or the model
 *       has no active bone, like ImGuiWindow::draw closing it through the visible flag.
 */
class QtBoneParametersDialog final : public QDialog {
public:
    QtBoneParametersDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtBoneParametersDialog() override = default;

    void reject() override;

private:
    void handleTranslationChanged();
    void handleOrientationChanged();
    void handleAccepted();
    void refresh();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    Model *m_model;
    model::BindPose m_bindPose;
    QDoubleSpinBox *m_translationSpinBox[3];
    QDoubleSpinBox *m_orientationSpinBox[3];
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTBONEPARAMETERSDIALOG_H_ */
