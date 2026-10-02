/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtInterpolationPanel.h"
#include "QtApplicationService.h"
#include "QtInterpolationCurveWidget.h"

#include "emapp/Error.h"
#include "emapp/ICamera.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IMotionKeyframeSelection.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"
#include "emapp/StringUtils.h"
#include "emapp/model/Bone.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

/* mirrors ImGuiWindow::selectBoneInterpolationType */
static QString
boneInterpolationTypeLabel(QtApplicationService *service, int type)
{
    switch (type) {
    case NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_TRANSLATION_X:
        return translate(service, "nanoem.gui.panel.interpolation.bone.translation.x");
    case NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_TRANSLATION_Y:
        return translate(service, "nanoem.gui.panel.interpolation.bone.translation.y");
    case NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_TRANSLATION_Z:
        return translate(service, "nanoem.gui.panel.interpolation.bone.translation.z");
    case NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_ORIENTATION:
        return translate(service, "nanoem.gui.panel.interpolation.bone.orientation");
    default:
        return QStringLiteral("(Unknown)");
    }
}

/* mirrors ImGuiWindow::selectCameraInterpolationType */
static QString
cameraInterpolationTypeLabel(QtApplicationService *service, int type)
{
    switch (type) {
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_LOOKAT_X:
        return translate(service, "nanoem.gui.panel.interpolation.camera.translation.x");
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_LOOKAT_Y:
        return translate(service, "nanoem.gui.panel.interpolation.camera.translation.y");
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_LOOKAT_Z:
        return translate(service, "nanoem.gui.panel.interpolation.camera.translation.z");
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_ANGLE:
        return translate(service, "nanoem.gui.panel.interpolation.camera.orientation");
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_FOV:
        return translate(service, "nanoem.gui.panel.interpolation.camera.fov");
    case NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_DISTANCE:
        return translate(service, "nanoem.gui.panel.interpolation.camera.distance");
    default:
        return QStringLiteral("(Unknown)");
    }
}

} /* namespace anonymous */

