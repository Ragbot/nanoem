/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtAccessoryOutsideParentDialog.h"
#include "QtApplicationService.h"

#include "emapp/Accessory.h"
#include "emapp/CommandRegistrator.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtAccessoryOutsideParentDialog::QtAccessoryOutsideParentDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.op.accessory"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.accessory.target.model"), this));
    m_targetModelCombo = new QComboBox(this);
    connect(m_targetModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtAccessoryOutsideParentDialog::handleTargetModelChanged);
    layout->addWidget(m_targetModelCombo);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.accessory.target.bone"), this));
    m_targetBoneCombo = new QComboBox(this);
    connect(m_targetBoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtAccessoryOutsideParentDialog::handleTargetBoneChanged);
    layout->addWidget(m_targetBoneCombo);
    auto *registerButton = new QPushButton(translate("nanoem.gui.window.op.accessory.register"), this);
    connect(registerButton, &QPushButton::clicked, this, &QtAccessoryOutsideParentDialog::handleRegister);
    layout->addWidget(registerButton);
    refresh();
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_project->activeAccessory()) {
            close();
        }
    });
    timer->start();
}

void
QtAccessoryOutsideParentDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
}

void
QtAccessoryOutsideParentDialog::refresh()
{
    Accessory *activeAccessory = m_project->activeAccessory();
    if (!activeAccessory) {
        return;
    }
    StringPair currentOutsideParent = activeAccessory->outsideParent();
    m_targetModelCombo->clear();
    m_targetModelCombo->addItem(translate("nanoem.gui.window.op.accessory.target.model.none"), QVariant::fromValue(static_cast<Model *>(nullptr)));
    int targetModelIndex = 0;
    Model *targetModel = nullptr;
    if (!currentOutsideParent.first.empty()) {
        targetModel = m_project->findModelByName(currentOutsideParent.first.c_str());
        if (targetModel) {
            targetModelIndex = 1;
        }
    }
    const Project::ModelList *models = m_project->allModels();
    for (Project::ModelList::const_iterator it = models->begin(), end = models->end(); it != end; ++it) {
        Model *model = *it;
        m_targetModelCombo->addItem(QString::fromUtf8(model->nameConstString()), QVariant::fromValue(model));
        if (model == targetModel) {
            targetModelIndex = m_targetModelCombo->count() - 1;
        }
    }
    m_targetModelCombo->setCurrentIndex(targetModelIndex);
    /* rebuild target bone combo based on selected target model */
    handleTargetModelChanged(targetModelIndex);
    if (!currentOutsideParent.second.empty()) {
        int boneIndex = m_targetBoneCombo->findData(QVariant::fromValue(currentOutsideParent.second.c_str()));
        if (boneIndex >= 0) {
            m_targetBoneCombo->setCurrentIndex(boneIndex);
        }
    }
}

void
QtAccessoryOutsideParentDialog::handleTargetModelChanged(int index)
{
    Accessory *activeAccessory = m_project->activeAccessory();
    if (!activeAccessory) {
        return;
    }
    StringPair currentOutsideParent = activeAccessory->outsideParent();
    Model *targetModel = index == 0 ? nullptr :
        m_targetModelCombo->itemData(index).value<Model *>();
    if (targetModel) {
        currentOutsideParent.first = targetModel->name();
    }
    else {
        currentOutsideParent = StringPair();
    }
    activeAccessory->setOutsideParent(currentOutsideParent);
    m_targetBoneCombo->clear();
    m_targetBoneCombo->addItem(translate("nanoem.gui.window.op.accessory.target.bone.none"), QVariant::fromValue(QString()));
    if (targetModel) {
        nanoem_rsize_t numBones;
        nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(targetModel->data(), &numBones);
        for (nanoem_rsize_t i = 0; i < numBones; i++) {
            const nanoem_model_bone_t *bonePtr = bones[i];
            if (const model::Bone *bone = model::Bone::cast(bonePtr)) {
                m_targetBoneCombo->addItem(QString::fromUtf8(bone->nameConstString()), QVariant::fromValue(bone->canonicalName()));
            }
        }
    }
}

void
QtAccessoryOutsideParentDialog::handleTargetBoneChanged(int index)
{
    Accessory *activeAccessory = m_project->activeAccessory();
    if (!activeAccessory) {
        return;
    }
    StringPair currentOutsideParent = activeAccessory->outsideParent();
    if (index > 0) {
        const QByteArray boneName = m_targetBoneCombo->itemData(index).toByteArray();
        currentOutsideParent.second = boneName.constData();
    }
    else {
        currentOutsideParent.second = String();
    }
    activeAccessory->setOutsideParent(currentOutsideParent);
}

void
QtAccessoryOutsideParentDialog::handleRegister()
{
    if (Accessory *activeAccessory = m_project->activeAccessory()) {
        CommandRegistrator registrator(m_project);
        registrator.registerAddAccessoryKeyframesCommandByCurrentLocalFrameIndex(activeAccessory);
    }
}

QString
QtAccessoryOutsideParentDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */