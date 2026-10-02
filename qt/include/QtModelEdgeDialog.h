/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELEDGEDIALOG_H_
#define NANOEM_EMAPP_QT_QTMODELEDGEDIALOG_H_

#include "emapp/Forward.h"
#include "emapp/Project.h"

#include <QDialog>

namespace nanoem {

class Model;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ModelEdgeDialog.
 *
 *       Non-modal like its ImGui counterpart: opened only while a model is active, closes
 *       when the active model changes. Color (ColorDialog) and scale factor (QDoubleSpinBox)
 *       apply live. Register button pushes the add-model-keyframes command. OK restores the
 *       editing mode; Cancel restores color, scale factor and editing mode.
 */
class QtModelEdgeDialog final : public QDialog {
public:
    QtModelEdgeDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtModelEdgeDialog() override = default;

protected:
    void reject() override;

private:
    void handleAccepted();
    void handleColorChanged();
    void handleRegister();
    void closeEvent(QCloseEvent *event) override;
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    Model *m_activeModel;
    Vector4 m_lastColor;
    nanoem_f32_t m_lastScaleFactor;
    Project::EditingMode m_lastEditingMode;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELEDGEDIALOG_H_ */