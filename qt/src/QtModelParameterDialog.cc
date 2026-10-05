/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelParameterDialog.h"
#include "QtApplicationService.h"

#include "emapp/Constants.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/StringUtils.h"
#include "emapp/command/ModelObjectCommand.h"
#include "emapp/command/TransformModelCommand.h"
#include "emapp/private/CommonInclude.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

const int kSyncIntervalMs = 100;
const int kNumMeasureRows = 11;
const double kSpinMaximum = 1.0e9;

static QString
formatCount(const nanoem_rsize_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}

static QString
formatTypeText(const nanoem_model_format_type_t type)
{
    switch (type) {
    case NANOEM_MODEL_FORMAT_TYPE_PMD_1_0:
        return QStringLiteral("PMD 1.0");
    case NANOEM_MODEL_FORMAT_TYPE_PMX_2_0:
        return QStringLiteral("PMX 2.0");
    case NANOEM_MODEL_FORMAT_TYPE_PMX_2_1:
        return QStringLiteral("PMX 2.1");
    default:
        return QStringLiteral("(Unknown)");
    }
}

static QString
codecTypeText(const nanoem_codec_type_t type)
{
    switch (type) {
    case NANOEM_CODEC_TYPE_SJIS:
        return QStringLiteral("ShiftJIS");
    case NANOEM_CODEC_TYPE_UTF8:
        return QStringLiteral("UTF-8");
    case NANOEM_CODEC_TYPE_UTF16:
        return QStringLiteral("UTF-16");
    default:
        return QStringLiteral("(Unknown)");
    }
}

static double
componentOf(const Vector3 &value, const int index)
{
    return index == 0 ? value.x : (index == 1 ? value.y : value.z);
}

static void
setComponentOf(Vector3 &value, const int index, const double v)
{
    if (index == 0) {
        value.x = float(v);
    }
    else if (index == 1) {
        value.y = float(v);
    }
    else {
        value.z = float(v);
    }
}

static QDoubleSpinBox *
createVector3ComponentSpinBox(QWidget *parent, const double value)
{
    auto *spinBox = new QDoubleSpinBox(parent);
    spinBox->setDecimals(6);
    spinBox->setRange(-kSpinMaximum, kSpinMaximum);
    spinBox->setValue(value);
    return spinBox;
}

static QFrame *
createHorizontalSeparator(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::HLine);
    frame->setFrameShadow(QFrame::Sunken);
    return frame;
}

static QDoubleSpinBox *
createMeasurementSpinBox(QWidget *parent, const int decimals, const QString &suffix, const bool readOnly)
{
    auto *spinBox = new QDoubleSpinBox(parent);
    spinBox->setDecimals(decimals);
    spinBox->setRange(-kSpinMaximum, kSpinMaximum);
    spinBox->setSuffix(suffix);
    spinBox->setReadOnly(readOnly);
    return spinBox;
}

} /* namespace anonymous */

IModelObjectSelection::ObjectType
QtModelParameterDialog::convertToObjectType(const TabType value) NANOEM_DECL_NOEXCEPT
{
    switch (value) {
    case kTabTypeBone:
        return IModelObjectSelection::kObjectTypeBone;
    case kTabTypeFace:
        return IModelObjectSelection::kObjectTypeFace;
    case kTabTypeJoint:
        return IModelObjectSelection::kObjectTypeJoint;
    case kTabTypeLabel:
        return IModelObjectSelection::kObjectTypeLabel;
    case kTabTypeMaterial:
        return IModelObjectSelection::kObjectTypeMaterial;
    case kTabTypeMorph:
        return IModelObjectSelection::kObjectTypeMorph;
    case kTabTypeRigidBody:
        return IModelObjectSelection::kObjectTypeRigidBody;
    case kTabTypeSoftBody:
        return IModelObjectSelection::kObjectTypeSoftBody;
    case kTabTypeVertex:
        return IModelObjectSelection::kObjectTypeVertex;
    default:
        return IModelObjectSelection::kObjectTypeNull;
    }
}

QtModelParameterDialog::QtModelParameterDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_activeModel(project->activeModel())
    , m_savedState(nullptr)
    , m_editingMode(project->editingMode())
    , m_tabType(kTabTypeMeasure)
    , m_finished(false)
    , m_updatingFields(false)
    , m_heightBased(true)
    , m_language(project->castLanguage())
    , m_showAllVertexPoints(m_activeModel ? m_activeModel->isShowAllVertexPoints() : false)
    , m_showAllVertexFaces(m_activeModel ? m_activeModel->isShowAllVertexFaces() : false)
    , m_showAllBones(m_activeModel ? m_activeModel->isShowAllBones() : false)
    , m_showAllRigidBodies(m_activeModel ? m_activeModel->isShowAllRigidBodyShapes() : false)
    , m_showAllJoints(m_activeModel ? m_activeModel->isShowAllJointShapes() : false)
    , m_savedTranslation(0)
    , m_savedRotation(0)
    , m_savedScale(1)
    , m_savedCMScaleFactor(Model::kDefaultCMScaleFactor)
    , m_savedModelCorrectionHeight(Model::kDefaultModelCorrectionHeight)
    , m_savedModelHeight(-1)
    , m_tabs(nullptr)
    , m_transformStack(nullptr)
    , m_scaleFactorLabel(nullptr)
    , m_cmScaleFactor(nullptr)
    , m_correctionHeight(nullptr)
    , m_modelHeightPT(nullptr)
    , m_modelHeightCM(nullptr)
    , m_heightApplyButton(nullptr)
    , m_formatComboBox(nullptr)
    , m_codecComboBox(nullptr)
    , m_japaneseRadioButton(nullptr)
    , m_englishRadioButton(nullptr)
    , m_nameEdit(nullptr)
    , m_commentEdit(nullptr)
    , m_additionalUVSpinBox(nullptr)
    , m_timer(nullptr)
{
    /* ModelParameterDialog's constructor drops whatever the previous object type selected
       before the first tab is drawn, so the selection and the editing mode start from here */
    if (m_activeModel) {
        internal::afterToggleModelEditingMode(IModelObjectSelection::kObjectTypeNull, m_activeModel, m_project);
    }
    m_savedState = nanoem_new(internal::ModelEditingState(m_editingMode));
    setAttribute(Qt::WA_DeleteOnClose, true);
    updateWindowTitle();
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createMeasureTab(), translate("nanoem.gui.window.model.tab.measure"));
    m_tabs->addTab(createSystemTab(), translate("nanoem.gui.window.model.tab.system"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.vertex"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.face"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.material"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.bone"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.morph"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.label"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.rigid-body"));
    m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.joint"));
    /* ImGuiWindow only shows Soft Body for PMX 2.1 models */
    if (m_activeModel && nanoemModelGetFormatType(m_activeModel->data()) == NANOEM_MODEL_FORMAT_TYPE_PMX_2_1) {
        m_tabs->addTab(createUnportedTab(), translate("nanoem.gui.window.model.tab.soft-body"));
    }
    layout->addWidget(m_tabs);
    /* the ImGui window sizes itself to kMinimumWindowWidth by 17 frame rows */
    setMinimumSize(640, 400);
    resize(760, 620);
    connect(m_tabs, &QTabWidget::currentChanged, this, &QtModelParameterDialog::toggleTab);
    if (m_activeModel) {
        Error error;
        internal::enableModelEditing(m_savedState, m_activeModel, m_project, error);
        error.addModalDialog(m_service);
    }
    rebuildSystemFields();
    updateHeightBasedTransform();
    syncNumericTransformSpinBoxes();
    rebuildMeasureCounts();
    /* the ImGui dialog ran all of this every frame: keep the counts fresh and follow the
       active model (or close when it goes away) without a frame callback of our own */
    m_timer = new QTimer(this);
    m_timer->setInterval(kSyncIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &QtModelParameterDialog::syncWithProject);
    m_timer->start();
}

QtModelParameterDialog::~QtModelParameterDialog()
{
    finish();
    nanoem_delete_safe(m_savedState);
}

void
QtModelParameterDialog::closeEvent(QCloseEvent *event)
{
    finish();
    QDialog::closeEvent(event);
}

void
QtModelParameterDialog::updateWindowTitle()
{
    QString title(translate("nanoem.gui.window.model.title"));
    if (m_activeModel) {
        title.append(QStringLiteral(" - "));
        title.append(QString::fromUtf8(m_activeModel->nameConstString()));
    }
    setWindowTitle(title);
}

void
QtModelParameterDialog::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (m_timer) {
        m_timer->stop();
    }
    if (m_activeModel) {
        m_project->setEditingMode(m_editingMode);
        m_activeModel->setShowAllVertexPoints(m_showAllVertexPoints);
        m_activeModel->setShowAllVertexFaces(m_showAllVertexFaces);
        m_activeModel->setShowAllBones(m_showAllBones);
        m_activeModel->setShowAllRigidBodyShapes(m_showAllRigidBodies);
        m_activeModel->setShowAllJointShapes(m_showAllJoints);
        if (m_tabType != kTabTypeSystem) {
            internal::beforeToggleModelEditingMode(convertToObjectType(m_tabType), m_activeModel, m_project);
            internal::afterToggleModelEditingMode(convertToObjectType(kTabTypeSystem), m_activeModel, m_project);
            m_tabType = kTabTypeSystem;
        }
    }
    Error error;
    internal::disableModelEditing(&m_savedState, m_project, error);
    error.notify(m_project->eventPublisher());
}

