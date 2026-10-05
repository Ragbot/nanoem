/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELPARAMETERDIALOG_H_
#define NANOEM_EMAPP_QT_QTMODELPARAMETERDIALOG_H_

#include "emapp/Forward.h"
#include "emapp/IModelObjectSelection.h"
#include "emapp/Project.h"
#include "emapp/internal/ModelEditingState.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QWidget;

namespace nanoem {

class Model;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ModelParameterDialog.
 *
 *       Non-modal like its ImGui counterpart, and it runs the same editing session: opening it
 *       turns model editing on and snapshots the project and the active model through
 *       internal::ModelEditingState, closing it writes the captured files back and restores
 *       everything that snapshot saved. A timer keeps it in step with the project the way the
 *       ImGui dialog's per-frame draw did (the active model going away closes it).
 *
 *       Ported so far: the window shell with all 11 tabs (Soft Body only for PMX 2.1), the
 *       Measure tab (model statistics plus the height based and numeric batch transform panes)
 *       and the System tab (format, language, name, comment, encoding and additional UV count).
 *       The object list tabs and the dialog's action menus are still ImGui, so QtUIWindow keeps
 *       forwarding openModelParameterDialog to the ImGui dialog until they land; until then the
 *       harness is the only thing that opens this dialog.
 */
class QtModelParameterDialog final : public QDialog {
public:
    QtModelParameterDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtModelParameterDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum TabType {
        kTabTypeFirstEnum = 0,
        kTabTypeMeasure = kTabTypeFirstEnum,
        kTabTypeSystem,
        kTabTypeVertex,
        kTabTypeFace,
        kTabTypeMaterial,
        kTabTypeBone,
        kTabTypeMorph,
        kTabTypeLabel,
        kTabTypeRigidBody,
        kTabTypeJoint,
        kTabTypeSoftBody,
        kTabTypeMaxEnum,
    };

    static IModelObjectSelection::ObjectType convertToObjectType(TabType value) NANOEM_DECL_NOEXCEPT;

    QWidget *createMeasureTab();
    QWidget *createSystemTab();
    QWidget *createUnportedTab();
    QWidget *createHeightBasedTransformPane(QWidget *parent);
    QWidget *createNumericInputTransformPane(QWidget *parent);
    void appendCountRow(QGridLayout *layout, nanoem_rsize_t row, const char *key);
    void rebuildMeasureCounts();
    void toggleTab(int index);
    void syncWithProject();
    void updateWindowTitle();
    void finish();
    void resetMeasureState();
    void updateHeightBasedTransform();
    void syncNumericTransformSpinBoxes();
    void rebuildSystemFields();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    Model *m_activeModel;
    internal::ModelEditingState *m_savedState;
    Project::EditingMode m_editingMode;
    TabType m_tabType;
    bool m_finished;
    bool m_updatingFields;
    bool m_heightBased;
    nanoem_language_type_t m_language;
    bool m_showAllVertexPoints;
    bool m_showAllVertexFaces;
    bool m_showAllBones;
    bool m_showAllRigidBodies;
    bool m_showAllJoints;
    Vector3 m_savedTranslation;
    Vector3 m_savedRotation;
    Vector3 m_savedScale;
    nanoem_f32_t m_savedCMScaleFactor;
    nanoem_f32_t m_savedModelCorrectionHeight;
    nanoem_f32_t m_savedModelHeight;
    QTabWidget *m_tabs;
    QStackedWidget *m_transformStack;
    QLabel *m_scaleFactorLabel;
    QDoubleSpinBox *m_cmScaleFactor;
    QDoubleSpinBox *m_correctionHeight;
    QDoubleSpinBox *m_modelHeightPT;
    QDoubleSpinBox *m_modelHeightCM;
    QPushButton *m_heightApplyButton;
    QList<QLabel *> m_countValueLabels;
    QList<QDoubleSpinBox *> m_numericSpinBoxes;
    QComboBox *m_formatComboBox;
    QComboBox *m_codecComboBox;
    QRadioButton *m_japaneseRadioButton;
    QRadioButton *m_englishRadioButton;
    QLineEdit *m_nameEdit;
    QPlainTextEdit *m_commentEdit;
    QSpinBox *m_additionalUVSpinBox;
    QTimer *m_timer;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELPARAMETERDIALOG_H_ */
