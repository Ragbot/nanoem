/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTACCESSORYPANEL_H_
#define NANOEM_EMAPP_QT_QTACCESSORYPANEL_H_

#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace nanoem {

class Accessory;
class Project;

namespace internal {
class AccessoryOrientationVectorValueState;
class AccessoryScaleFactorValueState;
class AccessoryTranslationVectorValueState;
class AccessoryOpacityValueState;
} /* namespace internal */

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui accessory panel (ImGuiWindow::drawAccessoryPanel).
 *
 *       Provides the active accessory selector (with the Ctrl+click name editing affordance),
 *       load/delete buttons, the visibility toggle, the outside parent form dialog opener (still
 *       rendered by ImGui), the translation/orientation/scale/opacity editors (each committed
 *       through its Accessory value state so a drag becomes one undo entry) and the register
 *       keyframe button. Shown only while no model is active.
 */
class QtAccessoryPanel final : public QGroupBox {
public:
    QtAccessoryPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Project *currentProject();
    Accessory *activeAccessory();
    void refresh();
    void refreshAccessoryComboBox(Project *project, Accessory *activeAccessory);
    void beginAccessoryNameEditing();
    void commitAccessoryName();
    void commitStates();
    void validateStates(Project *project);
    void handleTranslationChanged();
    void handleOrientationChanged();
    void handleScaleChanged(double value);
    void handleOpacityChanged(double value);
    void commitTranslation();
    void commitOrientation();
    void commitScale();
    void commitOpacity();

    QtApplicationService *m_service;
    QTimer *m_timer;
    QStackedWidget *m_nameStack = nullptr;
    QComboBox *m_accessoryComboBox = nullptr;
    QLineEdit *m_nameLineEdit = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QCheckBox *m_visibleCheckBox = nullptr;
    QPushButton *m_outsideParentButton = nullptr;
    QDoubleSpinBox *m_translationSpinBoxes[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *m_orientationSpinBoxes[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *m_scaleSpinBox = nullptr;
    QDoubleSpinBox *m_opacitySpinBox = nullptr;
    QPushButton *m_registerButton = nullptr;
    internal::AccessoryTranslationVectorValueState *m_translationState = nullptr;
    internal::AccessoryOrientationVectorValueState *m_orientationState = nullptr;
    internal::AccessoryScaleFactorValueState *m_scaleState = nullptr;
    internal::AccessoryOpacityValueState *m_opacityState = nullptr;
    const Project *m_stateProject = nullptr;
    const Accessory *m_stateAccessory = nullptr;
    bool m_editingAccessoryName = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTACCESSORYPANEL_H_ */
