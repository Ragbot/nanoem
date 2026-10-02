/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELOUTSIDEPARENTDIALOG_H_
#define NANOEM_EMAPP_QT_QTMODELOUTSIDEPARENTDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QComboBox;
class QLabel;

namespace nanoem {

class Model;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ModelOutsideParentDialog.
 *
 *       Non-modal like its ImGui counterpart: opened only while a model is active, closes
 *       when the active model changes or disappears. Three cascading combo boxes select the
 *       subject bone (or none), the target model (or none), and the target bone (or none).
 *       Selecting a target model populates its bone list for the target bone combo. Read-only
 *       translation/orientation fields show the subject bone's current transform. The Register
 *       button pushes the same CommandRegistrator command the ImGui dialog used.
 */
class QtModelOutsideParentDialog final : public QDialog {
public:
    QtModelOutsideParentDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtModelOutsideParentDialog() override = default;

private:
    void handleSubjectBoneChanged(int index);
    void handleTargetModelChanged(int index);
    void handleTargetBoneChanged(int index);
    void handleRegister();
    void updateTranslationOrientation();
    void refresh();
    void closeEvent(QCloseEvent *event) override;

public:
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QComboBox *m_subjectBoneCombo;
    QComboBox *m_targetModelCombo;
    QComboBox *m_targetBoneCombo;
    QLabel *m_translationLabel;
    QLabel *m_orientationLabel;
    bool m_updating = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELOUTSIDEPARENTDIALOG_H_ */