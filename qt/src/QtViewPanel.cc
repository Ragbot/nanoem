/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewPanel.h"
#include "QtApplicationService.h"

#include "emapp/ApplicationMenuBuilder.h"
#include "emapp/Error.h"
#include "emapp/ICamera.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QPushButton>
#include <QTimer>

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

/* matches the camera preset menu items: the ImGui view panel angles are identical to these presets */
static const nanoem_u32_t kViewDirectionMenuItemTypes[] = {
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetFront,
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetBack,
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetTop,
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetLeft,
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetRight,
    ApplicationMenuBuilder::kMenuItemTypeCameraPresetBottom,
};

static const char *const kViewDirectionLabelKeys[] = {
    "nanoem.gui.panel.view.front",
    "nanoem.gui.panel.view.back",
    "nanoem.gui.panel.view.up",
    "nanoem.gui.panel.view.left",
    "nanoem.gui.panel.view.right",
    "nanoem.gui.panel.view.bottom",
};

} /* namespace anonymous */

QtViewPanel::QtViewPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.view.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QGridLayout(this);
    for (int i = 0; i < kViewDirectionTypeMaxEnum; i++) {
        const ViewDirectionType type = static_cast<ViewDirectionType>(i);
        auto *button = new QPushButton(translate(m_service, kViewDirectionLabelKeys[i]), this);
        m_viewDirectionButtons[i] = button;
        layout->addWidget(button, i / 2, i % 2);
        connect(button, &QPushButton::clicked, this, [this, type]() { applyViewDirection(type); });
    }
    m_followModelCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.view.follow.model"), this);
    m_followBoneCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.view.follow.bone"), this);
    layout->addWidget(m_followModelCheckBox, 3, 0, 1, 2);
    layout->addWidget(m_followBoneCheckBox, 4, 0, 1, 2);
    connect(m_followModelCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            project->activeCamera()->setFollowingType(
                checked ? ICamera::kFollowingTypeModel : ICamera::kFollowingTypeNone);
        }
    });
    connect(m_followBoneCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            project->activeCamera()->setFollowingType(
                checked ? ICamera::kFollowingTypeBone : ICamera::kFollowingTypeNone);
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtViewPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

void
QtViewPanel::applyViewDirection(ViewDirectionType value)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Error error;
        m_service->dispatchMenuItemAction(project, kViewDirectionMenuItemTypes[value], error);
    }
}

void
QtViewPanel::refresh()
{
    Project *project = currentProject();
    if (project) {
        const bool enabled = !project->isPlaying();
        for (int i = 0; i < kViewDirectionTypeMaxEnum; i++) {
            m_viewDirectionButtons[i]->setEnabled(enabled);
        }
        const ICamera::FollowingType followingType = project->activeCamera()->followingType();
        m_followModelCheckBox->blockSignals(true);
        m_followModelCheckBox->setChecked(followingType == ICamera::kFollowingTypeModel);
        m_followModelCheckBox->setEnabled(enabled);
        m_followModelCheckBox->blockSignals(false);
        m_followBoneCheckBox->blockSignals(true);
        m_followBoneCheckBox->setChecked(followingType == ICamera::kFollowingTypeBone);
        m_followBoneCheckBox->setEnabled(enabled);
        m_followBoneCheckBox->blockSignals(false);
    }
}

} /* namespace qt */
} /* namespace nanoem */