void
QtModelParameterDialog::syncWithProject()
{
    Project *project = m_service->projectHolder()->currentProject();
    if (!project || project != m_project) {
        close();
        return;
    }
    Model *model = m_project->activeModel();
    if (model != m_activeModel) {
        if (model) {
            Error error;
            internal::captureModelState(m_savedState, model, m_project, error);
            error.addModalDialog(m_service);
        }
        m_activeModel = model;
        if (!m_activeModel) {
            /* ModelParameterDialog::draw closes the window with its model */
            close();
            return;
        }
        updateWindowTitle();
        rebuildSystemFields();
    }
    rebuildMeasureCounts();
    if (m_tabs->currentIndex() == kTabTypeMeasure) {
        updateHeightBasedTransform();
    }
}

void
QtModelParameterDialog::toggleTab(const int index)
{
    const TabType value = static_cast<TabType>(index);
    if (value != m_tabType) {
        const TabType previous = m_tabType;
        if (m_activeModel) {
            internal::beforeToggleModelEditingMode(convertToObjectType(previous), m_activeModel, m_project);
            internal::afterToggleModelEditingMode(convertToObjectType(value), m_activeModel, m_project);
        }
        m_tabType = value;
        if (previous == kTabTypeMeasure && value != kTabTypeMeasure) {
            /* ModelParameterDialog::draw resets the measure fields while its tab is hidden */
            resetMeasureState();
            updateHeightBasedTransform();
            syncNumericTransformSpinBoxes();
        }
        else if (value == kTabTypeMeasure) {
            updateHeightBasedTransform();
            syncNumericTransformSpinBoxes();
        }
        else if (value == kTabTypeSystem) {
            rebuildSystemFields();
        }
    }
}

QString
QtModelParameterDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

QWidget *
QtModelParameterDialog::createUnportedTab()
{
    /* placeholder for the object list tabs still drawn by the ImGui dialog */
    return new QWidget(m_tabs);
}

