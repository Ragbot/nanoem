/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTINTERPOLATIONPANEL_H_
#define NANOEM_EMAPP_QT_QTINTERPOLATIONPANEL_H_

#include "QtInterpolationCurveGraphDialog.h"

#include <QGroupBox>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QPushButton;
class QTimer;

namespace nanoem {

class Model;
class Project;

namespace model {
class Bone;
} /* namespace model */

namespace qt {

class QtApplicationService;
class QtInterpolationCurveWidget;

/**
 * @brief Native replacement of the ImGui interpolation panel
 *        (ImGuiWindow::drawBoneInterpolationPanel and drawCameraInterpolationPanel).
 *
 *       Shows the active interpolation type selector, the MMD style editable bezier curve
 *       (QtInterpolationCurveWidget written through to the active bone or the global camera
 *       exactly like the ImGui panel, without undo), the curve graph dialog opener (a native Qt
 *       dialog instead of the ImGui one drawn over the viewport) and the shared copy/paste/linear
 *       controls with the automatic adjustment toggle. Displays the bone variant while a model is
 *       active and the camera variant otherwise.
 */
class QtInterpolationPanel final : public QGroupBox {
public:
    QtInterpolationPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    Project *currentProject();
    void refresh();
    void refreshBone();
    void selectType(int index);
    void handleCurveEdited(const Vector4U8 &value);
    void openGraphDialog();
    void openCurveGraphDialog(Project *project);

    QtApplicationService *m_service;
    QTimer *m_timer;
    QPushButton *m_graphButton = nullptr;
    QComboBox *m_typeComboBox = nullptr;
    QtInterpolationCurveWidget *m_curveWidget = nullptr;
    QPointer<QtInterpolationCurveGraphDialog> m_boneGraphDialog;
    QPointer<QtInterpolationCurveGraphDialog> m_cameraGraphDialog;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_pasteButton = nullptr;
    QPushButton *m_linearButton = nullptr;
    QCheckBox *m_automaticCheckBox = nullptr;
    model::Bone *m_bone = nullptr;
    int m_currentType = 0;
    int m_comboCount = -1;
    bool m_boneVariant = false;
    bool m_comboVariant = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTINTERPOLATIONPANEL_H_ */
