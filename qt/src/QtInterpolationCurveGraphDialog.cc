/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtInterpolationCurveGraphDialog.h"
#include "QtApplicationService.h"
#include "QtInterpolationCurveWidget.h"

#include "emapp/BezierCurve.h"
#include "emapp/ICamera.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"
#include "emapp/model/Bone.h"

#include <QDialogButtonBox>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

const Vector4U8 kDefaultBezierControlPoint(20, 20, 107, 107);

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

static model::Bone *
activeBone(Project *project)
{
    Model *model = project->activeModel();
    return model ? model::Bone::cast(model->activeBone()) : nullptr;
}

} /* namespace anonymous */

QtInterpolationCurveGraphDialog::QtInterpolationCurveGraphDialog(
    QtApplicationService *service, Project *project, bool boneVariant, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_project(project)
    , m_snapshotControlPoint(kDefaultBezierControlPoint)
    , m_snapshotType(0)
    , m_boneVariant(boneVariant)
{
    setObjectName(QStringLiteral("interpolationCurveGraphDialog"));
    setWindowTitle(translate(service, "nanoem.gui.window.interpolation.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    if (boneVariant) {
        m_snapshotType = static_cast<int>(project->boneKeyframeInterpolationType());
        if (const model::Bone *bone = activeBone(project)) {
            m_snapshotControlPoint = bone->bezierControlPoints(project->boneKeyframeInterpolationType());
        }
    }
    else {
        m_snapshotType = static_cast<int>(project->cameraKeyframeInterpolationType());
        m_snapshotControlPoint = project->globalCamera()->bezierControlPoints(
            project->cameraKeyframeInterpolationType());
    }
    auto *layout = new QVBoxLayout(this);
    m_curveWidget = new QtInterpolationCurveWidget(this);
    m_curveWidget->setFixedSize(256, 256);
    m_curveWidget->setControlPointCallback(
        [this](const Vector4U8 &value) { handleCurveEdited(value); });
    layout->addWidget(m_curveWidget);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted) {
            restoreSnapshot();
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

void
QtInterpolationCurveGraphDialog::refresh()
{
    Project *project = m_service->projectHolder()->currentProject();
    if (project != m_project) {
        close();
        return;
    }
    const bool playing = project->isPlaying();
    Vector4U8 controlPoint(kDefaultBezierControlPoint);
    bool editable = false;
    if (m_boneVariant) {
        if (!project->activeModel()) {
            /* the ImGui dialog closes itself when the active model goes away */
            close();
            return;
        }
        if (const model::Bone *bone = activeBone(project)) {
            controlPoint = bone->bezierControlPoints(project->boneKeyframeInterpolationType());
            editable = !playing;
        }
    }
    else {
        controlPoint = project->globalCamera()->bezierControlPoints(project->cameraKeyframeInterpolationType());
        editable = !playing;
    }
    m_curveWidget->setControlPoint(controlPoint);
    m_curveWidget->setEnabled(editable);
    updatePlayhead(project, controlPoint);
}

void
QtInterpolationCurveGraphDialog::handleCurveEdited(const Vector4U8 &value)
{
    Project *project = m_service->projectHolder()->currentProject();
    if (project != m_project || project->isPlaying()) {
        return;
    }
    if (m_boneVariant) {
        if (model::Bone *bone = activeBone(project)) {
            bone->setBezierControlPoints(project->boneKeyframeInterpolationType(), value);
        }
    }
    else {
        project->globalCamera()->setBezierControlPoints(project->cameraKeyframeInterpolationType(), value);
    }
}

void
QtInterpolationCurveGraphDialog::restoreSnapshot()
{
    Project *project = m_service->projectHolder()->currentProject();
    if (project != m_project || project->isPlaying()) {
        return;
    }
    if (m_boneVariant) {
        if (model::Bone *bone = activeBone(project)) {
            bone->setBezierControlPoints(
                static_cast<nanoem_motion_bone_keyframe_interpolation_type_t>(m_snapshotType),
                m_snapshotControlPoint);
        }
    }
    else {
        project->globalCamera()->setBezierControlPoints(
            static_cast<nanoem_motion_camera_keyframe_interpolation_type_t>(m_snapshotType),
            m_snapshotControlPoint);
    }
}

void
QtInterpolationCurveGraphDialog::updatePlayhead(Project *project, const Vector4U8 &controlPoint)
{
    nanoem_f32_t x = 0, y = 0;
    bool visible = false;
    const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
    const Motion *motion = m_boneVariant ? project->resolveMotion(project->activeModel()) : project->cameraMotion();
    nanoem_motion_bone_keyframe_t *prevBone = nullptr, *nextBone = nullptr;
    nanoem_motion_camera_keyframe_t *prevCamera = nullptr, *nextCamera = nullptr;
    nanoem_frame_index_t interval = 0;
    if (motion && motion->data()) {
        if (m_boneVariant) {
            const nanoem_model_bone_t *bone = project->activeModel()->activeBone();
            const nanoem_unicode_string_t *namePtr = nanoemModelBoneGetName(bone, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
            nanoemMotionSearchClosestBoneKeyframes(motion->data(), namePtr, frameIndex, &prevBone, &nextBone);
            if (prevBone && nextBone) {
                const nanoem_frame_index_t start = nanoemMotionKeyframeObjectGetFrameIndex(
                        nanoemMotionBoneKeyframeGetKeyframeObject(prevBone)),
                    end = nanoemMotionKeyframeObjectGetFrameIndex(
                        nanoemMotionBoneKeyframeGetKeyframeObject(nextBone));
                if (end > start) {
                    interval = end - start;
                    x = Motion::coefficient(prevBone, nextBone, frameIndex);
                    visible = true;
                }
            }
        }
        else {
            nanoemMotionSearchClosestCameraKeyframes(motion->data(), frameIndex, &prevCamera, &nextCamera);
            if (prevCamera && nextCamera) {
                const nanoem_frame_index_t start = nanoemMotionKeyframeObjectGetFrameIndex(
                        nanoemMotionCameraKeyframeGetKeyframeObject(prevCamera)),
                    end = nanoemMotionKeyframeObjectGetFrameIndex(
                        nanoemMotionCameraKeyframeGetKeyframeObject(nextCamera));
                if (end > start) {
                    interval = end - start;
                    x = Motion::coefficient(prevCamera, nextCamera, frameIndex);
                    visible = true;
                }
            }
        }
    }
    if (visible) {
        const BezierCurve curve(Vector2U8(controlPoint.x, controlPoint.y),
            Vector2U8(controlPoint.z, controlPoint.w), interval);
        y = curve.value(x);
    }
    m_curveWidget->setPlayhead(x, y, visible);
}

} /* namespace qt */
} /* namespace nanoem */