QWidget *
QtModelParameterDialog::createMeasureTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *frame = new QFrame(widget);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *grid = new QGridLayout(frame);
    grid->setColumnStretch(1, 1);
    appendCountRow(grid, 0, "nanoem.gui.model.edit.measure.vertex");
    appendCountRow(grid, 1, "nanoem.gui.model.edit.measure.face");
    appendCountRow(grid, 2, "nanoem.gui.model.edit.measure.material");
    appendCountRow(grid, 3, "nanoem.gui.model.edit.measure.texture");
    appendCountRow(grid, 4, "nanoem.gui.model.edit.measure.bone");
    appendCountRow(grid, 5, "nanoem.gui.model.edit.measure.constraint");
    appendCountRow(grid, 6, "nanoem.gui.model.edit.measure.morph");
    appendCountRow(grid, 7, "nanoem.gui.model.edit.measure.label");
    appendCountRow(grid, 8, "nanoem.gui.model.edit.measure.rigid-body");
    appendCountRow(grid, 9, "nanoem.gui.model.edit.measure.joint");
    appendCountRow(grid, 10, "nanoem.gui.model.edit.measure.soft-body");
    /* the ImGui left pane takes 35% of the available width */
    layout->addWidget(frame, 35);
    auto *right = new QWidget(widget);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(translate("nanoem.gui.model.edit.measure.transform.title"), right));
    auto *radioLayout = new QHBoxLayout;
    auto *heightBasedRadioButton =
        new QRadioButton(translate("nanoem.gui.model.edit.measure.transform.height-based.title"), right);
    auto *numericInputRadioButton =
        new QRadioButton(translate("nanoem.gui.model.edit.measure.transform.numeric-input.title"), right);
    heightBasedRadioButton->setChecked(m_heightBased);
    radioLayout->addWidget(heightBasedRadioButton);
    radioLayout->addWidget(numericInputRadioButton);
    radioLayout->addStretch(1);
    rightLayout->addLayout(radioLayout);
    rightLayout->addWidget(createHorizontalSeparator(right));
    m_transformStack = new QStackedWidget(right);
    m_transformStack->addWidget(createHeightBasedTransformPane(right));
    m_transformStack->addWidget(createNumericInputTransformPane(right));
    rightLayout->addWidget(m_transformStack, 1);
    connect(heightBasedRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_heightBased = true;
            m_transformStack->setCurrentIndex(0);
        }
    });
    connect(numericInputRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_heightBased = false;
            m_transformStack->setCurrentIndex(1);
        }
    });
    layout->addWidget(right, 65);
    return widget;
}

void
QtModelParameterDialog::appendCountRow(QGridLayout *layout, const nanoem_rsize_t row, const char *key)
{
    const int gridRow = static_cast<int>(row) * 2;
    layout->addWidget(new QLabel(translate(key), layout->parentWidget()), gridRow, 0);
    auto *value = new QLabel(QStringLiteral("0"), layout->parentWidget());
    layout->addWidget(value, gridRow, 1);
    m_countValueLabels.append(value);
    if (row > 0) {
        layout->addWidget(createHorizontalSeparator(layout->parentWidget()), gridRow - 1, 0, 1, 2);
    }
}

void
QtModelParameterDialog::rebuildMeasureCounts()
{
    if (!m_activeModel || m_countValueLabels.size() != kNumMeasureRows) {
        return;
    }
    nanoem_rsize_t numVertices, numIndices, numMaterials, numTextures, numBones,
        numConstraints = 0, numMorphs, numLabels, numRigidBodies, numJoints, numSoftBodies;
    nanoem_model_t *data = m_activeModel->data();
    nanoemModelGetAllVertexObjects(data, &numVertices);
    nanoemModelGetAllVertexIndices(data, &numIndices);
    nanoemModelGetAllMaterialObjects(data, &numMaterials);
    nanoemModelGetAllTextureObjects(data, &numTextures);
    {
        nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(data, &numBones);
        for (nanoem_rsize_t i = 0; i < numBones; i++) {
            if (nanoemModelBoneHasConstraint(bones[i])) {
                numConstraints++;
            }
        }
    }
    nanoemModelGetAllMorphObjects(data, &numMorphs);
    nanoemModelGetAllLabelObjects(data, &numLabels);
    nanoemModelGetAllRigidBodyObjects(data, &numRigidBodies);
    nanoemModelGetAllJointObjects(data, &numJoints);
    nanoemModelGetAllSoftBodyObjects(data, &numSoftBodies);
    const nanoem_rsize_t values[] = { numVertices, numIndices / 3, numMaterials, numTextures, numBones, numConstraints,
        numMorphs, numLabels, numRigidBodies, numJoints, numSoftBodies };
    for (int i = 0; i < kNumMeasureRows; i++) {
        m_countValueLabels[i]->setText(formatCount(values[i]));
    }
}

