/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtCameraParametersDialog.h"
#include "QtApplicationService.h"

#include "emapp/ICamera.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"
#include "emapp/command/UpdateCameraCommand.h"

#include "glm/glm.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

/* ImGuiWindow::drawCameraParameterPanel uses ImGui::DragFloat with no clamp on look-at/distance
   and a [-90, 90] / [-180, 180] clamp on the euler angle rows */
const nanoem_f32_t kUnboundedMinimum = -100000.0f;
const nanoem_f32_t kUnboundedMaximum = 100000.0f;
const nanoem_f32_t kStep = 0.1f;
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

QtCameraParametersDialog::QtCameraParametersDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_camera(project->globalCamera())
    , m_lookAt(m_camera->lookAt())
    , m_angle(m_camera->angle())
    , m_distance(m_camera->distance())
{
    setWindowTitle(translate("nanoem.gui.window.camera.parameter.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.parameter.look-at"), this));
    auto *lookAtRow = new QHBoxLayout;
    for (int i = 0; i < 3; i++) {
        m_lookAtSpinBox[i] = createAxisSpinBox(i, m_lookAt[i], kUnboundedMinimum, kUnboundedMaximum, 3);
        lookAtRow->addWidget(m_lookAtSpinBox[i]);
    }
    layout->addLayout(lookAtRow);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.parameter.angle"), this));
    const Vector3 angleDegrees(glm::degrees(m_angle));
    auto *angleRow = new QHBoxLayout;
    for (int i = 0; i < 3; i++) {
        /* ImGuiWindow clamps the euler rows to [-90, 90] on the Y axis and [-180, 180] otherwise */
        const nanoem_f32_t maximum = i == 1 ? 90.0f : 180.0f;
        m_angleSpinBox[i] = createAxisSpinBox(i, angleDegrees[i], -maximum, maximum, 1);
        angleRow->addWidget(m_angleSpinBox[i]);
    }
    layout->addLayout(angleRow);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.parameter.distance"), this));
    auto *distanceRow = new QHBoxLayout;
    m_distanceSpinBox = createAxisSpinBox(0, m_distance, kUnboundedMinimum, kUnboundedMaximum, 3);
    m_distanceSpinBox->setPrefix(QString());
    distanceRow->addWidget(m_distanceSpinBox);
    layout->addLayout(distanceRow);
    for (int i = 0; i < 3; i++) {
        connect(m_lookAtSpinBox[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { handleLookAtChanged(); });
        connect(m_angleSpinBox[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { handleAngleChanged(); });
    }
    connect(m_distanceSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { handleDistanceChanged(); });
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtCameraParametersDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtCameraParametersDialog::reject);
    layout->addWidget(buttonBox);
}

void
QtCameraParametersDialog::handleLookAtChanged()
{
    m_camera->setLookAt(vector3Value(m_lookAtSpinBox));
    m_camera->update();
    m_project->resetAllModelEdges();
}

void
QtCameraParametersDialog::handleAngleChanged()
{
    m_camera->setAngle(glm::radians(vector3Value(m_angleSpinBox)));
    m_camera->update();
}

void
QtCameraParametersDialog::handleDistanceChanged()
{
    m_camera->setDistance(nanoem_f32_t(m_distanceSpinBox->value()));
    m_camera->update();
    m_project->resetAllModelEdges();
}

void
QtCameraParametersDialog::handleAccepted()
{
    const Vector3 lookAt(vector3Value(m_lookAtSpinBox));
    const Vector3 angle(glm::radians(vector3Value(m_angleSpinBox)));
    const nanoem_f32_t distance = nanoem_f32_t(m_distanceSpinBox->value());
    /* every row applies live, so the camera already holds the edited values, while
       command::UpdateCameraCommand snapshots the camera as the "before" state when it is created.
       Put the snapshot back first so the undo entry actually reverts this edit instead of being a
       no-op, then apply the edited values on top of the pushed command */
    m_camera->setLookAt(m_lookAt);
    m_camera->setAngle(m_angle);
    m_camera->setDistance(m_distance);
    m_camera->update();
    m_project->pushUndo(command::UpdateCameraCommand::create(
        m_project, m_camera, lookAt, angle, distance, m_camera->fovRadians(), m_camera->isPerspective()));
    m_camera->setLookAt(lookAt);
    m_camera->setAngle(angle);
    m_camera->setDistance(distance);
    m_camera->update();
    m_project->resetAllModelEdges();
    accept();
}

void
QtCameraParametersDialog::reject()
{
    /* ImGuiWindow::drawCameraParameterPanel resets the snapshot whenever the window is dismissed */
    m_camera->setLookAt(m_lookAt);
    m_camera->setAngle(m_angle);
    m_camera->setDistance(m_distance);
    m_camera->update();
    m_project->resetAllModelEdges();
    QDialog::reject();
}

QString
QtCameraParametersDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
