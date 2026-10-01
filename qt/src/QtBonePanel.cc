/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtBonePanel.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IModelObjectSelection.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/model/Bone.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
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

QtBonePanel::QtBonePanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.bone.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    auto *modeLayout = new QGridLayout;
    m_selectRadioButton = new QRadioButton(translate(m_service, "nanoem.gui.panel.bone.select"), this);
    m_rotateRadioButton = new QRadioButton(translate(m_service, "nanoem.gui.panel.bone.rotate"), this);
    m_moveRadioButton = new QRadioButton(translate(m_service, "nanoem.gui.panel.bone.move"), this);
    m_noneRadioButton = new QRadioButton(QStringLiteral("(none)"), this);
    m_modeButtonGroup = new QButtonGroup(this);
    m_modeButtonGroup->setExclusive(true);
    m_modeButtonGroup->addButton(m_selectRadioButton);
    m_modeButtonGroup->addButton(m_rotateRadioButton);
    m_modeButtonGroup->addButton(m_moveRadioButton);
    m_modeButtonGroup->addButton(m_noneRadioButton);
    modeLayout->addWidget(m_selectRadioButton, 0, 0);
    modeLayout->addWidget(m_rotateRadioButton, 0, 1);
    modeLayout->addWidget(m_moveRadioButton, 1, 0);
    modeLayout->addWidget(m_noneRadioButton, 1, 1);
    layout->addLayout(modeLayout);
    m_boxSelectButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.select.box"), this);
    m_selectAllButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.select.all"), this);
    m_selectDirtyButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.select.dirty"), this);
    auto *selectRow = new QHBoxLayout;
    selectRow->addWidget(m_boxSelectButton, 1);
    selectRow->addWidget(m_selectAllButton, 1);
    selectRow->addWidget(m_selectDirtyButton, 1);
    layout->addLayout(selectRow);
    m_copyButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.copy"), this);
    m_pasteButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.paste"), this);
    m_inversePasteButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.inverse-paste"), this);
    auto *clipboardRow = new QHBoxLayout;
    clipboardRow->addWidget(m_copyButton, 1);
    clipboardRow->addWidget(m_pasteButton, 1);
    layout->addLayout(clipboardRow);
    m_physicsCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.bone.physics-simulation"), this);
    auto *inverseRow = new QHBoxLayout;
    inverseRow->addWidget(m_inversePasteButton, 1);
    inverseRow->addWidget(m_physicsCheckBox, 1);
    layout->addLayout(inverseRow);
    m_registerButton = new QPushButton(translate(m_service, "nanoem.gui.panel.bone.register"), this);
    layout->addWidget(m_registerButton);
    connect(m_modeButtonGroup, &QButtonGroup::buttonClicked, this,
        [this](QAbstractButton *button) { applyEditingMode(button); });
    connect(m_boxSelectButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        Model *model = activeModel();
        if (project && model && !project->isPlaying()) {
            setSelectMode();
            IModelObjectSelection *selection = model->selection();
            selection->removeAllBones();
            selection->setBoxSelectedBoneModeEnabled(!selection->isBoxSelectedBoneModeEnabled());
        }
    });
    connect(m_selectAllButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        Model *model = activeModel();
        if (project && model && !project->isPlaying()) {
            setSelectMode();
            model->selection()->addAllBones();
        }
    });
    connect(m_selectDirtyButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        Model *model = activeModel();
        if (project && model && !project->isPlaying()) {
            setSelectMode();
            model->selection()->addAllDirtyBones();
        }
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            Error error;
            project->copyAllSelectedBones(error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_pasteButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            Error error;
            project->pasteAllSelectedBones(error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_inversePasteButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            Error error;
            project->symmetricPasteAllSelectedBones(error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_physicsCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            project->setPhysicsSimulationForBoneKeyframeEnabled(checked);
        }
    });
    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (Model *model = activeModel()) {
            if (project && !project->isPlaying()) {
                CommandRegistrator registrator(project);
                registrator.registerAddBoneKeyframesCommandBySelectedBoneSet(model);
            }
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtBonePanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

Model *
QtBonePanel::activeModel()
{
    Project *project = currentProject();
    return project ? project->activeModel() : nullptr;
}

void
QtBonePanel::setSelectMode()
{
    Project *project = currentProject();
    if (Model *model = activeModel()) {
        project->setEditingMode(Project::kEditingModeSelect);
        model->selection()->setBoxSelectedBoneModeEnabled(false);
    }
}

void
QtBonePanel::applyEditingMode(QAbstractButton *button)
{
    Project *project = currentProject();
    if (!project || project->isPlaying()) {
        return;
    }
    Project::EditingMode mode = Project::kEditingModeNone;
    if (button == m_selectRadioButton) {
        mode = Project::kEditingModeSelect;
    }
    else if (button == m_rotateRadioButton) {
        mode = Project::kEditingModeRotate;
    }
    else if (button == m_moveRadioButton) {
        mode = Project::kEditingModeMove;
    }
    Model *model = activeModel();
    project->setEditingMode(mode);
    if (model) {
        model->selection()->setBoxSelectedBoneModeEnabled(false);
    }
}

void
QtBonePanel::refresh()
{
    Project *project = currentProject();
    Model *model = project ? project->activeModel() : nullptr;
    const bool visible = project != nullptr && model != nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project || !model) {
        return;
    }
    const bool buttonEnabled = !project->isPlaying();
    const nanoem_model_bone_t *activeBone = model->activeBone();
    const Project::EditingMode mode = project->editingMode();
    m_selectRadioButton->blockSignals(true);
    m_rotateRadioButton->blockSignals(true);
    m_moveRadioButton->blockSignals(true);
    m_noneRadioButton->blockSignals(true);
    m_selectRadioButton->setChecked(mode == Project::kEditingModeSelect);
    m_rotateRadioButton->setChecked(mode == Project::kEditingModeRotate);
    m_moveRadioButton->setChecked(mode == Project::kEditingModeMove);
    m_noneRadioButton->setChecked(mode == Project::kEditingModeNone);
    m_selectRadioButton->setEnabled(buttonEnabled && model::Bone::isSelectable(activeBone));
    m_rotateRadioButton->setEnabled(buttonEnabled && model::Bone::isRotateable(activeBone));
    m_moveRadioButton->setEnabled(buttonEnabled && model::Bone::isMovable(activeBone));
    m_noneRadioButton->setEnabled(buttonEnabled);
    m_selectRadioButton->blockSignals(false);
    m_rotateRadioButton->blockSignals(false);
    m_moveRadioButton->blockSignals(false);
    m_noneRadioButton->blockSignals(false);
    m_boxSelectButton->setEnabled(buttonEnabled);
    m_selectAllButton->setEnabled(buttonEnabled);
    m_selectDirtyButton->setEnabled(buttonEnabled);
    m_copyButton->setEnabled(buttonEnabled);
    m_pasteButton->setEnabled(buttonEnabled);
    m_inversePasteButton->setEnabled(buttonEnabled);
    m_physicsCheckBox->blockSignals(true);
    m_physicsCheckBox->setChecked(project->isPhysicsSimulationForBoneKeyframeEnabled());
    m_physicsCheckBox->setEnabled(buttonEnabled);
    m_physicsCheckBox->blockSignals(false);
    m_registerButton->setEnabled(buttonEnabled);
}

} /* namespace qt */
} /* namespace nanoem */
