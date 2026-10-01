/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtCameraPanel.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/ICamera.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/internal/CameraValueState.h"
#include "emapp/internal/imgui/CameraParentModelBoneSelector.h"
#include "emapp/internal/imgui/CameraParentModelSelector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
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

} /* namespace anonymous */

QtCameraPanel::QtCameraPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.camera.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    m_resetButton = new QPushButton(translate(m_service, "nanoem.gui.panel.camera.reset"), this);
    layout->addWidget(m_resetButton);
    m_perspectiveCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.camera.perspective"), this);
    layout->addWidget(m_perspectiveCheckBox);
    auto *fovRow = new QHBoxLayout;
    fovRow->addWidget(new QLabel(translate(m_service, "nanoem.gui.panel.camera.fov"), this));
    m_fovSpinBox = new QSpinBox(this);
    m_fovSpinBox->setRange(1, 135);
    m_fovSpinBox->setKeyboardTracking(false);
    fovRow->addWidget(m_fovSpinBox, 1);
    layout->addLayout(fovRow);
    m_modelComboBox = new QComboBox(this);
    layout->addWidget(m_modelComboBox);
    m_boneComboBox = new QComboBox(this);
    layout->addWidget(m_boneComboBox);
    m_registerButton = new QPushButton(translate(m_service, "nanoem.gui.panel.camera.register"), this);
    layout->addWidget(m_registerButton);
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            ICamera *camera = project->activeCamera();
            camera->reset();
            camera->update();
            project->resetAllModelEdges();
        }
    });
    connect(m_perspectiveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            ICamera *camera = project->activeCamera();
            camera->setPerspective(checked);
            camera->update();
            project->resetAllModelEdges();
        }
    });
    connect(m_fovSpinBox, &QSpinBox::valueChanged, this, &QtCameraPanel::handleFovChanged);
    connect(m_fovSpinBox, &QSpinBox::editingFinished, this, &QtCameraPanel::commitFov);
    connect(m_modelComboBox, &QComboBox::activated, this, &QtCameraPanel::applyOutsideParentModel);
    connect(m_boneComboBox, &QComboBox::activated, this, &QtCameraPanel::applyOutsideParentBone);
    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            CommandRegistrator registrator(project);
            registrator.registerAddCameraKeyframesCommandByCurrentLocalFrameIndex();
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtCameraPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

void
QtCameraPanel::handleFovChanged(int value)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        ICamera *camera = project->activeCamera();
        if (camera) {
            if (!m_fovState) {
                m_fovState = new internal::CameraFovVectorValueState(project, camera);
                m_fovStateProject = project;
                m_fovStateCamera = camera;
            }
            const nanoem_f32_t v = static_cast<nanoem_f32_t>(value);
            m_fovState->setValue(&v);
        }
    }
}

void
QtCameraPanel::commitFov()
{
    if (m_fovState) {
        m_fovState->commit();
        delete m_fovState;
        m_fovState = nullptr;
        m_fovStateProject = nullptr;
        m_fovStateCamera = nullptr;
    }
}

void
QtCameraPanel::validateFovState(Project *project)
{
    if (m_fovState && (m_fovStateProject != project || m_fovStateCamera != project->activeCamera() ||
                         project->isPlaying())) {
        delete m_fovState;
        m_fovState = nullptr;
        m_fovStateProject = nullptr;
        m_fovStateCamera = nullptr;
    }
}

void
QtCameraPanel::applyOutsideParentModel(int index)
{
    Project *project = currentProject();
    if (project) {
        ICamera *camera = project->activeCamera();
        StringPair pair(camera->outsideParent());
        const internal::imgui::CameraParentModelSelector selector(project);
        pair.first = selector.resolveName(index);
        camera->setOutsideParent(pair);
        refresh();
    }
}

void
QtCameraPanel::applyOutsideParentBone(int index)
{
    Project *project = currentProject();
    if (project) {
        ICamera *camera = project->activeCamera();
        StringPair pair(camera->outsideParent());
        const internal::imgui::CameraParentModelSelector modelSelector(project);
        const Model *model = modelSelector.resolveModel(m_modelComboBox->currentIndex());
        const internal::imgui::CameraParentModelBoneSelector boneSelector(model, project->translator());
        pair.second = boneSelector.resolveName(index);
        camera->setOutsideParent(pair);
        refresh();
    }
}

void
QtCameraPanel::refreshModelComboBox(Project *project, const ICamera *camera)
{
    const internal::imgui::CameraParentModelSelector selector(project);
    const StringPair pair(camera->outsideParent());
    const int count = selector.count();
    bool rebuild = m_modelComboBox->count() != count;
    for (int i = 0; !rebuild && i < count; i++) {
        const char *text = nullptr;
        selector.select(i, &text);
        if (m_modelComboBox->itemText(i) != QString::fromUtf8(text)) {
            rebuild = true;
        }
    }
    if (rebuild) {
        m_modelComboBox->clear();
        for (int i = 0; i < count; i++) {
            const char *text = nullptr;
            selector.select(i, &text);
            m_modelComboBox->addItem(QString::fromUtf8(text));
        }
    }
    const int index = selector.index(pair);
    if (m_modelComboBox->currentIndex() != index) {
        m_modelComboBox->blockSignals(true);
        m_modelComboBox->setCurrentIndex(index);
        m_modelComboBox->blockSignals(false);
    }
}

void
QtCameraPanel::refreshBoneComboBox(Project *project, int modelIndex, const ICamera *camera)
{
    const internal::imgui::CameraParentModelSelector modelSelector(project);
    const Model *model = modelSelector.resolveModel(modelIndex);
    const internal::imgui::CameraParentModelBoneSelector boneSelector(model, project->translator());
    const StringPair pair(camera->outsideParent());
    const int count = boneSelector.count();
    bool rebuild = m_boneComboBox->count() != count;
    for (int i = 0; !rebuild && i < count; i++) {
        const char *text = nullptr;
        boneSelector.select(i, &text);
        if (m_boneComboBox->itemText(i) != QString::fromUtf8(text)) {
            rebuild = true;
        }
    }
    if (rebuild) {
        m_boneComboBox->clear();
        for (int i = 0; i < count; i++) {
            const char *text = nullptr;
            boneSelector.select(i, &text);
            m_boneComboBox->addItem(QString::fromUtf8(text));
        }
    }
    const int index = boneSelector.index(pair);
    if (m_boneComboBox->currentIndex() != index) {
        m_boneComboBox->blockSignals(true);
        m_boneComboBox->setCurrentIndex(index);
        m_boneComboBox->blockSignals(false);
    }
}

void
QtCameraPanel::refresh()
{
    Project *project = currentProject();
    const bool visible = project != nullptr && project->activeModel() == nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project) {
        return;
    }
    validateFovState(project);
    ICamera *camera = project->activeCamera();
    const bool buttonEnabled = !project->isPlaying();
    m_resetButton->setEnabled(buttonEnabled);
    m_perspectiveCheckBox->blockSignals(true);
    m_perspectiveCheckBox->setChecked(camera->isPerspective());
    m_perspectiveCheckBox->setEnabled(buttonEnabled);
    m_perspectiveCheckBox->blockSignals(false);
    m_fovSpinBox->setEnabled(buttonEnabled);
    if (!m_fovState && !m_fovSpinBox->hasFocus()) {
        m_fovSpinBox->blockSignals(true);
        m_fovSpinBox->setValue(camera->fov());
        m_fovSpinBox->blockSignals(false);
    }
    refreshModelComboBox(project, camera);
    refreshBoneComboBox(project, m_modelComboBox->currentIndex(), camera);
    m_registerButton->setEnabled(buttonEnabled);
}

} /* namespace qt */
} /* namespace nanoem */
