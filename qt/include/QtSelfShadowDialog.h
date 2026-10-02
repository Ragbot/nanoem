/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTSELFSHADOWDIALOG_H_
#define NANOEM_EMAPP_QT_QTSELFSHADOWDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui SelfShadowConfigurationDialog.
 *
 *       Non-modal like its ImGui counterpart: the three coverage mode radios, the distance slider
 *       and the Initialize button apply immediately to the project's ShadowCamera, while Register
 *       pushes the add-self-shadow-keyframes command for the current frame through the same
 *       CommandRegistrator the ImGui dialog used. The dialog is closed by QtUIWindow whenever the
 *       wrapped window resets or is destroyed since it holds a raw Project pointer.
 */
class QtSelfShadowDialog final : public QDialog {
public:
    QtSelfShadowDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtSelfShadowDialog() override = default;

private:
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTSELFSHADOWDIALOG_H_ */
