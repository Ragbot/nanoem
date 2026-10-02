/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtPhysicsEngineDialog.h"
#include "QtApplicationService.h"

#include "emapp/ITranslator.h"
#include "emapp/PhysicsEngine.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

static QDoubleSpinBox *
createSpinBox(nanoem_f32_t minimum, nanoem_f32_t maximum, int decimals, nanoem_f32_t step)
{
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(decimals);
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    return spinBox;
}

QtPhysicsEngineDialog::QtPhysicsEngineDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    const PhysicsEngine *engine = project->physicsEngine();
    m_lastDirection = engine->direction();
    m_lastAcceleration = engine->acceleration();
    m_lastTimeStepFactor = project->timeStepFactor();
    setWindowTitle(translate("nanoem.gui.window.project.physics-engine.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.physics-engine.acceleration"), this));
    m_accelerationSpinBox = createSpinBox(0.0f, 10000.0f, 2, 0.1f);
    m_accelerationSpinBox->setValue(m_lastAcceleration);
    connect(m_accelerationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) {
            if (PhysicsEngine *engine = m_project->physicsEngine()) {
                engine->setAcceleration(nanoem_f32_t(value));
            }
        });
    layout->addWidget(m_accelerationSpinBox);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.physics-engine.direction"), this));
    auto *directionLayout = new QHBoxLayout;
    for (int i = 0; i < 3; i++) {
        m_directionSpinBox[i] = createSpinBox(-1.0f, 1.0f, 2, 0.01f);
        m_directionSpinBox[i]->setValue(m_lastDirection[i]);
        connect(m_directionSpinBox[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, i](double value) {
                if (PhysicsEngine *engine = m_project->physicsEngine()) {
                    Vector3 dir = engine->direction();
                    dir[i] = nanoem_f32_t(value);
                    engine->setDirection(dir);
                }
            });
        directionLayout->addWidget(new QLabel(QString("XYZ")[i], this));
        directionLayout->addWidget(m_directionSpinBox[i]);
    }
    layout->addLayout(directionLayout);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.physics-engine.time-step-factor"), this));
    m_timeStepFactorSpinBox = createSpinBox(0.01f, 2.0f, 2, 0.05f);
    m_timeStepFactorSpinBox->setValue(m_lastTimeStepFactor);
    connect(m_timeStepFactorSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) {
            m_project->setTimeStepFactor(nanoem_f32_t(value));
        });
    layout->addWidget(m_timeStepFactorSpinBox);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (PhysicsEngine *engine = m_project->physicsEngine()) {
            engine->apply();
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtPhysicsEngineDialog::reject);
    layout->addWidget(buttonBox);
}

/* Cancel restores all values and calls engine->apply() like the ImGui version */
void
QtPhysicsEngineDialog::reject()
{
    if (PhysicsEngine *engine = m_project->physicsEngine()) {
        engine->setDirection(m_lastDirection);
        engine->setAcceleration(m_lastAcceleration);
        m_project->setTimeStepFactor(m_lastTimeStepFactor);
        engine->apply();
    }
    QDialog::reject();
}

QString
QtPhysicsEngineDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */