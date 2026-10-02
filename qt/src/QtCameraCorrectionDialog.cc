/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtCameraCorrectionDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/ITranslator.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

/* the ImGui DragFloat rows were unbounded with a "%.2f" display; the spin box keeps the two decimal
   places and a prefix so it reads the same " * 1.00" / " + 0.00" as the ImGui format string */
static const nanoem_f32_t kFactorMinimum = -100000.0f;
static const nanoem_f32_t kFactorMaximum = 100000.0f;
static const nanoem_f32_t kFactorStep = 0.1f;

static QDoubleSpinBox *
createFactorSpinBox(const char *prefix, nanoem_f32_t value)
{
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(2);
    spinBox->setRange(kFactorMinimum, kFactorMaximum);
    spinBox->setSingleStep(kFactorStep);
    spinBox->setPrefix(QString::fromUtf8(prefix));
    spinBox->setValue(value);
    return spinBox;
}

static void
createVectorFactorRows(QVBoxLayout *layout, QDoubleSpinBox *mul[], QDoubleSpinBox *add[])
{
    for (int i = 0; i < 3; i++) {
        auto *row = new QHBoxLayout;
        mul[i] = createFactorSpinBox(" * ", 1.0f);
        add[i] = createFactorSpinBox(" + ", 0.0f);
        row->addWidget(mul[i]);
        row->addWidget(add[i]);
        layout->addLayout(row);
    }
}

static Vector3
vector3Value(QDoubleSpinBox *value[])
{
    return Vector3(nanoem_f32_t(value[0]->value()), nanoem_f32_t(value[1]->value()), nanoem_f32_t(value[2]->value()));
}

QtCameraCorrectionDialog::QtCameraCorrectionDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.camera.correction.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.correction.look-at"), this));
    createVectorFactorRows(layout, m_lookAtMul, m_lookAtAdd);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.correction.angle"), this));
    createVectorFactorRows(layout, m_angleMul, m_angleAdd);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.camera.correction.distance"), this));
    auto *distanceRow = new QHBoxLayout;
    m_distanceMul = createFactorSpinBox(" * ", 1.0f);
    m_distanceAdd = createFactorSpinBox(" + ", 0.0f);
    distanceRow->addWidget(m_distanceMul);
    distanceRow->addWidget(m_distanceAdd);
    layout->addLayout(distanceRow);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtCameraCorrectionDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtCameraCorrectionDialog::reject);
    layout->addWidget(buttonBox);
}

void
QtCameraCorrectionDialog::handleAccepted()
{
    Error error;
    CommandRegistrator registrator(m_project);
    const Motion::CorrectionVectorFactor lookAt(vector3Value(m_lookAtMul), vector3Value(m_lookAtAdd));
    const Motion::CorrectionVectorFactor angle(vector3Value(m_angleMul), vector3Value(m_angleAdd));
    const Motion::CorrectionScalarFactor distance(
        nanoem_f32_t(m_distanceMul->value()), nanoem_f32_t(m_distanceAdd->value()));
    registrator.registerCorrectAllSelectedCameraKeyframesCommand(lookAt, angle, distance, error);
    error.notify(m_project->eventPublisher());
    accept();
}

QString
QtCameraCorrectionDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