QtInterpolationPanel::QtInterpolationPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.interpolation.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    auto *topRow = new QHBoxLayout;
    m_graphButton = new QPushButton(QString(QChar(0x2699)), this);
    m_graphButton->setFixedWidth(30);
    m_typeComboBox = new QComboBox(this);
    topRow->addWidget(m_graphButton);
    topRow->addWidget(m_typeComboBox, 1);
    layout->addLayout(topRow);
    m_curveWidget = new QtInterpolationCurveWidget(this);
    m_curveWidget->setControlPointCallback(
        [this](const Vector4U8 &value) { handleCurveEdited(value); });
    auto *curveRow = new QHBoxLayout;
    curveRow->addWidget(m_curveWidget, 1);
    layout->addLayout(curveRow);
    m_copyButton = new QPushButton(translate(m_service, "nanoem.gui.panel.interpolation.copy"), this);
    m_pasteButton = new QPushButton(translate(m_service, "nanoem.gui.panel.interpolation.paste"), this);
    auto *copyRow = new QHBoxLayout;
    copyRow->addWidget(m_copyButton, 1);
    copyRow->addWidget(m_pasteButton, 1);
    layout->addLayout(copyRow);
    m_linearButton = new QPushButton(translate(m_service, "nanoem.gui.panel.interpolation.linear"), this);
    m_automaticCheckBox = new QCheckBox(
        translate(m_service, "nanoem.gui.panel.interpolation.automatic"), this);
    auto *linearRow = new QHBoxLayout;
    linearRow->addWidget(m_linearButton, 1);
    linearRow->addWidget(m_automaticCheckBox, 1);
    layout->addLayout(linearRow);
    connect(m_typeComboBox, &QComboBox::activated, this, &QtInterpolationPanel::selectType);
    connect(m_graphButton, &QPushButton::clicked, this, &QtInterpolationPanel::openGraphDialog);
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            project->copyAllSelectedKeyframeInterpolations();
        }
    });
    connect(m_pasteButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying() && project->hasSelectedKeyframeInterpolations()) {
            project->pasteAllSelectedKeyframeInterpolations(project->currentLocalFrameIndex());
        }
    });
    connect(m_linearButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying() && project->hasSelectedKeyframeInterpolations()) {
            Error error;
            project->makeAllSelectedKeyframeInterpolationsLinear(error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_automaticCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project) {
            project->setBezierCurveAdjustmentEnabled(checked);
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtInterpolationPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

void
QtInterpolationPanel::refreshBone()
{
    m_bone = nullptr;
    Project *project = currentProject();
    Model *model = project ? project->activeModel() : nullptr;
    if (model) {
        if (Motion *motion = project->resolveMotion(model)) {
            Motion::BoneKeyframeList keyframes;
            motion->selection()->getAll(keyframes, nullptr);
            if (keyframes.size() > 0) {
                const nanoem_unicode_string_t *namePtr = nanoemMotionBoneKeyframeGetName(keyframes[0]);
                String name;
                StringUtils::getUtf8String(namePtr, project->unicodeStringFactory(), name);
                m_bone = model::Bone::cast(model->findBone(name));
            }
        }
    }
}

void
QtInterpolationPanel::selectType(int index)
{
    Project *project = currentProject();
    if (project) {
        const int type = NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_FIRST_ENUM + index;
        if (m_boneVariant) {
            project->setBoneKeyframeInterpolationType(
                static_cast<nanoem_motion_bone_keyframe_interpolation_type_t>(type));
        }
        else {
            const int cameraType = NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_FIRST_ENUM + index;
            project->setCameraKeyframeInterpolationType(
                static_cast<nanoem_motion_camera_keyframe_interpolation_type_t>(cameraType));
        }
        refresh();
    }
}

void
QtInterpolationPanel::handleCurveEdited(const Vector4U8 &value)
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    if (m_boneVariant) {
        if (m_bone) {
            const nanoem_motion_bone_keyframe_interpolation_type_t type =
                static_cast<nanoem_motion_bone_keyframe_interpolation_type_t>(m_currentType);
            m_bone->setBezierControlPoints(type, value);
        }
    }
    else if (!project->isPlaying()) {
        ICamera *camera = project->globalCamera();
        const nanoem_motion_camera_keyframe_interpolation_type_t type =
            static_cast<nanoem_motion_camera_keyframe_interpolation_type_t>(m_currentType);
        camera->setBezierControlPoints(type, value);
    }
}

void
QtInterpolationPanel::openGraphDialog()
{
    Project *project = currentProject();
    if (project) {
        if (m_boneVariant) {
            if (m_bone) {
                openCurveGraphDialog(project);
            }
        }
        else {
            openCurveGraphDialog(project);
        }
    }
}

void
QtInterpolationPanel::openCurveGraphDialog(Project *project)
{
    QPointer<QtInterpolationCurveGraphDialog> &dialog =
        m_boneVariant ? m_boneGraphDialog : m_cameraGraphDialog;
    if (dialog && dialog->isVisible()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    dialog = new QtInterpolationCurveGraphDialog(m_service, project, m_boneVariant, window());
    dialog->show();
}

void
QtInterpolationPanel::refresh()
{
    Project *project = currentProject();
    const bool visible = project != nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project) {
        m_bone = nullptr;
        return;
    }
    const bool boneVariant = project->activeModel() != nullptr;
    m_boneVariant = boneVariant;
    if (boneVariant) {
        refreshBone();
    }
    else {
        m_bone = nullptr;
    }
    const bool playing = project->isPlaying();
    int firstEnum = 0;
    int count = 0;
    int type = 0;
    if (boneVariant) {
        firstEnum = NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_FIRST_ENUM;
        count = NANOEM_MOTION_BONE_KEYFRAME_INTERPOLATION_TYPE_MAX_ENUM - firstEnum;
        type = static_cast<int>(project->boneKeyframeInterpolationType());
    }
    else {
        firstEnum = NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_FIRST_ENUM;
        count = NANOEM_MOTION_CAMERA_KEYFRAME_INTERPOLATION_TYPE_MAX_ENUM - firstEnum;
        type = static_cast<int>(project->cameraKeyframeInterpolationType());
    }
    m_currentType = type;
    if (m_comboCount != count || m_comboVariant != boneVariant) {
        m_typeComboBox->clear();
        for (int i = 0; i < count; i++) {
            const int item = firstEnum + i;
            m_typeComboBox->addItem(boneVariant ? boneInterpolationTypeLabel(m_service, item)
                                                : cameraInterpolationTypeLabel(m_service, item));
        }
        m_comboCount = count;
        m_comboVariant = boneVariant;
    }
    const int comboIndex = type - firstEnum;
    if (m_typeComboBox->currentIndex() != comboIndex) {
        m_typeComboBox->blockSignals(true);
        m_typeComboBox->setCurrentIndex(comboIndex);
        m_typeComboBox->blockSignals(false);
    }
    Vector4U8 controlPoint(20, 20, 107, 107);
    if (boneVariant) {
        if (m_bone) {
            controlPoint = m_bone->bezierControlPoints(
                static_cast<nanoem_motion_bone_keyframe_interpolation_type_t>(type));
        }
    }
    else {
        controlPoint = project->globalCamera()->bezierControlPoints(
            static_cast<nanoem_motion_camera_keyframe_interpolation_type_t>(type));
    }
    const bool curveEnabled = boneVariant ? m_bone != nullptr : !playing;
    m_curveWidget->setControlPoint(controlPoint);
    m_curveWidget->setEnabled(curveEnabled);
    m_graphButton->setEnabled(boneVariant ? m_bone != nullptr : true);
    m_copyButton->setEnabled(!playing);
    const bool hasSelection = project->hasSelectedKeyframeInterpolations();
    m_pasteButton->setEnabled(!playing && hasSelection);
    m_linearButton->setEnabled(!playing && hasSelection);
    m_automaticCheckBox->blockSignals(true);
    m_automaticCheckBox->setChecked(project->isBezierCurveAjustmentEnabled());
    m_automaticCheckBox->blockSignals(false);
}

} /* namespace qt */
} /* namespace nanoem */
