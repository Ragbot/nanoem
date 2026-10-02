/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTACCESSORYOUTSIDEPARENTDIALOG_H_
#define NANOEM_EMAPP_QT_QTACCESSORYOUTSIDEPARENTDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QComboBox;

namespace nanoem {

class Accessory;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui AccessoryOutsideParentDialog.
 *
 *       Non-modal like its ImGui counterpart: opened only while an accessory is active, closes
 *       when the active accessory changes or disappears. Two cascading combo boxes select the
 *       target model (or none) and the target bone (or none). Selecting a target model populates
 *       its bone list. The Register button pushes the same CommandRegistrator command the ImGui
 *       dialog used.
 */
class QtAccessoryOutsideParentDialog final : public QDialog {
public:
    QtAccessoryOutsideParentDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtAccessoryOutsideParentDialog() override = default;

private:
    void handleTargetModelChanged(int index);
    void handleTargetBoneChanged(int index);
    void handleRegister();
    void refresh();
    void closeEvent(QCloseEvent *event) override;
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QComboBox *m_targetModelCombo;
    QComboBox *m_targetBoneCombo;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTACCESSORYOUTSIDEPARENTDIALOG_H_ */