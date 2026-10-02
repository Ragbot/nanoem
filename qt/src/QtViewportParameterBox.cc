/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewportParameterBox.h"
#include "QtApplicationService.h"

#include "emapp/ICamera.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/IVectorValueState.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/internal/BoneValueState.h"
#include "emapp/internal/CameraValueState.h"
#include "emapp/private/CommonInclude.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include <QApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>

namespace nanoem {
namespace qt {
namespace {

const int kRefreshIntervalMillis = 50;
/* matches the ImGui drag ranges: kTranslationStepFactor speed, %.2f/%.1f formats */
const double kUnboundedRange = 10000000.0;

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

/* the spin boxes keep focus in their line edit while the user edits; ancestorOf also covers the
   arrow clicks that never focus the line edit itself */
static bool
isEditing(const QWidget *spinBox)
{
    QWidget *focus = QApplication::focusWidget();
    return spinBox->hasFocus() || (focus != nullptr && spinBox->isAncestorOf(focus));
}

static void
commitState(IVectorValueState **state)
{
    if (*state) {
        (*state)->commit();
        nanoem_delete_safe(*state);
    }
}

/* an unfocused arrow click is a single value change: commit it right away so it stays undoable
   instead of waiting for an editingFinished that a plain click never emits */
static void
maybeCommit(const QWidget *spinBox, IVectorValueState **state)
{
    if (!isEditing(spinBox)) {
        commitState(state);
    }
}

static bool
isCameraStateValid(const IVectorValueState *state, const ICamera *camera, const Project *project)
{
    const internal::BaseCameraVectorValueState *base = static_cast<const internal::BaseCameraVectorValueState *>(state);
    return base->camera() == camera && base->project() == project;
}

} /* namespace anonymous */

QtViewportParameterBox::QtViewportParameterBox(QtApplicationService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_boneTranslationState(nullptr)
    , m_boneOrientationState(nullptr)
    , m_cameraLookAtState(nullptr)
    , m_cameraAngleState(nullptr)
    , m_cameraDistanceState(nullptr)
{
    setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);
    /* model row: mirrors the activeModel branch of drawViewportParameterBox */
    m_modelRow = new QWidget(this);
    auto *modelLayout = new QHBoxLayout(m_modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->setSpacing(4);
    m_translationLabel = new QLabel(translate(service, "nanoem.gui.viewport.parameter.model.translation"), m_modelRow);
    modelLayout->addWidget(m_translationLabel);
    for (int i = 0; i < 3; i++) {
        m_translationSpinBox[i] = createSpinBox(-kUnboundedRange, kUnboundedRange, 2, 0.1);
        modelLayout->addWidget(m_translationSpinBox[i]);
    }
    m_orientationLabel = new QLabel(translate(service, "nanoem.gui.viewport.parameter.model.orientation"), m_modelRow);
    modelLayout->addWidget(m_orientationLabel);
    for (int i = 0; i < 3; i++) {
        m_orientationSpinBox[i] = createSpinBox(-180.0, 180.0, 1, 1.0);
        modelLayout->addWidget(m_orientationSpinBox[i]);
    }
    modelLayout->addStretch(1);
    /* camera row: mirrors the else branch of drawViewportParameterBox */
    m_cameraRow = new QWidget(this);
    auto *cameraLayout = new QHBoxLayout(m_cameraRow);
    cameraLayout->setContentsMargins(0, 0, 0, 0);
    cameraLayout->setSpacing(4);
    m_lookAtLabel = new QLabel(translate(service, "nanoem.gui.viewport.parameter.camera.look-at"), m_cameraRow);
    cameraLayout->addWidget(m_lookAtLabel);
    for (int i = 0; i < 3; i++) {
        m_lookAtSpinBox[i] = createSpinBox(-kUnboundedRange, kUnboundedRange, 2, 0.1);
        cameraLayout->addWidget(m_lookAtSpinBox[i]);
    }
    m_angleLabel = new QLabel(translate(service, "nanoem.gui.viewport.parameter.camera.angle"), m_cameraRow);
    cameraLayout->addWidget(m_angleLabel);
    for (int i = 0; i < 3; i++) {
        m_angleSpinBox[i] = createSpinBox(-180.0, 180.0, 1, 1.0);
        cameraLayout->addWidget(m_angleSpinBox[i]);
    }
    m_distanceLabel = new QLabel(translate(service, "nanoem.gui.viewport.parameter.camera.distance"), m_cameraRow);
    cameraLayout->addWidget(m_distanceLabel);
    m_distanceSpinBox = createSpinBox(1.0, 100000.0, 1, 1.0);
    cameraLayout->addWidget(m_distanceSpinBox);
    cameraLayout->addStretch(1);
    layout->addWidget(m_modelRow);
    layout->addWidget(m_cameraRow);
    m_modelRow->hide();
    m_cameraRow->hide();
    for (int i = 0; i < 3; i++) {
        QDoubleSpinBox *translation = m_translationSpinBox[i];
        connect(translation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, translation](double) {
            const nanoem_f32_t value[3] = { nanoem_f32_t(m_translationSpinBox[0]->value()),
                nanoem_f32_t(m_translationSpinBox[1]->value()), nanoem_f32_t(m_translationSpinBox[2]->value()) };
            applyBoneTranslation(value);
            maybeCommit(translation, &m_boneTranslationState);
        });
        QDoubleSpinBox *orientation = m_orientationSpinBox[i];
        connect(orientation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, orientation](double) {
                const Vector3 degrees(nanoem_f32_t(m_orientationSpinBox[0]->value()),
                    nanoem_f32_t(m_orientationSpinBox[1]->value()), nanoem_f32_t(m_orientationSpinBox[2]->value()));
                const Vector3 radians(glm::radians(degrees));
                applyBoneOrientation(glm::value_ptr(radians));
                maybeCommit(orientation, &m_boneOrientationState);
            });
        QDoubleSpinBox *lookAt = m_lookAtSpinBox[i];
        connect(lookAt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lookAt](double) {
            const nanoem_f32_t value[3] = { nanoem_f32_t(m_lookAtSpinBox[0]->value()),
                nanoem_f32_t(m_lookAtSpinBox[1]->value()), nanoem_f32_t(m_lookAtSpinBox[2]->value()) };
            applyCameraLookAt(value);
            maybeCommit(lookAt, &m_cameraLookAtState);
        });
        QDoubleSpinBox *angle = m_angleSpinBox[i];
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, angle](double) {
            const Vector3 degrees(nanoem_f32_t(m_angleSpinBox[0]->value()), nanoem_f32_t(m_angleSpinBox[1]->value()),
                nanoem_f32_t(m_angleSpinBox[2]->value()));
            const Vector3 radians(glm::radians(degrees));
            applyCameraAngle(glm::value_ptr(radians));
            maybeCommit(angle, &m_cameraAngleState);
        });
    }
    connect(m_distanceSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        const nanoem_f32_t v = nanoem_f32_t(value);
        applyCameraDistance(&v);
        maybeCommit(m_distanceSpinBox, &m_cameraDistanceState);
    });
    /* every spin box ends its editing session the same way ImGui's handleVectorValueState did on
       IsItemDeactivatedAfterEdit: collapse the pending preview into one undo entry */
    QDoubleSpinBox *allSpinBoxes[] = { m_translationSpinBox[0], m_translationSpinBox[1], m_translationSpinBox[2],
        m_orientationSpinBox[0], m_orientationSpinBox[1], m_orientationSpinBox[2], m_lookAtSpinBox[0],
        m_lookAtSpinBox[1], m_lookAtSpinBox[2], m_angleSpinBox[0], m_angleSpinBox[1], m_angleSpinBox[2],
        m_distanceSpinBox };
    for (QDoubleSpinBox *spinBox : allSpinBoxes) {
        connect(spinBox, &QDoubleSpinBox::editingFinished, this, [this]() { commitPendingStates(); });
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(kRefreshIntervalMillis);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
    /* derive the height from both rows: at construction there may be no project yet and the rows
       are hidden, so the widget's own sizeHint would measure an empty layout */
    const int rowHeight = qMax(m_modelRow->sizeHint().height(), m_cameraRow->sizeHint().height());
    const int verticalMargins = layout->contentsMargins().top() + layout->contentsMargins().bottom();
    setFixedHeight(qMax(rowHeight + verticalMargins, 24));
}

QtViewportParameterBox::~QtViewportParameterBox()
{
    /* like ~ImGuiWindow: drop previews without committing them */
    deletePendingStates();
}

Project *
QtViewportParameterBox::currentProject() const
{
    return m_service->projectHolder()->currentProject();
}

QDoubleSpinBox *
QtViewportParameterBox::createSpinBox(double minimum, double maximum, int decimals, double singleStep)
{
    auto *spinBox = new QDoubleSpinBox(this);
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(decimals);
    spinBox->setSingleStep(singleStep);
    return spinBox;
}

void
QtViewportParameterBox::applyBoneTranslation(const nanoem_f32_t *value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    Model *model = project->activeModel();
    const nanoem_model_bone_t *bonePtr = model ? model->activeBone() : nullptr;
    if (m_boneTranslationState) {
        m_boneTranslationState->setValue(value);
    }
    else if (bonePtr && model && !project->isPlaying()) {
        m_boneTranslationState = nanoem_new(internal::BoneTranslationValueState(bonePtr, model, project));
        m_boneTranslationState->setValue(value);
    }
}

void
QtViewportParameterBox::applyBoneOrientation(const nanoem_f32_t *value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    Model *model = project->activeModel();
    const nanoem_model_bone_t *bonePtr = model ? model->activeBone() : nullptr;
    if (m_boneOrientationState) {
        m_boneOrientationState->setValue(glm::value_ptr(Quaternion(glm::make_vec3(value))));
    }
    else if (bonePtr && model && !project->isPlaying()) {
        m_boneOrientationState = nanoem_new(internal::BoneOrientationValueState(bonePtr, model, project));
        m_boneOrientationState->setValue(glm::value_ptr(Quaternion(glm::make_vec3(value))));
    }
}

void
QtViewportParameterBox::applyCameraLookAt(const nanoem_f32_t *value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    ICamera *camera = project->activeCamera();
    if (m_cameraLookAtState) {
        if (isCameraStateValid(m_cameraLookAtState, camera, project)) {
            m_cameraLookAtState->setValue(value);
        }
        else {
            nanoem_delete_safe(m_cameraLookAtState);
        }
    }
    else if (camera && !project->isPlaying()) {
        m_cameraLookAtState = nanoem_new(internal::CameraLookAtVectorValueState(project, camera));
        m_cameraLookAtState->setValue(value);
    }
}

void
QtViewportParameterBox::applyCameraAngle(const nanoem_f32_t *value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    ICamera *camera = project->activeCamera();
    if (m_cameraAngleState) {
        if (isCameraStateValid(m_cameraAngleState, camera, project)) {
            m_cameraAngleState->setValue(value);
        }
        else {
            nanoem_delete_safe(m_cameraAngleState);
        }
    }
    else if (camera && !project->isPlaying()) {
        m_cameraAngleState = nanoem_new(internal::CameraAngleVectorValueState(project, camera));
        m_cameraAngleState->setValue(value);
    }
}

void
QtViewportParameterBox::applyCameraDistance(const nanoem_f32_t *value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    ICamera *camera = project->activeCamera();
    if (m_cameraDistanceState) {
        if (isCameraStateValid(m_cameraDistanceState, camera, project)) {
            m_cameraDistanceState->setValue(value);
        }
        else {
            nanoem_delete_safe(m_cameraDistanceState);
        }
    }
    else if (camera && !project->isPlaying()) {
        m_cameraDistanceState = nanoem_new(internal::CameraDistanceVectorValueState(project, camera));
        m_cameraDistanceState->setValue(value);
    }
}

void
QtViewportParameterBox::commitPendingStates()
{
    commitState(&m_boneTranslationState);
    commitState(&m_boneOrientationState);
    commitState(&m_cameraLookAtState);
    commitState(&m_cameraAngleState);
    commitState(&m_cameraDistanceState);
}

void
QtViewportParameterBox::deletePendingStates()
{
    nanoem_delete_safe(m_boneTranslationState);
    nanoem_delete_safe(m_boneOrientationState);
    nanoem_delete_safe(m_cameraLookAtState);
    nanoem_delete_safe(m_cameraAngleState);
    nanoem_delete_safe(m_cameraDistanceState);
}

void
QtViewportParameterBox::refresh()
{
    Project *project = currentProject();
    if (!project || project->isModelEditingEnabled()) {
        m_modelRow->hide();
        m_cameraRow->hide();
        return;
    }
    const bool playing = project->isPlaying();
    if (Model *model = project->activeModel()) {
        m_cameraRow->hide();
        m_modelRow->show();
        const nanoem_model_bone_t *bonePtr = model->activeBone();
        const model::Bone *bone = model::Bone::cast(bonePtr);
        const Vector3 translation = bone ? bone->localUserTranslation() : Vector3(0);
        const Vector3 orientation =
            bone ? glm::degrees(glm::eulerAngles(bone->localUserOrientation())) : Vector3(0);
        const bool movable = !playing && model::Bone::isMovable(bonePtr),
                   rotateable = !playing && model::Bone::isRotateable(bonePtr);
        for (int i = 0; i < 3; i++) {
            QDoubleSpinBox *translationSpinBox = m_translationSpinBox[i];
            if (!isEditing(translationSpinBox)) {
                const QSignalBlocker blocker(translationSpinBox);
                translationSpinBox->setValue(translation[i]);
            }
            translationSpinBox->setEnabled(movable);
            QDoubleSpinBox *orientationSpinBox = m_orientationSpinBox[i];
            if (!isEditing(orientationSpinBox)) {
                const QSignalBlocker blocker(orientationSpinBox);
                orientationSpinBox->setValue(orientation[i]);
            }
            orientationSpinBox->setEnabled(rotateable);
        }
    }
    else {
        m_modelRow->hide();
        m_cameraRow->show();
        if (ICamera *camera = project->activeCamera()) {
            const Vector3 lookAt(camera->lookAt()), angle(glm::degrees(camera->angle()));
            for (int i = 0; i < 3; i++) {
                QDoubleSpinBox *lookAtSpinBox = m_lookAtSpinBox[i];
                if (!isEditing(lookAtSpinBox)) {
                    const QSignalBlocker blocker(lookAtSpinBox);
                    lookAtSpinBox->setValue(lookAt[i]);
                }
                lookAtSpinBox->setEnabled(!playing);
                QDoubleSpinBox *angleSpinBox = m_angleSpinBox[i];
                if (!isEditing(angleSpinBox)) {
                    const QSignalBlocker blocker(angleSpinBox);
                    angleSpinBox->setValue(angle[i]);
                }
                angleSpinBox->setEnabled(!playing);
            }
            if (!isEditing(m_distanceSpinBox)) {
                const QSignalBlocker blocker(m_distanceSpinBox);
                m_distanceSpinBox->setValue(camera->distance());
            }
            m_distanceSpinBox->setEnabled(!playing);
        }
    }
}

} /* namespace qt */
} /* namespace nanoem */
