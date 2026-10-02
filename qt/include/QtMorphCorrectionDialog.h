/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMORPHCORRECTIONDIALOG_H_
#define NANOEM_EMAPP_QT_QTMORPHCORRECTIONDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QDoubleSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui MorphCorrectionDialog.
 *
 *       Non-modal like its ImGui counterpart: opened only while a model is active (and closes
 *       itself when the active model goes away), OK applies the mul/add weight pair through
 *       CommandRegistrator::registerCorrectAllSelectedMorphKeyframesCommand for the model that is
 *       active at click time, matching how ImGuiWindow re-targets m_activeModel every frame.
 */
class QtMorphCorrectionDialog final : public QDialog {
public:
    QtMorphCorrectionDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtMorphCorrectionDialog() override = default;

private:
    void handleAccepted();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QDoubleSpinBox *m_mul;
    QDoubleSpinBox *m_add;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMORPHCORRECTIONDIALOG_H_ */
