/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTBONECORRECTIONDIALOG_H_
#define NANOEM_EMAPP_QT_QTBONECORRECTIONDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui BoneCorrectionDialog.
 *
 *       Non-modal like its ImGui counterpart: opened only while a model is active (and closes
 *       itself when the active model goes away), OK applies the translation and orientation
 *       mul/add factor rows through
 *       CommandRegistrator::registerCorrectAllSelectedBoneKeyframesCommand for the model that is
 *       active at click time, matching how ImGuiWindow re-targets m_activeModel every frame.
 */
class QtBoneCorrectionDialog final : public QDialog {
public:
    QtBoneCorrectionDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtBoneCorrectionDialog() override = default;

private:
    void handleAccepted();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QDoubleSpinBox *m_translationMul[3];
    QDoubleSpinBox *m_translationAdd[3];
    QDoubleSpinBox *m_orientationMul[3];
    QDoubleSpinBox *m_orientationAdd[3];
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTBONECORRECTIONDIALOG_H_ */
