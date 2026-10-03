/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTSCALEALLSELECTEDKEYFRAMESDIALOG_H_
#define NANOEM_EMAPP_QT_QTSCALEALLSELECTEDKEYFRAMESDIALOG_H_

#include "emapp/Forward.h"
#include "emapp/Project.h"

#include <QDialog>

class QSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ScaleAllSelectedKeyframesDialog.
 *
 *        Non-modal like its ImGui counterpart: shows the current selection segment (read only)
 *        and a scale factor, and on OK pushes CommandRegistrator::
 *        registerScaleAllMotionKeyframesInCommand for the whole keyframe type set.
 */
class QtScaleAllSelectedKeyframesDialog final : public QDialog {
public:
    QtScaleAllSelectedKeyframesDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtScaleAllSelectedKeyframesDialog() override = default;

private:
    void refresh();
    void handleAccepted();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    nanoem_f32_t m_scaleFactor;
    QSpinBox *m_fromSpinBox;
    QSpinBox *m_toSpinBox;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTSCALEALLSELECTEDKEYFRAMESDIALOG_H_ */