QWidget *
QtModelParameterDialog::createHeightBasedTransformPane(QWidget *parent)
{
    auto *pane = new QWidget(parent);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.measure.transform.height-based.a"), pane));
    m_modelHeightPT = createMeasurementSpinBox(pane, 6, QStringLiteral(" pt"), true);
    layout->addWidget(m_modelHeightPT);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.measure.transform.height-based.b"), pane));
    m_cmScaleFactor = createMeasurementSpinBox(pane, 6, QStringLiteral(" pt"), false);
    layout->addWidget(m_cmScaleFactor);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.measure.transform.height-based.c"), pane));
    m_correctionHeight = createMeasurementSpinBox(pane, 3, QStringLiteral(" cm"), false);
    layout->addWidget(m_correctionHeight);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.measure.transform.height-based.result"), pane));
    m_modelHeightCM = createMeasurementSpinBox(pane, 3, QStringLiteral(" cm"), false);
    layout->addWidget(m_modelHeightCM);
    m_scaleFactorLabel = new QLabel(pane);
    layout->addWidget(m_scaleFactorLabel);
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    m_heightApplyButton = new QPushButton(translate("nanoem.gui.model.edit.measure.transform.apply"), pane);
    auto *resetButton = new QPushButton(translate("nanoem.gui.model.edit.measure.transform.reset"), pane);
    buttonLayout->addWidget(m_heightApplyButton);
    buttonLayout->addWidget(resetButton);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);
    layout->addStretch(1);
    connect(m_cmScaleFactor, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_updatingFields) {
            m_savedCMScaleFactor = float(value);
            updateHeightBasedTransform();
        }
    });
    connect(m_correctionHeight, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_updatingFields) {
            m_savedModelCorrectionHeight = float(value);
            m_savedModelHeight = -1;
            updateHeightBasedTransform();
        }
    });
    connect(m_modelHeightCM, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_updatingFields) {
            m_savedModelHeight = float(value);
            updateHeightBasedTransform();
        }
    });
    connect(m_heightApplyButton, &QPushButton::clicked, this, [this]() {
        const nanoem_f32_t modelHeight = m_activeModel->measureHeight(1.0f, 0.0f),
                           modelHeightCM = (modelHeight / m_savedCMScaleFactor) + m_savedModelCorrectionHeight;
        const nanoem_f32_t scaleFactor = m_savedModelHeight / modelHeightCM;
        const Matrix4x4 transformModelMatrix(glm::scale(Constants::kIdentity, Vector3(scaleFactor)));
        m_activeModel->pushUndo(command::TransformModelCommand::create(m_activeModel, transformModelMatrix));
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetMeasureState();
        updateHeightBasedTransform();
        syncNumericTransformSpinBoxes();
    });
    return pane;
}

