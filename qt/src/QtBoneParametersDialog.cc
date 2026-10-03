/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtBoneParametersDialog.h"
#include "QtApplicationService.h"

#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/model/Bone.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

/* ImGuiWindow::kTranslationStepFactor / kOrientationStepFactor and the DragFloat display formats
   used by ImGuiWindow::drawBoneParameterPanel */
const nanoem_f32_t kStep = 0.1f;
const nanoem_f32_t kUnboundedMinimum = -100000.0f;
const nanoem_f32_t kUnboundedMaximum = 100000.0f;
const char *const kAxisPrefixes[] = { "X: ", "Y: ", "Z: " };

QDoubleSpinBox *
createAxisSpinBox(int axis, nanoem_f32_t value, nanoem_f32_t minimum, nanoem_f32_t maximum, int decimals)
{
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(decimals);
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(kStep);
    spinBox->setPrefix(QString::fromUtf8(kAxisPrefixes[axis]));
    spinBox->setValue(value);
    return spinBox;
}

Vector3
vector3Value(QDoubleSpinBox *value[])
{
    return Vector3(nanoem_f32_t(value[0]->value()), nanoem_f32_t(value[1]->value()), nanoem_f32_t(value[2]->value()));
}

} /* namespace anonymous */

QtBoneParametersDialog::QtBoneParametersDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_model(project->activeModel())
{
    setWindowTitle(translate("nanoem.gui.window.bone.parameter.title"));
    m_model->saveBindPose(m_bindPose);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.bone.parameter.translation"), this));
    auto *translationRow = new QHBoxLayout;
    auto *orientationRow = new QHBoxLayout;
    for (int i = 0; i < 3; i++) {
        m_translationSpinBox[i] = createAxisSpinBox(i, 0.0f, kUnboundedMinimum, kUnboundedMaximum, 3);
        translationRow->addWidget(m_translationSpinBox[i]);
        m_orientationSpinBox[i] = createAxisSpinBox(i, 0.0f, -180.0f, 180.0f, 1);
        orientationRow->addWidget(m_orientationSpinBox[i]);
    }
    layout->addLayout(translationRow);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.bone.parameter.orientation"), this));
    layout->addLayout(orientationRow);
    connect(m_translationSpinBox[0], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleTranslationChanged(); });
    connect(m_translationSpinBox[1], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleTranslationChanged(); });
    connect(m_translationSpinBox[2], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleTranslationChanged(); });
    connect(m_orientationSpinBox[0], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleOrientationChanged(); });
    connect(m_orientationSpinBox[1], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleOrientationChanged(); });
    connect(m_orientationSpinBox[2], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleOrientationChanged(); });
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtBoneParametersDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtBoneParametersDialog::reject);
    layout->addWidget(buttonBox);
    /* ImGuiWindow::drawBoneParameterPanel closes the window as soon as the active model changes or
       the model has no active bone */
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() {
        Model *activeModel = m_project->activeModel();
        if (activeModel != m_model || !activeModel->activeBone()) {
            close();
        }
    });
    timer->start();
    refresh();
}

void
QtBoneParametersDialog::handleTranslationChanged()
{
    if (nanoem_model_bone_t *activeBone = m_model->activeBone()) {
        if (model::Bone *bone = model::Bone::cast(activeBone)) {
            bone->setLocalUserTranslation(vector3Value(m_translationSpinBox));
            m_model->performAllBonesTransform();
        }
    }
}

void
QtBoneParametersDialog::handleOrientationChanged()
{
    if (nanoem_model_bone_t *activeBone = m_model->activeBone()) {
        if (model::Bone *bone = model::Bone::cast(activeBone)) {
            const Quaternion orientation(glm::radians(vector3Value(m_orientationSpinBox)));
            bone->setLocalUserOrientation(orientation);
            m_model->performAllBonesTransform();
        }
    }
}

void
QtBoneParametersDialog::handleAccepted()
{
    const Vector3 translation(vector3Value(m_translationSpinBox));
    const Quaternion orientation(glm::radians(vector3Value(m_orientationSpinBox)));
    /* every row applies live, and Model::registerUpdateActiveBoneTransformCommand snapshots the
       bind pose as the "before" state before it applies the arguments: restore the snapshot taken
       when the dialog opened first so the pushed undo entry actually reverts this edit */
    m_model->restoreBindPose(m_bindPose);
    m_model->performAllBonesTransform();
    m_model->registerUpdateActiveBoneTransformCommand(translation, orientation);
    accept();
}

void
QtBoneParametersDialog::reject()
{
    /* dismissing without confirming restores the bind pose, like ImGuiWindow's !visible path */
    m_model->restoreBindPose(m_bindPose);
    m_model->performAllBonesTransform();
    QDialog::reject();
}

void
QtBoneParametersDialog::refresh()
{
    if (nanoem_model_bone_t *activeBone = m_model->activeBone()) {
        const model::Bone *bone = model::Bone::cast(activeBone);
        const Vector3 translation(bone->localUserTranslation());
        const Vector3 orientation(glm::degrees(glm::eulerAngles(bone->localUserOrientation())));
        for (int i = 0; i < 3; i++) {
            QSignalBlocker blocker(m_translationSpinBox[i]);
            m_translationSpinBox[i]->setValue(translation[i]);
            QSignalBlocker orientationBlocker(m_orientationSpinBox[i]);
            m_orientationSpinBox[i]->setValue(orientation[i]);
        }
    }
}

QString
QtBoneParametersDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
