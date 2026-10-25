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
#include "emapp/command/ModelObjectCommand.h"
#include "emapp/internal/ModelEditingState.h"
#include "emapp/model/Vertex.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QCheckBox;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
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
    QWidget *createVertexTab();
    QWidget *createFaceTab();
    QWidget *createHeightBasedTransformPane(QWidget *parent);
    QWidget *createNumericInputTransformPane(QWidget *parent);
    void appendCountRow(QGridLayout *layout, nanoem_rsize_t row, const char *key);
    void rebuildMeasureCounts();
    void syncVertexTab();
    void syncFaceTab();
    void rebuildVertexList();
    void rebuildVertexDetail();
    void rebuildFaceList();
    void rebuildFaceDetail();
    void updateSortedSelectedVertices(const IModelObjectSelection *selection);
    void vertexSelectionChanged();
    void vertexItemChanged(QListWidgetItem *item);
    void faceSelectionChanged();
    void faceItemChanged(QListWidgetItem *item);
    void appendVertexBoneWeightRows(
        QVBoxLayout *layout, QWidget *parent, nanoem_rsize_t numItems, nanoem_model_vertex_t *vertexPtr);
    void appendBatchVertexBoneWeightRows(QVBoxLayout *layout, QWidget *parent, nanoem_rsize_t numItems);
    void appendVertexPropertyPane(QVBoxLayout *layout, QWidget *parent, nanoem_model_vertex_t *vertexPtr);
    void appendBatchVertexPropertyPane(QVBoxLayout *layout, QWidget *parent);
    void appendFacePropertyPane(QVBoxLayout *layout, QWidget *parent, const Vector3UI32 &face);
    void toggleBone(const nanoem_model_bone_t *bonePtr);
    void toggleMaterial(const nanoem_model_material_t *materialPtr);
    void toggleVertex(const nanoem_model_vertex_t *vertexPtr);
    void toggleTab(int index);
    void syncWithProject();
    void updateWindowTitle();
    void finish();
    void resetMeasureState();
    void updateHeightBasedTransform();
    void syncNumericTransformSpinBoxes();
    void rebuildSystemFields();
    QString translate(const char *key) const;
    /* Material tab */
    QWidget *createMaterialTab();
    void syncMaterialTab();
    void rebuildMaterialList();
    void materialSelectionChanged();
    void materialItemChanged(QListWidgetItem *item);
    void rebuildMaterialDetail();
    void appendMaterialPropertyPane(QVBoxLayout *layout, QWidget *parent, nanoem_model_material_t *materialPtr);
    void appendBatchMaterialPropertyPane(QVBoxLayout *layout, QWidget *parent);
    void toggleMaterial(int index);

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
    nanoem_rsize_t m_vertexIndex;
    nanoem_rsize_t m_faceIndex;
    nanoem_rsize_t m_boneIndex;
    nanoem_rsize_t m_materialIndex;
    nanoem_rsize_t m_vertexCandidateUVAIndex;
    nanoem_rsize_t m_sortedSelectedVertexIndex;
    model::Vertex::List m_sortedSelectedVertices;
    command::BatchChangeAllVertexObjectsCommand::Parameter m_batchVertexParameter;
    int m_vertexDetailKind;
    int m_faceDetailKind;
    nanoem_rsize_t m_vertexDetailIndex;
    nanoem_rsize_t m_vertexDetailCount;
    nanoem_rsize_t m_faceDetailIndex;
    nanoem_rsize_t m_faceDetailCount;
    QLabel *m_vertexCountLabel;
    QListWidget *m_vertexList;
    QWidget *m_vertexDetail;
    QWidget *m_vertexDetailChild;
    QLabel *m_faceCountLabel;
    QListWidget *m_faceList;
    QWidget *m_faceDetail;
    QWidget *m_faceDetailChild;
    /* Material tab */
    QLabel *m_materialCountLabel;
    QListWidget *m_materialList;
    QWidget *m_materialDetail;
    QWidget *m_materialDetailChild;
    QLabel *m_materialNameLabel;
    QComboBox *m_materialNameCombo;
    QPushButton *m_materialNameButton;
    QDoubleSpinBox *m_materialAmbientSpinBox[3];
    QDoubleSpinBox *m_materialDiffuseSpinBox[4];
    QDoubleSpinBox *m_materialSpecularSpinBox[3];
    QDoubleSpinBox *m_materialSpecularPowerSpinBox;
    QDoubleSpinBox *m_materialEdgeSpinBox;
    QComboBox *m_materialPrimitiveCombo;
    QComboBox *m_materialSpheremapCombo;
    QCheckBox *m_materialToonCheckBox;
    QCheckBox *m_materialEdgeCheckBox;
    QCheckBox *m_materialCullingCheckBox;
    QCheckBox *m_materialShadowCheckBox;
    QCheckBox *m_materialSelfShadowCheckBox;
    QCheckBox *m_materialVertexColorCheckBox;
    QLineEdit *m_materialClobEdit;
    QPushButton *m_materialApplyButton;
    command::BatchChangeAllMaterialObjectsCommand::Parameter m_batchMaterialParameter;
    int m_materialDetailKind;
    nanoem_rsize_t m_materialDetailIndex;
    nanoem_rsize_t m_materialDetailCount;
    QLabel *m_materialAmbientLabel;
    QLabel *m_materialDiffuseLabel;
    QLabel *m_materialSpecularLabel;
    QLabel *m_materialEdgeLabel;
    QLabel *m_materialPrimitiveLabel;
    QLabel *m_materialSpheremapLabel;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELPARAMETERDIALOG_H_ */