QWidget *
QtModelParameterDialog::createNumericInputTransformPane(QWidget *parent)
{
    auto *pane = new QWidget(parent);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    const char *const titles[] = { "Translation", "Rotation", "Scale" };
    Vector3 *const values[] = { &m_savedTranslation, &m_savedRotation, &m_savedScale };
    for (int i = 0; i < 3; i++) {
        layout->addWidget(new QLabel(QString::fromUtf8(titles[i]), pane));
        auto *row = new QHBoxLayout;
        Vector3 *target = values[i];
        for (int j = 0; j < 3; j++) {
            auto *spinBox = createVector3ComponentSpinBox(pane, componentOf(*target, j));
            connect(spinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
                [this, target, j](double value) {
                    if (!m_updatingFields) {
                        setComponentOf(*target, j, value);
                    }
                });
            m_numericSpinBoxes.append(spinBox);
            row->addWidget(spinBox);
        }
        row->addStretch(1);
        layout->addLayout(row);
    }
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    auto *applyButton = new QPushButton(translate("nanoem.gui.model.edit.measure.transform.apply"), pane);
    auto *resetButton = new QPushButton(translate("nanoem.gui.model.edit.measure.transform.reset"), pane);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(resetButton);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);
    layout->addStretch(1);
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        const Matrix4x4 scaleMatrix(glm::scale(Constants::kIdentity, m_savedScale)),
            rotateX(glm::rotate(Constants::kIdentity, m_savedRotation.x, Constants::kUnitX)),
            rotateY(glm::rotate(Constants::kIdentity, m_savedRotation.y, Constants::kUnitY)),
            rotateZ(glm::rotate(Constants::kIdentity, m_savedRotation.z, Constants::kUnitZ)),
            rotateMatrix(rotateZ * rotateY * rotateX),
            translateMatrix(glm::translate(Constants::kIdentity, m_savedTranslation)),
            transformModelMatrix(scaleMatrix * rotateMatrix * translateMatrix);
        m_activeModel->pushUndo(command::TransformModelCommand::create(m_activeModel, transformModelMatrix));
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetMeasureState();
        updateHeightBasedTransform();
        syncNumericTransformSpinBoxes();
    });
    return pane;
}

void
QtModelParameterDialog::resetMeasureState()
{
    m_savedCMScaleFactor = Model::kDefaultCMScaleFactor;
    m_savedModelCorrectionHeight = Model::kDefaultModelCorrectionHeight;
    m_savedTranslation = Constants::kZeroV3;
    m_savedRotation = Constants::kZeroV3;
    m_savedScale = Vector3(1);
    m_savedModelHeight = -1;
}

void
QtModelParameterDialog::syncNumericTransformSpinBoxes()
{
    const Vector3 *const values[] = { &m_savedTranslation, &m_savedRotation, &m_savedScale };
    if (m_numericSpinBoxes.size() != 9) {
        return;
    }
    m_updatingFields = true;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m_numericSpinBoxes[i * 3 + j]->setValue(componentOf(*values[i], j));
        }
    }
    m_updatingFields = false;
}

void
QtModelParameterDialog::updateHeightBasedTransform()
{
    if (!m_activeModel || !m_scaleFactorLabel) {
        return;
    }
    const nanoem_f32_t modelHeight = m_activeModel->measureHeight(1.0f, 0.0f),
                       modelHeightCM = (modelHeight / m_savedCMScaleFactor) + m_savedModelCorrectionHeight;
    if (m_savedModelHeight < 0.0f) {
        m_savedModelHeight = modelHeightCM;
    }
    const nanoem_f32_t scaleFactor = m_savedModelHeight / modelHeightCM;
    m_updatingFields = true;
    m_modelHeightPT->setValue(modelHeight);
    m_cmScaleFactor->setValue(m_savedCMScaleFactor);
    m_correctionHeight->setValue(m_savedModelCorrectionHeight);
    m_modelHeightCM->setValue(m_savedModelHeight);
    m_updatingFields = false;
    m_scaleFactorLabel->setText(QStringLiteral("%1: %2")
            .arg(translate("nanoem.gui.model.edit.measure.transform.height-based.scale-factor"))
            .arg(scaleFactor, 0, 'f', 6));
    m_heightApplyButton->setEnabled(scaleFactor != 1.0f);
}

