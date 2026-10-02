/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEDIALOG_H_
#define NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QTimer;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;
class QtInterpolationCurveWidget;

/**
 * @brief Native replacement of BoneKeyframeInterpolationCurveGraphDialog and
 *        CameraKeyframeInterpolationCurveGraphDialog (ImGui form dialogs drawn over the GL viewport).
 *
 *       Shows the MMD style editable curve (QtInterpolationCurveWidget) with the current frame
 *       position marker plus OK/Cancel. Edits are written through live without undo while the
 *       dialog is open (the same end states as the ImGui dialog which applies on OK); Cancel
 *       restores the control point captured when the dialog opened. Follows interpolation type,
 *       active bone and playback changes while open like the ImGui dialogs do, and closes itself
 *       when the project is swapped or the active model goes away.
 */
class QtInterpolationCurveGraphDialog final : public QDialog {
public:
    QtInterpolationCurveGraphDialog(
        QtApplicationService *service, Project *project, bool boneVariant, QWidget *parent);

private:
    void refresh();
    void handleCurveEdited(const Vector4U8 &value);
    void restoreSnapshot();
    void updatePlayhead(Project *project, const Vector4U8 &controlPoint);

    QtApplicationService *m_service;
    Project *m_project;
    QtInterpolationCurveWidget *m_curveWidget;
    QTimer *m_timer;
    Vector4U8 m_snapshotControlPoint;
    int m_snapshotType;
    bool m_boneVariant;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEDIALOG_H_ */
