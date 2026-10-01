/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTBONEPANEL_H_
#define NANOEM_EMAPP_QT_QTBONEPANEL_H_

#include <QGroupBox>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QPushButton;
class QRadioButton;
class QTimer;

namespace nanoem {

class Model;
class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui bone panel (ImGuiWindow::drawBonePanel).
 *
 *       Provides the editing mode radios (select/rotate/move/none with the same enable gating on
 *       the active bone capabilities), the box select/select all/select dirty actions, the bone
 *       clipboard (copy/paste/inverse paste), the physics simulation toggle and the register
 *       keyframe button. Shown only while a model is active.
 */
class QtBonePanel final : public QGroupBox {
public:
    QtBonePanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    Project *currentProject();
    Model *activeModel();
    void refresh();
    void applyEditingMode(QAbstractButton *button);
    void setSelectMode();

    QtApplicationService *m_service;
    QTimer *m_timer;
    QButtonGroup *m_modeButtonGroup = nullptr;
    QRadioButton *m_selectRadioButton = nullptr;
    QRadioButton *m_rotateRadioButton = nullptr;
    QRadioButton *m_moveRadioButton = nullptr;
    QRadioButton *m_noneRadioButton = nullptr;
    QPushButton *m_boxSelectButton = nullptr;
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_selectDirtyButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_pasteButton = nullptr;
    QPushButton *m_inversePasteButton = nullptr;
    QCheckBox *m_physicsCheckBox = nullptr;
    QPushButton *m_registerButton = nullptr;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTBONEPANEL_H_ */