QWidget *
QtModelParameterDialog::createSystemTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(widget);
    auto *versionRow = new QHBoxLayout;
    versionRow->addWidget(new QLabel(QStringLiteral("Version"), widget));
    m_formatComboBox = new QComboBox(widget);
    m_formatComboBox->setFixedWidth(80);
    for (int i = NANOEM_MODEL_FORMAT_TYPE_FIRST_ENUM; i < NANOEM_MODEL_FORMAT_TYPE_MAX_ENUM; i++) {
        const nanoem_model_format_type_t format = static_cast<nanoem_model_format_type_t>(i);
        m_formatComboBox->addItem(formatTypeText(format), i);
    }
    /* PMD 1.0 models cannot be converted: the ImGui combo disables that row */
    const int pmdIndex = m_formatComboBox->findData(int(NANOEM_MODEL_FORMAT_TYPE_PMD_1_0));
    if (auto *itemModel = qobject_cast<QStandardItemModel *>(m_formatComboBox->model())) {
        if (pmdIndex >= 0) {
            itemModel->item(pmdIndex)->setEnabled(false);
        }
    }
    versionRow->addWidget(m_formatComboBox);
    versionRow->addStretch(1);
    layout->addLayout(versionRow);
    auto *languageRow = new QHBoxLayout;
    m_japaneseRadioButton =
        new QRadioButton(translate("nanoem.gui.window.preference.project.language.japanese"), widget);
    m_englishRadioButton = new QRadioButton(translate("nanoem.gui.window.preference.project.language.english"), widget);
    languageRow->addWidget(m_japaneseRadioButton);
    languageRow->addWidget(m_englishRadioButton);
    languageRow->addStretch(1);
    layout->addLayout(languageRow);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.system.name"), widget));
    m_nameEdit = new QLineEdit(widget);
    layout->addWidget(m_nameEdit);
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.system.comment"), widget));
    m_commentEdit = new QPlainTextEdit(widget);
    m_commentEdit->setMinimumHeight(160);
    layout->addWidget(m_commentEdit, 1);
    auto *optionRow = new QHBoxLayout;
    optionRow->addWidget(new QLabel(translate("nanoem.gui.model.edit.system.encoding"), widget));
    m_codecComboBox = new QComboBox(widget);
    for (int i = NANOEM_CODEC_TYPE_FIRST_ENUM; i < NANOEM_CODEC_TYPE_MAX_ENUM; i++) {
        const nanoem_codec_type_t codec = static_cast<nanoem_codec_type_t>(i);
        m_codecComboBox->addItem(codecTypeText(codec), i);
    }
    /* the ImGui combo disables the ShiftJIS row */
    const int sjisIndex = m_codecComboBox->findData(int(NANOEM_CODEC_TYPE_SJIS));
    if (auto *itemModel = qobject_cast<QStandardItemModel *>(m_codecComboBox->model())) {
        if (sjisIndex >= 0) {
            itemModel->item(sjisIndex)->setEnabled(false);
        }
    }
    optionRow->addWidget(m_codecComboBox);
    optionRow->addSpacing(24);
    optionRow->addWidget(new QLabel(translate("nanoem.gui.model.edit.system.uva"), widget));
    m_additionalUVSpinBox = new QSpinBox(widget);
    m_additionalUVSpinBox->setRange(0, 4);
    optionRow->addWidget(m_additionalUVSpinBox);
    optionRow->addStretch(1);
    layout->addLayout(optionRow);
    connect(m_japaneseRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_language = NANOEM_LANGUAGE_TYPE_JAPANESE;
            rebuildSystemFields();
        }
    });
    connect(m_englishRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_language = NANOEM_LANGUAGE_TYPE_ENGLISH;
            rebuildSystemFields();
        }
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_updatingFields) {
            nanoem_unicode_string_factory_t *factory = m_project->unicodeStringFactory();
            StringUtils::UnicodeStringScope scope(factory);
            const QByteArray utf8(text.toUtf8());
            if (StringUtils::tryGetString(factory, utf8.constData(), scope)) {
                nanoem_status_t status = NANOEM_STATUS_SUCCESS;
                command::ScopedMutableModel scoped(m_activeModel, &status);
                nanoemMutableModelSetName(scoped, scope.value(), m_language, &status);
            }
        }
    });
    connect(m_commentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_updatingFields) {
            nanoem_unicode_string_factory_t *factory = m_project->unicodeStringFactory();
            StringUtils::UnicodeStringScope scope(factory);
            const QByteArray utf8(m_commentEdit->toPlainText().toUtf8());
            if (StringUtils::tryGetString(factory, utf8.constData(), scope)) {
                nanoem_status_t status = NANOEM_STATUS_SUCCESS;
                command::ScopedMutableModel scoped(m_activeModel, &status);
                nanoemMutableModelSetComment(scoped, scope.value(), m_language, &status);
            }
        }
    });
    connect(m_formatComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            if (m_updatingFields || index < 0 || !m_activeModel) {
                return;
            }
            const nanoem_model_format_type_t format =
                static_cast<nanoem_model_format_type_t>(m_formatComboBox->itemData(index).toInt());
            if (format != nanoemModelGetFormatType(m_activeModel->data())) {
                nanoem_status_t status = NANOEM_STATUS_SUCCESS;
                command::ScopedMutableModel scoped(m_activeModel, &status);
                nanoemMutableModelSetFormatType(scoped, format);
            }
        });
    connect(m_codecComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            if (m_updatingFields || index < 0 || !m_activeModel) {
                return;
            }
            const nanoem_codec_type_t codec =
                static_cast<nanoem_codec_type_t>(m_codecComboBox->itemData(index).toInt());
            if (codec != nanoemModelGetCodecType(m_activeModel->data())) {
                nanoem_status_t status = NANOEM_STATUS_SUCCESS;
                command::ScopedMutableModel scoped(m_activeModel, &status);
                nanoemMutableModelSetCodecType(scoped, codec);
            }
        });
    connect(
        m_additionalUVSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_updatingFields || !m_activeModel) {
                return;
            }
            nanoem_status_t status = NANOEM_STATUS_SUCCESS;
            command::ScopedMutableModel scoped(m_activeModel, &status);
            nanoemMutableModelSetAdditionalUVSize(scoped, value);
        });
    return widget;
}

