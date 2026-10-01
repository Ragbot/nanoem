/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtLightPanel.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/ILight.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"
#include "emapp/internal/LightValueState.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

static nanoem_f32_t
clampUnit(nanoem_f32_t value)
{
    return std::max<nanoem_f32_t>(0.0f, std::min<nanoem_f32_t>(1.0f, value));
}

} /* namespace anonymous */

QtLightPanel::QtLightPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.light.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    m_resetButton = new QPushButton(translate(m_service, "nanoem.gui.panel.light.reset"), this);
    layout->addWidget(m_resetButton);
    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(new QLabel(translate(m_service, "nanoem.gui.panel.light.color"), this));
    m_colorButton = new QPushButton(this);
    m_colorButton->setMinimumHeight(22);
    colorRow->addWidget(m_colorButton, 1);
    layout->addLayout(colorRow);
    auto *directionRow = new QHBoxLayout;
    m_directionLabel = new QLabel(translate(m_service, "nanoem.gui.panel.light.direction"), this);
    directionRow->addWidget(m_directionLabel);
    for (int i = 0; i < 3; i++) {
        auto *spinBox = new QDoubleSpinBox(this);
        spinBox->setRange(-1.0, 1.0);
        spinBox->setSingleStep(0.01);
        spinBox->setDecimals(2);
        spinBox->setKeyboardTracking(false);
        spinBox->setToolTip(translate(m_service, "nanoem.gui.panel.light.direction"));
        directionRow->addWidget(spinBox, 1);
        m_directionSpinBoxes[i] = spinBox;
    }
    layout->addLayout(directionRow);
    m_selfShadowButton = new QPushButton(translate(m_service, "nanoem.gui.panel.light.self-shadow"), this);
    layout->addWidget(m_selfShadowButton);
    m_registerButton = new QPushButton(translate(m_service, "nanoem.gui.panel.light.register"), this);
    layout->addWidget(m_registerButton);
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            project->activeLight()->reset();
        }
    });
    connect(m_colorButton, &QPushButton::clicked, this, &QtLightPanel::pickColor);
    for (int i = 0; i < 3; i++) {
        connect(m_directionSpinBoxes[i], &QDoubleSpinBox::valueChanged, this, &QtLightPanel::handleDirectionChanged);
        connect(m_directionSpinBoxes[i], &QDoubleSpinBox::editingFinished, this, &QtLightPanel::commitDirection);
    }
    connect(m_selfShadowButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            m_service->openSelfShadowConfigurationDialog();
        }
    });
    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            CommandRegistrator registrator(project);
            registrator.registerAddLightKeyframesCommandByCurrentLocalFrameIndex();
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtLightPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

void
QtLightPanel::pickColor()
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        ILight *light = project->activeLight();
        const Vector3 value(light->color());
        const QColor initial = QColor::fromRgbF(clampUnit(value.x), clampUnit(value.y), clampUnit(value.z));
        const QColor color(QColorDialog::getColor(initial, this, translate(m_service, "nanoem.gui.panel.light.title"),
            QColorDialog::ShowAlphaChannel));
        if (color.isValid()) {
            /* a color pick is a single discrete change: create, apply and commit the undo entry at once */
            internal::LightColorVectorValueState state(project, light);
            const nanoem_f32_t rgb[3] = { clampUnit(color.redF()), clampUnit(color.greenF()),
                clampUnit(color.blueF()) };
            state.setValue(rgb);
            state.commit();
            refresh();
        }
    }
}

void
QtLightPanel::handleDirectionChanged(double /* value */)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        ILight *light = project->activeLight();
        if (!m_directionState) {
            m_directionState = new internal::LightDirectionVectorValueState(project, light);
            m_stateProject = project;
        }
        const Vector3 direction(static_cast<nanoem_f32_t>(m_directionSpinBoxes[0]->value()),
            static_cast<nanoem_f32_t>(m_directionSpinBoxes[1]->value()),
            static_cast<nanoem_f32_t>(m_directionSpinBoxes[2]->value()));
        m_directionState->setValue(glm::value_ptr(direction));
    }
}

void
QtLightPanel::commitDirection()
{
    if (m_directionState) {
        m_directionState->commit();
        delete m_directionState;
        m_directionState = nullptr;
        m_stateProject = nullptr;
    }
}

void
QtLightPanel::validateStates(Project *project)
{
    if (m_stateProject != project) {
        delete m_directionState;
        m_directionState = nullptr;
        m_stateProject = nullptr;
    }
    if (m_directionState && project->isPlaying()) {
        delete m_directionState;
        m_directionState = nullptr;
    }
}

void
QtLightPanel::refresh()
{
    Project *project = currentProject();
    const bool visible = project != nullptr && project->activeModel() == nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project) {
        return;
    }
    validateStates(project);
    ILight *light = project->activeLight();
    const bool buttonEnabled = !project->isPlaying();
    m_resetButton->setEnabled(buttonEnabled);
    const Vector3 color(light->color());
    m_colorButton->setStyleSheet(QStringLiteral("background-color: rgb(%1, %2, %3);")
                                     .arg(qRound(clampUnit(color.x) * 255))
                                     .arg(qRound(clampUnit(color.y) * 255))
                                     .arg(qRound(clampUnit(color.z) * 255)));
    const bool directionEnabled = buttonEnabled && !m_directionState;
    const Vector3 direction(light->direction());
    const nanoem_f32_t values[3] = { direction.x, direction.y, direction.z };
    for (int i = 0; i < 3; i++) {
        m_directionSpinBoxes[i]->blockSignals(true);
        if (!m_directionState && !m_directionSpinBoxes[i]->hasFocus()) {
            m_directionSpinBoxes[i]->setValue(values[i]);
        }
        m_directionSpinBoxes[i]->setEnabled(directionEnabled || m_directionSpinBoxes[i]->hasFocus());
        m_directionSpinBoxes[i]->blockSignals(false);
    }
    m_selfShadowButton->setEnabled(buttonEnabled);
    m_registerButton->setEnabled(buttonEnabled);
}

} /* namespace qt */
} /* namespace nanoem */
