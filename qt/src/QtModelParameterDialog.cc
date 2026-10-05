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
#include "emapp/internal/ModelObjectSelection.h"
#include "emapp/model/Bone.h"
#include "emapp/model/Material.h"
#include "emapp/private/CommonInclude.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

#include <functional>

namespace nanoem {
namespace qt {
namespace {

const int kSyncIntervalMs = 100;
const int kNumMeasureRows = 11;
const int kObjectListPaneWidth = 200;
const double kSpinMaximum = 1.0e9;
/* QDoubleSpinBox sizes itself for the widest value it may display, so the object fields (up to four
   of them share a row) keep their range modest: 1e9 makes that row wider than the pane scrolls to */
const double kObjectFieldMaximum = 1.0e6;

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

static QString
vertexTypeText(const nanoem_model_vertex_type_t type)
{
    switch (type) {
    case NANOEM_MODEL_VERTEX_TYPE_BDEF1:
        return QStringLiteral("BDEF1");
    case NANOEM_MODEL_VERTEX_TYPE_BDEF2:
        return QStringLiteral("BDEF2");
    case NANOEM_MODEL_VERTEX_TYPE_BDEF4:
        return QStringLiteral("BDEF4");
    case NANOEM_MODEL_VERTEX_TYPE_SDEF:
        return QStringLiteral("SDEF");
    case NANOEM_MODEL_VERTEX_TYPE_QDEF:
        return QStringLiteral("QDEF");
    default:
        return QStringLiteral("(Unknown)");
    }
}

static QString
vertexText(const nanoem_model_vertex_t *vertexPtr, const nanoem_rsize_t index)
{
    const nanoem_f32_t *origin = nanoemModelVertexGetOrigin(vertexPtr);
    return QStringLiteral("%1 (%2, %3, %4)")
        .arg(formatCount(index + 1))
        .arg(origin[0], 0, 'f', 2)
        .arg(origin[1], 0, 'f', 2)
        .arg(origin[2], 0, 'f', 2);
}

static QString
faceText(const nanoem_rsize_t index, const nanoem_u32_t *vertexIndices, const nanoem_rsize_t offset)
{
    return QStringLiteral("%1 (%2, %3, %4)")
        .arg(formatCount(index + 1))
        .arg(vertexIndices[offset + 0])
        .arg(vertexIndices[offset + 1])
        .arg(vertexIndices[offset + 2]);
}

static QToolButton *
createLinkButton(QWidget *parent, const bool enabled, const QString &toolTip, const std::function<void()> &jump)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setText(QStringLiteral("→"));
    button->setToolTip(toolTip);
    button->setEnabled(enabled);
    button->setFixedWidth(28);
    QObject::connect(button, &QToolButton::clicked, [jump]() { jump(); });
    return button;
}

static QList<QDoubleSpinBox *>
appendComponentFields(QWidget *parent, QVBoxLayout *layout, const QString &title, const QVector<double> &values,
    const int decimals, QWidget *prefixWidget = nullptr, const int prefixStretch = 0)
{
    if (!title.isEmpty()) {
        layout->addWidget(new QLabel(title, parent));
    }
    auto *row = new QHBoxLayout;
    if (prefixWidget) {
        row->addWidget(prefixWidget, prefixStretch);
    }
    QList<QDoubleSpinBox *> fields;
    for (int i = 0; i < values.size(); i++) {
        auto *spinBox = new QDoubleSpinBox(parent);
        spinBox->setDecimals(decimals);
        spinBox->setRange(-kObjectFieldMaximum, kObjectFieldMaximum);
        spinBox->setValue(values[i]);
        row->addWidget(spinBox, 1);
        fields.append(spinBox);
    }
    layout->addLayout(row);
    return fields;
}

/**
 * @brief Adds a label and the row of numeric fields ModelParameterDialog shows as InputFloatN, and keeps
 *        writing whatever the user typed back as one vector, the way the ImGui field did per edit.
 */
static void
appendComponentField(QWidget *parent, QVBoxLayout *layout, const QString &title, const QVector<double> &values,
    const int decimals, const std::function<void(const QVector<double> &)> &commit, QWidget *prefixWidget = nullptr,
    const int prefixStretch = 0)
{
    const QList<QDoubleSpinBox *> fields =
        appendComponentFields(parent, layout, title, values, decimals, prefixWidget, prefixStretch);
    if (commit) {
        for (QDoubleSpinBox *field : fields) {
            QObject::connect(field, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                [fields, commit](double) {
                    QVector<double> current;
                    for (QDoubleSpinBox *f : fields) {
                        current.append(f->value());
                    }
                    commit(current);
                });
        }
    }
}

static QComboBox *
createVertexTypeCombo(QWidget *parent, const nanoem_model_vertex_type_t currentType)
{
    auto *comboBox = new QComboBox(parent);
    for (int i = NANOEM_MODEL_VERTEX_TYPE_FIRST_ENUM; i < NANOEM_MODEL_VERTEX_TYPE_MAX_ENUM; i++) {
        const nanoem_model_vertex_type_t type = static_cast<nanoem_model_vertex_type_t>(i);
        comboBox->addItem(vertexTypeText(type), i);
    }
    comboBox->setCurrentIndex(comboBox->findData(int(currentType)));
    return comboBox;
}

/**
 * @brief One row of the type dependent bone slots: the jump to the bone tab button, the bone to influence
 *        and (except for BDEF1, which has no weight) its weight slider, all sharing the row ModelParameterDialog
 *        lays them out on with SameLine.
 */
static void
appendBoneWeightRow(QVBoxLayout *layout, QWidget *parent, nanoem_model_bone_t *const *bones,
    const nanoem_rsize_t numBones, const nanoem_model_bone_t *currentBonePtr, const nanoem_f32_t weight,
    const bool showWeight, const QString &linkToolTip, const std::function<void(const nanoem_model_bone_t *)> &setBone,
    const std::function<void(nanoem_f32_t)> &setWeight, const std::function<void(const nanoem_model_bone_t *)> &jump)
{
    QList<const nanoem_model_bone_t *> orderedBones;
    orderedBones.append(nullptr);
    auto *comboBox = new QComboBox(parent);
    comboBox->addItem(QString::fromUtf8(model::Bone::nameConstString(nullptr, "(none)")));
    for (nanoem_rsize_t i = 0; i < numBones; i++) {
        const nanoem_model_bone_t *bonePtr = bones[i];
        if (const model::Bone *bone = model::Bone::cast(bonePtr)) {
            comboBox->addItem(QString::fromUtf8(bone->nameConstString()));
            orderedBones.append(bonePtr);
        }
    }
    if (auto *itemModel = qobject_cast<QStandardItemModel *>(comboBox->model())) {
        if (auto *item = itemModel->item(0)) {
            /* ModelParameterDialog only lets the combo pick an existing bone, (none) is the placeholder */
            item->setEnabled(false);
        }
    }
    int currentIndex = 0;
    for (int i = 0; i < orderedBones.size(); i++) {
        if (orderedBones[i] == currentBonePtr) {
            currentIndex = i;
            break;
        }
    }
    comboBox->setCurrentIndex(currentIndex);
    QObject::connect(comboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        [orderedBones, setBone](const int value) {
            if (value >= 0 && value < orderedBones.size()) {
                setBone(orderedBones[value]);
            }
        });
    auto *row = new QHBoxLayout;
    row->addWidget(createLinkButton(
        parent, currentBonePtr != nullptr, linkToolTip, [jump, currentBonePtr]() { jump(currentBonePtr); }));
    row->addWidget(comboBox, showWeight ? 3 : 1);
    if (showWeight) {
        auto *slider = new QSlider(Qt::Horizontal, parent);
        slider->setRange(0, 1000);
        slider->setValue(int(glm::clamp(weight, 0.0f, 1.0f) * 1000.0f));
        auto *valueLabel = new QLabel(QString::number(double(weight), 'f', 3), parent);
        valueLabel->setMinimumWidth(72);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QObject::connect(slider, &QSlider::valueChanged, [valueLabel, setWeight](const int value) {
            const nanoem_f32_t weightValue = nanoem_f32_t(value) / 1000.0f;
            valueLabel->setText(QString::number(double(weightValue), 'f', 3));
            setWeight(weightValue);
        });
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
    }
    layout->addLayout(row);
}

static bool
isVertexSelectionInSync(
    QListWidget *list, nanoem_model_vertex_t *const *vertices, const IModelObjectSelection *selection)
{
    const QList<QListWidgetItem *> selected = list->selectedItems();
    if (nanoem_rsize_t(selected.size()) != selection->countAllVertices()) {
        return false;
    }
    for (QListWidgetItem *item : selected) {
        const int row = list->row(item);
        if (row < 0 || nanoem_rsize_t(row) >= nanoem_rsize_t(list->count()) ||
            !selection->containsVertex(vertices[row])) {
            return false;
        }
    }
    return true;
}

static void
applyVertexSelectionToList(QListWidget *list, nanoem_model_vertex_t *const *vertices, const nanoem_rsize_t numVertices,
    const IModelObjectSelection *selection)
{
    const QSignalBlocker blocker(list);
    list->clearSelection();
    for (nanoem_rsize_t i = 0; i < numVertices; i++) {
        if (selection->containsVertex(vertices[i])) {
            if (QListWidgetItem *item = list->item(int(i))) {
                item->setSelected(true);
            }
        }
    }
}

static bool
isFaceSelectionInSync(QListWidget *list, const nanoem_u32_t *vertexIndices, const nanoem_rsize_t numFaces,
    const IModelObjectSelection *selection)
{
    const QList<QListWidgetItem *> selected = list->selectedItems();
    if (nanoem_rsize_t(selected.size()) != selection->countAllFaces()) {
        return false;
    }
    for (QListWidgetItem *item : selected) {
        const int row = list->row(item);
        if (row < 0 || nanoem_rsize_t(row) >= numFaces) {
            return false;
        }
        const nanoem_rsize_t offset = nanoem_rsize_t(row) * 3;
        const Vector4UI32 face(
            nanoem_u32_t(row), vertexIndices[offset + 0], vertexIndices[offset + 1], vertexIndices[offset + 2]);
        if (!selection->containsFace(face)) {
            return false;
        }
    }
    return true;
}

static void
applyFaceSelectionToList(QListWidget *list, const nanoem_u32_t *vertexIndices, const nanoem_rsize_t numFaces,
    const IModelObjectSelection *selection)
{
    const QSignalBlocker blocker(list);
    list->clearSelection();
    for (nanoem_rsize_t i = 0; i < numFaces; i++) {
        const nanoem_rsize_t offset = i * 3;
        const Vector4UI32 face(
            nanoem_u32_t(i), vertexIndices[offset + 0], vertexIndices[offset + 1], vertexIndices[offset + 2]);
        if (selection->containsFace(face)) {
            if (QListWidgetItem *item = list->item(int(i))) {
                item->setSelected(true);
            }
        }
    }
}

static void
clearDetailPane(QWidget *detailPane, QWidget *&child)
{
    if (child) {
        detailPane->layout()->removeWidget(child);
        /* the widget calling into the rebuild may be inside the pane, so let the loop dispose of it */
        child->hide();
        child->deleteLater();
        child = nullptr;
    }
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
    , m_vertexIndex(0)
    , m_faceIndex(0)
    , m_boneIndex(0)
    , m_materialIndex(0)
    , m_vertexCandidateUVAIndex(0)
    , m_sortedSelectedVertexIndex(0)
    , m_vertexDetailKind(-1)
    , m_faceDetailKind(-1)
    , m_vertexDetailIndex(0)
    , m_vertexDetailCount(0)
    , m_faceDetailIndex(0)
    , m_faceDetailCount(0)
    , m_vertexCountLabel(nullptr)
    , m_vertexList(nullptr)
    , m_vertexDetail(nullptr)
    , m_vertexDetailChild(nullptr)
    , m_faceCountLabel(nullptr)
    , m_faceList(nullptr)
    , m_faceDetail(nullptr)
    , m_faceDetailChild(nullptr)
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
    m_tabs->addTab(createVertexTab(), translate("nanoem.gui.window.model.tab.vertex"));
    m_tabs->addTab(createFaceTab(), translate("nanoem.gui.window.model.tab.face"));
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
    const int activeTab = m_tabs->currentIndex();
    if (activeTab == kTabTypeMeasure) {
        rebuildMeasureCounts();
        updateHeightBasedTransform();
    }
    else if (activeTab == kTabTypeVertex) {
        syncVertexTab();
    }
    else if (activeTab == kTabTypeFace) {
        syncFaceTab();
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
        if (value == kTabTypeVertex) {
            syncVertexTab();
        }
        else if (value == kTabTypeFace) {
            syncFaceTab();
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

QWidget *
QtModelParameterDialog::createVertexTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *leftPane = new QWidget(widget);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_vertexCountLabel = new QLabel(leftPane);
    leftLayout->addWidget(m_vertexCountLabel);
    m_vertexList = new QListWidget(leftPane);
    m_vertexList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftLayout->addWidget(m_vertexList, 1);
    leftPane->setFixedWidth(kObjectListPaneWidth);
    layout->addWidget(leftPane);
    m_vertexDetail = new QWidget;
    auto *detailLayout = new QVBoxLayout(m_vertexDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    /* ModelParameterDialog draws the right pane as a child window, so long property panes scroll */
    auto *detailScrollArea = new QScrollArea(widget);
    detailScrollArea->setWidgetResizable(true);
    detailScrollArea->setFrameShape(QFrame::NoFrame);
    detailScrollArea->setWidget(m_vertexDetail);
    layout->addWidget(detailScrollArea, 1);
    connect(m_vertexList, &QListWidget::itemSelectionChanged, this, &QtModelParameterDialog::vertexSelectionChanged);
    connect(m_vertexList, &QListWidget::itemChanged, this, &QtModelParameterDialog::vertexItemChanged);
    syncVertexTab();
    return widget;
}

void
QtModelParameterDialog::syncVertexTab()
{
    if (!m_activeModel || !m_vertexList) {
        return;
    }
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    IModelObjectSelection *selection = m_activeModel->selection();
    if (numVertices != nanoem_rsize_t(m_vertexList->count())) {
        rebuildVertexList();
    }
    else {
        const QSignalBlocker blocker(m_vertexList);
        for (nanoem_rsize_t i = 0; i < numVertices; i++) {
            QListWidgetItem *item = m_vertexList->item(int(i));
            const model::Vertex *vertex = model::Vertex::cast(vertices[i]);
            if (item && vertex) {
                const Qt::CheckState state = vertex->isEditingMasked() ? Qt::Unchecked : Qt::Checked;
                if (item->checkState() != state) {
                    item->setCheckState(state);
                }
            }
        }
        if (m_vertexIndex < numVertices) {
            QListWidgetItem *item = m_vertexList->item(int(m_vertexIndex));
            if (item) {
                item->setText(vertexText(vertices[m_vertexIndex], m_vertexIndex));
            }
        }
    }
    if (!isVertexSelectionInSync(m_vertexList, vertices, selection)) {
        applyVertexSelectionToList(m_vertexList, vertices, numVertices, selection);
    }
    m_vertexCountLabel->setText(
        QStringLiteral("%1 / %2").arg(formatCount(m_vertexIndex + 1)).arg(formatCount(numVertices)));
    rebuildVertexDetail();
}

void
QtModelParameterDialog::rebuildVertexList()
{
    if (!m_activeModel || !m_vertexList) {
        return;
    }
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    IModelObjectSelection *selection = m_activeModel->selection();
    {
        const QSignalBlocker blocker(m_vertexList);
        m_vertexList->setUpdatesEnabled(false);
        m_vertexList->clear();
        for (nanoem_rsize_t i = 0; i < numVertices; i++) {
            const nanoem_model_vertex_t *vertexPtr = vertices[i];
            const model::Vertex *vertex = model::Vertex::cast(vertexPtr);
            auto *item = new QListWidgetItem(vertexText(vertexPtr, i), m_vertexList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(vertex && vertex->isEditingMasked() ? Qt::Unchecked : Qt::Checked);
        }
        m_vertexList->setUpdatesEnabled(true);
    }
    applyVertexSelectionToList(m_vertexList, vertices, numVertices, selection);
}

void
QtModelParameterDialog::vertexSelectionChanged()
{
    if (!m_activeModel) {
        return;
    }
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    IModelObjectSelection *selection = m_activeModel->selection();
    if (!isVertexSelectionInSync(m_vertexList, vertices, selection)) {
        selection->removeAllVertices();
        const QList<QListWidgetItem *> selected = m_vertexList->selectedItems();
        for (QListWidgetItem *item : selected) {
            const int row = m_vertexList->row(item);
            if (row >= 0 && nanoem_rsize_t(row) < numVertices) {
                selection->addVertex(vertices[row]);
            }
        }
    }
    const int currentRow = m_vertexList->currentRow();
    if (currentRow >= 0) {
        m_vertexIndex = nanoem_rsize_t(currentRow);
    }
    m_vertexCountLabel->setText(
        QStringLiteral("%1 / %2").arg(formatCount(m_vertexIndex + 1)).arg(formatCount(numVertices)));
    rebuildVertexDetail();
}

void
QtModelParameterDialog::vertexItemChanged(QListWidgetItem *item)
{
    if (!m_activeModel || !item) {
        return;
    }
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    const int row = m_vertexList->row(item);
    if (row >= 0 && nanoem_rsize_t(row) < numVertices) {
        if (model::Vertex *vertex = model::Vertex::cast(vertices[row])) {
            vertex->setEditingMasked(item->checkState() != Qt::Checked);
        }
    }
}

void
QtModelParameterDialog::rebuildVertexDetail()
{
    if (!m_activeModel || !m_vertexDetail) {
        return;
    }
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    IModelObjectSelection *selection = m_activeModel->selection();
    const nanoem_rsize_t selectedCount = selection->countAllVertices();
    int kind = -1;
    nanoem_rsize_t index = 0;
    if (selectedCount > 1) {
        updateSortedSelectedVertices(selection);
        kind = 1;
        index = m_sortedSelectedVertices.empty()
            ? 0
            : nanoem_rsize_t(model::Vertex::index(m_sortedSelectedVertices.front()));
    }
    else if (numVertices > 0 && m_vertexIndex < numVertices) {
        kind = 2;
        index = m_vertexIndex;
    }
    if (kind == m_vertexDetailKind && index == m_vertexDetailIndex && selectedCount == m_vertexDetailCount) {
        return;
    }
    m_vertexDetailKind = kind;
    m_vertexDetailIndex = index;
    m_vertexDetailCount = selectedCount;
    clearDetailPane(m_vertexDetail, m_vertexDetailChild);
    if (kind < 0) {
        return;
    }
    auto *child = new QWidget(m_vertexDetail);
    auto *layout = new QVBoxLayout(child);
    layout->setContentsMargins(0, 0, 0, 0);
    if (kind == 1) {
        appendBatchVertexPropertyPane(layout, child);
    }
    else {
        appendVertexPropertyPane(layout, child, vertices[index]);
    }
    m_vertexDetail->layout()->addWidget(child);
    /* the pane is rebuilt while the dialog is up, so the new child has to be shown by hand */
    child->show();
    m_vertexDetailChild = child;
}

void
QtModelParameterDialog::updateSortedSelectedVertices(const IModelObjectSelection *selection)
{
    if (!m_sortedSelectedVertices.empty() && m_sortedSelectedVertices.size() != selection->countAllVertices()) {
        m_sortedSelectedVertices.clear();
        m_sortedSelectedVertexIndex = 0;
    }
    if (m_sortedSelectedVertices.empty()) {
        internal::ModelObjectSelection::sortedVertexList(selection, m_sortedSelectedVertices);
        if (!m_sortedSelectedVertices.empty()) {
            command::BatchChangeAllVertexObjectsCommand::save(m_sortedSelectedVertices.front(), m_batchVertexParameter);
        }
    }
    if (m_sortedSelectedVertexIndex >= m_sortedSelectedVertices.size()) {
        m_sortedSelectedVertexIndex = 0;
    }
}

void
QtModelParameterDialog::appendVertexPropertyPane(QVBoxLayout *layout, QWidget *parent, nanoem_model_vertex_t *vertexPtr)
{
    const Vector3 origin(glm::make_vec3(nanoemModelVertexGetOrigin(vertexPtr)));
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.origin"),
        { origin.x, origin.y, origin.z }, 3, [vertexPtr](const QVector<double> &values) {
            const Vector3 value = Vector3(float(values[0]), float(values[1]), float(values[2]));
            command::ScopedMutableVertex scoped(vertexPtr);
            nanoemMutableModelVertexSetOrigin(scoped, glm::value_ptr(value));
        });
    const Vector3 normal(glm::make_vec3(nanoemModelVertexGetNormal(vertexPtr)));
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.normal"),
        { normal.x, normal.y, normal.z }, 3, [vertexPtr](const QVector<double> &values) {
            const Vector3 value = Vector3(float(values[0]), float(values[1]), float(values[2]));
            command::ScopedMutableVertex scoped(vertexPtr);
            nanoemMutableModelVertexSetNormal(scoped, glm::value_ptr(value));
        });
    const Vector2 texcoord(glm::make_vec2(nanoemModelVertexGetTexCoord(vertexPtr)));
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.texcoord"), { texcoord.x, texcoord.y },
        3, [vertexPtr](const QVector<double> &values) {
            const Vector2 value = Vector2(float(values[0]), float(values[1]));
            command::ScopedMutableVertex scoped(vertexPtr);
            nanoemMutableModelVertexSetTexCoord(scoped, glm::value_ptr(value));
        });
    {
        auto *uvaComboBox = new QComboBox(parent);
        for (nanoem_rsize_t i = 0; i < 4; i++) {
            uvaComboBox->addItem(QStringLiteral("UVA%1").arg(i + 1), int(i));
        }
        uvaComboBox->setCurrentIndex(int(m_vertexCandidateUVAIndex));
        const Vector4 value(glm::make_vec4(nanoemModelVertexGetAdditionalUV(vertexPtr, m_vertexCandidateUVAIndex)));
        appendComponentField(
            parent, layout, QString(), { value.x, value.y, value.z, value.w }, 3,
            [vertexPtr, this](const QVector<double> &values) {
                const Vector4 uv = Vector4(float(values[0]), float(values[1]), float(values[2]), float(values[3]));
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetAdditionalUV(scoped, glm::value_ptr(uv), m_vertexCandidateUVAIndex);
            },
            uvaComboBox, 1);
        connect(uvaComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](const int index) {
                m_vertexCandidateUVAIndex = nanoem_rsize_t(index);
                m_vertexDetailKind = -1;
                rebuildVertexDetail();
            });
    }
    layout->addWidget(createHorizontalSeparator(parent));
    {
        layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.vertex.material"), parent));
        const model::Vertex *vertex = model::Vertex::cast(vertexPtr);
        const nanoem_model_material_t *materialPtr = vertex ? vertex->material() : nullptr;
        auto *row = new QHBoxLayout;
        row->addWidget(createLinkButton(parent, materialPtr != nullptr,
            translate("nanoem.gui.window.model.tab.material"), [this, materialPtr]() { toggleMaterial(materialPtr); }));
        row->addWidget(new QLabel(QString::fromUtf8(model::Material::nameConstString(materialPtr, "(none)")), parent));
        row->addStretch(1);
        layout->addLayout(row);
    }
    {
        const nanoem_f32_t width = nanoemModelVertexGetEdgeSize(vertexPtr);
        appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.edge"), { width }, 3,
            [vertexPtr](const QVector<double> &values) {
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetEdgeSize(scoped, float(values[0]));
            });
    }
    layout->addWidget(createHorizontalSeparator(parent));
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.vertex.type"), parent));
    auto *typeComboBox = createVertexTypeCombo(parent, nanoemModelVertexGetType(vertexPtr));
    layout->addWidget(typeComboBox);
    connect(typeComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
        [this, vertexPtr](const int index) {
            const nanoem_model_vertex_type_t type = static_cast<nanoem_model_vertex_type_t>(index);
            if (type != nanoemModelVertexGetType(vertexPtr)) {
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetType(scoped, type);
                m_vertexDetailKind = -1;
                rebuildVertexDetail();
            }
        });
    nanoem_rsize_t numBones;
    nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(m_activeModel->data(), &numBones);
    const QString boneToolTip = translate("nanoem.gui.window.model.tab.bone");
    switch (nanoemModelVertexGetType(vertexPtr)) {
    case NANOEM_MODEL_VERTEX_TYPE_BDEF1: {
        const nanoem_model_bone_t *bonePtr = nanoemModelVertexGetBoneObject(vertexPtr, 0);
        appendBoneWeightRow(
            layout, parent, bones, numBones, bonePtr, 0.0f, false, boneToolTip,
            [vertexPtr](const nanoem_model_bone_t *bone) {
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetBoneObject(scoped, bone, 0);
            },
            [](const nanoem_f32_t) { }, [this](const nanoem_model_bone_t *bone) { toggleBone(bone); });
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_BDEF2: {
        appendVertexBoneWeightRows(layout, parent, 2, vertexPtr);
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_BDEF4: {
        appendVertexBoneWeightRows(layout, parent, 4, vertexPtr);
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_SDEF: {
        appendVertexBoneWeightRows(layout, parent, 2, vertexPtr);
        const Vector3 r0(glm::make_vec3(nanoemModelVertexGetSdefR0(vertexPtr)));
        appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.sdef.r0"), { r0.x, r0.y, r0.z }, 3,
            [vertexPtr](const QVector<double> &values) {
                const Vector3 value = Vector3(float(values[0]), float(values[1]), float(values[2]));
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetSdefR0(scoped, glm::value_ptr(value));
            });
        const Vector3 r1(glm::make_vec3(nanoemModelVertexGetSdefR1(vertexPtr)));
        appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.sdef.r1"), { r1.x, r1.y, r1.z }, 3,
            [vertexPtr](const QVector<double> &values) {
                const Vector3 value = Vector3(float(values[0]), float(values[1]), float(values[2]));
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetSdefR1(scoped, glm::value_ptr(value));
            });
        const Vector3 c(glm::make_vec3(nanoemModelVertexGetSdefC(vertexPtr)));
        appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.sdef.c"), { c.x, c.y, c.z }, 3,
            [vertexPtr](const QVector<double> &values) {
                const Vector3 value = Vector3(float(values[0]), float(values[1]), float(values[2]));
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetSdefC(scoped, glm::value_ptr(value));
            });
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_QDEF: {
        appendVertexBoneWeightRows(layout, parent, 4, vertexPtr);
        break;
    }
    default:
        break;
    }
    /* without this the rows share out the pane's leftover height instead of stacking at the top */
    layout->addStretch(1);
}

void
QtModelParameterDialog::appendVertexBoneWeightRows(
    QVBoxLayout *layout, QWidget *parent, const nanoem_rsize_t numItems, nanoem_model_vertex_t *vertexPtr)
{
    nanoem_rsize_t numBones;
    nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(m_activeModel->data(), &numBones);
    const QString boneToolTip = translate("nanoem.gui.window.model.tab.bone");
    for (nanoem_rsize_t i = 0; i < numItems; i++) {
        const nanoem_model_bone_t *bonePtr = nanoemModelVertexGetBoneObject(vertexPtr, i);
        const nanoem_f32_t weight = nanoemModelVertexGetBoneWeight(vertexPtr, i);
        appendBoneWeightRow(
            layout, parent, bones, numBones, bonePtr, weight, true, boneToolTip,
            [vertexPtr, i](const nanoem_model_bone_t *bone) {
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetBoneObject(scoped, bone, i);
            },
            [vertexPtr, i](const nanoem_f32_t value) {
                command::ScopedMutableVertex scoped(vertexPtr);
                nanoemMutableModelVertexSetBoneWeight(scoped, value, i);
            },
            [this](const nanoem_model_bone_t *bone) { toggleBone(bone); });
    }
}

void
QtModelParameterDialog::appendBatchVertexBoneWeightRows(
    QVBoxLayout *layout, QWidget *parent, const nanoem_rsize_t numItems)
{
    nanoem_rsize_t numBones;
    nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(m_activeModel->data(), &numBones);
    const QString boneToolTip = translate("nanoem.gui.window.model.tab.bone");
    for (nanoem_rsize_t i = 0; i < numItems; i++) {
        const nanoem_model_bone_t *bonePtr = m_batchVertexParameter.m_bones[i];
        const nanoem_f32_t weight = m_batchVertexParameter.m_weights[i];
        appendBoneWeightRow(
            layout, parent, bones, numBones, bonePtr, weight, true, boneToolTip,
            [this, i](const nanoem_model_bone_t *bone) { m_batchVertexParameter.m_bones[i] = bone; },
            [this, i](const nanoem_f32_t value) { m_batchVertexParameter.m_weights[i] = value; },
            [this](const nanoem_model_bone_t *bone) { toggleBone(bone); });
    }
}

void
QtModelParameterDialog::appendBatchVertexPropertyPane(QVBoxLayout *layout, QWidget *parent)
{
    if (m_sortedSelectedVertices.empty()) {
        return;
    }
    const nanoem_rsize_t count = m_activeModel->selection()->countAllVertices();
    auto *applyButton =
        new QPushButton(QString::asprintf(translate("nanoem.gui.model.edit.batch.apply.vertex").toUtf8().constData(),
                            static_cast<unsigned int>(count)),
            parent);
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        command::BatchChangeAllVertexObjectsCommand::List objects;
        nanoem_rsize_t numVertices;
        nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
        const model::Vertex::Set *vertexSet = m_activeModel->selection()->allVertexSet();
        for (nanoem_rsize_t i = 0; i < numVertices; i++) {
            if (vertexSet->find(vertices[i]) != vertexSet->end()) {
                objects.push_back(vertices[i]);
            }
        }
        m_activeModel->pushUndo(
            command::BatchChangeAllVertexObjectsCommand::create(m_activeModel, objects, m_batchVertexParameter));
        rebuildVertexList();
    });
    layout->addWidget(applyButton);
    layout->addWidget(createHorizontalSeparator(parent));
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.batch.copy.vertex"), parent));
    {
        auto *comboBox = new QComboBox(parent);
        for (model::Vertex::List::const_iterator it = m_sortedSelectedVertices.begin(),
                                                 end = m_sortedSelectedVertices.end();
            it != end; ++it) {
            comboBox->addItem(QStringLiteral("Vertex%1").arg(model::Vertex::index(*it)));
        }
        comboBox->setCurrentIndex(int(m_sortedSelectedVertexIndex));
        connect(comboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](const int index) {
                if (index >= 0 && nanoem_rsize_t(index) < m_sortedSelectedVertices.size()) {
                    command::BatchChangeAllVertexObjectsCommand::save(
                        m_sortedSelectedVertices[index], m_batchVertexParameter);
                    m_sortedSelectedVertexIndex = nanoem_rsize_t(index);
                    m_vertexDetailKind = -1;
                    rebuildVertexDetail();
                }
            });
        layout->addWidget(comboBox);
    }
    layout->addWidget(createHorizontalSeparator(parent));
    const Vector3 &origin = m_batchVertexParameter.m_origin;
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.origin"),
        { origin.x, origin.y, origin.z }, 3, [this](const QVector<double> &values) {
            m_batchVertexParameter.m_origin = Vector3(float(values[0]), float(values[1]), float(values[2]));
        });
    const Vector3 &normal = m_batchVertexParameter.m_normal;
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.normal"),
        { normal.x, normal.y, normal.z }, 3, [this](const QVector<double> &values) {
            m_batchVertexParameter.m_normal = Vector3(float(values[0]), float(values[1]), float(values[2]));
        });
    const Vector2 &texcoord = m_batchVertexParameter.m_texcoord;
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.texcoord"), { texcoord.x, texcoord.y },
        3, [this](const QVector<double> &values) {
            m_batchVertexParameter.m_texcoord = Vector2(float(values[0]), float(values[1]));
        });
    {
        auto *uvaComboBox = new QComboBox(parent);
        for (nanoem_rsize_t i = 0; i < 4; i++) {
            uvaComboBox->addItem(QStringLiteral("UVA%1").arg(i + 1), int(i));
        }
        uvaComboBox->setCurrentIndex(int(m_vertexCandidateUVAIndex));
        const Vector4 &value = m_batchVertexParameter.m_uva[m_vertexCandidateUVAIndex];
        appendComponentField(
            parent, layout, QString(), { value.x, value.y, value.z, value.w }, 3,
            [this](const QVector<double> &values) {
                m_batchVertexParameter.m_uva[m_vertexCandidateUVAIndex] =
                    Vector4(float(values[0]), float(values[1]), float(values[2]), float(values[3]));
            },
            uvaComboBox, 1);
        connect(uvaComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](const int index) {
                m_vertexCandidateUVAIndex = nanoem_rsize_t(index);
                m_vertexDetailKind = -1;
                rebuildVertexDetail();
            });
    }
    layout->addWidget(createHorizontalSeparator(parent));
    appendComponentField(parent, layout, translate("nanoem.gui.model.edit.vertex.edge"),
        { m_batchVertexParameter.m_edgeSize }, 3,
        [this](const QVector<double> &values) { m_batchVertexParameter.m_edgeSize = float(values[0]); });
    layout->addWidget(createHorizontalSeparator(parent));
    layout->addWidget(new QLabel(translate("nanoem.gui.model.edit.vertex.type"), parent));
    auto *typeComboBox = createVertexTypeCombo(parent, m_batchVertexParameter.m_type);
    layout->addWidget(typeComboBox);
    connect(typeComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
        [this](const int index) {
            const nanoem_model_vertex_type_t type = static_cast<nanoem_model_vertex_type_t>(index);
            if (type != m_batchVertexParameter.m_type) {
                m_batchVertexParameter.m_type = type;
                m_vertexDetailKind = -1;
                rebuildVertexDetail();
            }
        });
    switch (m_batchVertexParameter.m_type) {
    case NANOEM_MODEL_VERTEX_TYPE_BDEF1: {
        nanoem_rsize_t numBones;
        nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(m_activeModel->data(), &numBones);
        appendBoneWeightRow(
            layout, parent, bones, numBones, m_batchVertexParameter.m_bones[0], 0.0f, false,
            translate("nanoem.gui.window.model.tab.bone"),
            [this](const nanoem_model_bone_t *bone) { m_batchVertexParameter.m_bones[0] = bone; },
            [](const nanoem_f32_t) { }, [this](const nanoem_model_bone_t *bone) { toggleBone(bone); });
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_BDEF2: {
        appendBatchVertexBoneWeightRows(layout, parent, 2);
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_BDEF4: {
        appendBatchVertexBoneWeightRows(layout, parent, 4);
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_SDEF: {
        appendBatchVertexBoneWeightRows(layout, parent, 2);
        break;
    }
    case NANOEM_MODEL_VERTEX_TYPE_QDEF: {
        appendBatchVertexBoneWeightRows(layout, parent, 4);
        break;
    }
    default:
        break;
    }
    layout->addStretch(1);
}

QWidget *
QtModelParameterDialog::createFaceTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *leftPane = new QWidget(widget);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_faceCountLabel = new QLabel(leftPane);
    leftLayout->addWidget(m_faceCountLabel);
    m_faceList = new QListWidget(leftPane);
    m_faceList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftLayout->addWidget(m_faceList, 1);
    leftPane->setFixedWidth(kObjectListPaneWidth);
    layout->addWidget(leftPane);
    m_faceDetail = new QWidget;
    auto *detailLayout = new QVBoxLayout(m_faceDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    auto *detailScrollArea = new QScrollArea(widget);
    detailScrollArea->setWidgetResizable(true);
    detailScrollArea->setFrameShape(QFrame::NoFrame);
    detailScrollArea->setWidget(m_faceDetail);
    layout->addWidget(detailScrollArea, 1);
    connect(m_faceList, &QListWidget::itemSelectionChanged, this, &QtModelParameterDialog::faceSelectionChanged);
    connect(m_faceList, &QListWidget::itemChanged, this, &QtModelParameterDialog::faceItemChanged);
    syncFaceTab();
    return widget;
}

void
QtModelParameterDialog::syncFaceTab()
{
    if (!m_activeModel || !m_faceList) {
        return;
    }
    nanoem_rsize_t numVertexIndices;
    const nanoem_u32_t *vertexIndices = nanoemModelGetAllVertexIndices(m_activeModel->data(), &numVertexIndices);
    const nanoem_rsize_t numFaces = numVertexIndices / 3;
    IModelObjectSelection *selection = m_activeModel->selection();
    if (numFaces != nanoem_rsize_t(m_faceList->count())) {
        rebuildFaceList();
    }
    else {
        const QSignalBlocker blocker(m_faceList);
        for (nanoem_rsize_t i = 0; i < numFaces; i++) {
            QListWidgetItem *item = m_faceList->item(int(i));
            if (item) {
                const Qt::CheckState state = m_activeModel->isFaceEditingMasked(i) ? Qt::Unchecked : Qt::Checked;
                if (item->checkState() != state) {
                    item->setCheckState(state);
                }
            }
        }
        if (m_faceIndex < numFaces) {
            QListWidgetItem *item = m_faceList->item(int(m_faceIndex));
            if (item) {
                item->setText(faceText(m_faceIndex, vertexIndices, m_faceIndex * 3));
            }
        }
    }
    if (!isFaceSelectionInSync(m_faceList, vertexIndices, numFaces, selection)) {
        applyFaceSelectionToList(m_faceList, vertexIndices, numFaces, selection);
    }
    m_faceCountLabel->setText(QStringLiteral("%1 / %2").arg(formatCount(m_faceIndex + 1)).arg(formatCount(numFaces)));
    rebuildFaceDetail();
}

void
QtModelParameterDialog::rebuildFaceList()
{
    if (!m_activeModel || !m_faceList) {
        return;
    }
    nanoem_rsize_t numVertexIndices;
    const nanoem_u32_t *vertexIndices = nanoemModelGetAllVertexIndices(m_activeModel->data(), &numVertexIndices);
    const nanoem_rsize_t numFaces = numVertexIndices / 3;
    IModelObjectSelection *selection = m_activeModel->selection();
    {
        const QSignalBlocker blocker(m_faceList);
        m_faceList->setUpdatesEnabled(false);
        m_faceList->clear();
        for (nanoem_rsize_t i = 0; i < numFaces; i++) {
            const nanoem_rsize_t offset = i * 3;
            auto *item = new QListWidgetItem(faceText(i, vertexIndices, offset), m_faceList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(m_activeModel->isFaceEditingMasked(i) ? Qt::Unchecked : Qt::Checked);
        }
        m_faceList->setUpdatesEnabled(true);
    }
    applyFaceSelectionToList(m_faceList, vertexIndices, numFaces, selection);
}

void
QtModelParameterDialog::faceSelectionChanged()
{
    if (!m_activeModel) {
        return;
    }
    nanoem_rsize_t numVertexIndices;
    const nanoem_u32_t *vertexIndices = nanoemModelGetAllVertexIndices(m_activeModel->data(), &numVertexIndices);
    const nanoem_rsize_t numFaces = numVertexIndices / 3;
    IModelObjectSelection *selection = m_activeModel->selection();
    if (!isFaceSelectionInSync(m_faceList, vertexIndices, numFaces, selection)) {
        selection->removeAllFaces();
        const QList<QListWidgetItem *> selected = m_faceList->selectedItems();
        for (QListWidgetItem *item : selected) {
            const int row = m_faceList->row(item);
            if (row >= 0 && nanoem_rsize_t(row) < numFaces) {
                const nanoem_rsize_t offset = nanoem_rsize_t(row) * 3;
                selection->addFace(Vector4UI32(nanoem_u32_t(row), vertexIndices[offset + 0], vertexIndices[offset + 1],
                    vertexIndices[offset + 2]));
            }
        }
    }
    const int currentRow = m_faceList->currentRow();
    if (currentRow >= 0) {
        m_faceIndex = nanoem_rsize_t(currentRow);
    }
    m_faceCountLabel->setText(QStringLiteral("%1 / %2").arg(formatCount(m_faceIndex + 1)).arg(formatCount(numFaces)));
    rebuildFaceDetail();
}

void
QtModelParameterDialog::faceItemChanged(QListWidgetItem *item)
{
    if (!m_activeModel || !item) {
        return;
    }
    nanoem_rsize_t numVertexIndices;
    const nanoem_u32_t *vertexIndices = nanoemModelGetAllVertexIndices(m_activeModel->data(), &numVertexIndices);
    const nanoem_rsize_t numFaces = numVertexIndices / 3;
    const int row = m_faceList->row(item);
    if (row >= 0 && nanoem_rsize_t(row) < numFaces) {
        m_activeModel->setFaceEditingMasked(nanoem_rsize_t(row), item->checkState() != Qt::Checked);
    }
}

void
QtModelParameterDialog::rebuildFaceDetail()
{
    if (!m_activeModel || !m_faceDetail) {
        return;
    }
    nanoem_rsize_t numVertexIndices;
    const nanoem_u32_t *vertexIndices = nanoemModelGetAllVertexIndices(m_activeModel->data(), &numVertexIndices);
    const nanoem_rsize_t numFaces = numVertexIndices / 3;
    IModelObjectSelection *selection = m_activeModel->selection();
    const nanoem_rsize_t selectedCount = selection->countAllFaces();
    int kind = -1;
    nanoem_rsize_t index = 0;
    if (selectedCount > 1) {
        /* ModelParameterDialog::layoutBatchFaceChangePane is empty, so the batch pane has no fields yet */
        kind = 1;
    }
    else if (numFaces > 0 && m_faceIndex < numFaces) {
        kind = 2;
        index = m_faceIndex;
    }
    if (kind == m_faceDetailKind && index == m_faceDetailIndex && selectedCount == m_faceDetailCount) {
        return;
    }
    m_faceDetailKind = kind;
    m_faceDetailIndex = index;
    m_faceDetailCount = selectedCount;
    clearDetailPane(m_faceDetail, m_faceDetailChild);
    if (kind < 0) {
        return;
    }
    auto *child = new QWidget(m_faceDetail);
    auto *layout = new QVBoxLayout(child);
    layout->setContentsMargins(0, 0, 0, 0);
    if (kind == 2) {
        const nanoem_rsize_t offset = index * 3;
        const Vector3UI32 face(vertexIndices[offset + 0], vertexIndices[offset + 1], vertexIndices[offset + 2]);
        appendFacePropertyPane(layout, child, face);
    }
    m_faceDetail->layout()->addWidget(child);
    child->show();
    m_faceDetailChild = child;
}

void
QtModelParameterDialog::appendFacePropertyPane(QVBoxLayout *layout, QWidget *parent, const Vector3UI32 &face)
{
    nanoem_rsize_t numVertices;
    nanoem_model_vertex_t *const *vertices = nanoemModelGetAllVertexObjects(m_activeModel->data(), &numVertices);
    const QString vertexToolTip = translate("nanoem.gui.window.model.tab.vertex");
    const QString materialToolTip = translate("nanoem.gui.window.model.tab.material");
    for (int i = 0; i < 3; i++) {
        const nanoem_u32_t vertexIndex = face[i];
        if (nanoem_rsize_t(vertexIndex) >= numVertices) {
            continue;
        }
        const nanoem_model_vertex_t *vertexPtr = vertices[vertexIndex];
        auto *row = new QHBoxLayout;
        row->addWidget(createLinkButton(
            parent, vertexPtr != nullptr, vertexToolTip, [this, vertexPtr]() { toggleVertex(vertexPtr); }));
        row->addWidget(new QLabel(QStringLiteral("Vertex%1").arg(model::Vertex::index(vertexPtr)), parent));
        if (const model::Vertex *vertex = model::Vertex::cast(vertexPtr)) {
            const nanoem_model_material_t *materialPtr = vertex->material();
            if (const model::Material *material = model::Material::cast(materialPtr)) {
                row->addWidget(createLinkButton(parent, materialPtr != nullptr, materialToolTip,
                    [this, materialPtr]() { toggleMaterial(materialPtr); }));
                row->addWidget(new QLabel(QString::fromUtf8(material->nameConstString()), parent));
            }
        }
        row->addStretch(1);
        layout->addLayout(row);
    }
    layout->addStretch(1);
}

void
QtModelParameterDialog::toggleVertex(const nanoem_model_vertex_t *vertexPtr)
{
    m_vertexIndex = nanoem_rsize_t(model::Vertex::index(vertexPtr));
    m_tabs->setCurrentIndex(kTabTypeVertex);
}

void
QtModelParameterDialog::toggleBone(const nanoem_model_bone_t *bonePtr)
{
    m_boneIndex = nanoem_rsize_t(model::Bone::index(bonePtr));
    m_tabs->setCurrentIndex(kTabTypeBone);
}

void
QtModelParameterDialog::toggleMaterial(const nanoem_model_material_t *materialPtr)
{
    m_materialIndex = nanoem_rsize_t(model::Material::index(materialPtr));
    m_tabs->setCurrentIndex(kTabTypeMaterial);
}

} /* namespace qt */
} /* namespace nanoem */