void
QtModelParameterDialog::rebuildSystemFields()
{
    if (!m_activeModel || !m_nameEdit) {
        return;
    }
    nanoem_model_t *data = m_activeModel->data();
    {
        const QSignalBlocker blocker(m_formatComboBox);
        m_formatComboBox->setCurrentIndex(m_formatComboBox->findData(int(nanoemModelGetFormatType(data))));
    }
    {
        const QSignalBlocker blocker(m_codecComboBox);
        m_codecComboBox->setCurrentIndex(m_codecComboBox->findData(int(nanoemModelGetCodecType(data))));
    }
    {
        const QSignalBlocker blocker(m_additionalUVSpinBox);
        m_additionalUVSpinBox->setValue(int(nanoemModelGetAdditionalUVSize(data)));
    }
    {
        const QSignalBlocker blocker(m_japaneseRadioButton);
        const QSignalBlocker englishBlocker(m_englishRadioButton);
        m_japaneseRadioButton->setChecked(m_language == NANOEM_LANGUAGE_TYPE_JAPANESE);
        m_englishRadioButton->setChecked(m_language == NANOEM_LANGUAGE_TYPE_ENGLISH);
    }
    nanoem_unicode_string_factory_t *factory = m_project->unicodeStringFactory();
    String name, comment;
    StringUtils::getUtf8String(nanoemModelGetName(data, m_language), factory, name);
    StringUtils::getUtf8String(nanoemModelGetComment(data, m_language), factory, comment);
    {
        const QSignalBlocker blocker(m_nameEdit);
        const QSignalBlocker commentBlocker(m_commentEdit);
        m_nameEdit->setText(QString::fromUtf8(name.c_str()));
        m_commentEdit->setPlainText(QString::fromUtf8(comment.c_str()));
    }
}

} /* namespace qt */
} /* namespace nanoem */
