/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelOutsideParentDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"

#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/euler_angles.hpp"

#include <QComboBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

static QString
boneNameOrNone(const model::Bone *bone, QtModelOutsideParentDialog *self, const char *noneKey)
{
    return bone ? QString::fromUtf8(bone->nameConstString()) : self->translate(noneKey);
}

QtModelOutsideParentDialog::QtModelOutsideParentDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.op.model"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.model.subject.bone"), this));
    m_subjectBoneCombo = new QComboBox(this);
    connect(m_subjectBoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtModelOutsideParentDialog::handleSubjectBoneChanged);
    layout->addWidget(m_subjectBoneCombo);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.model.target.model"), this));
    m_targetModelCombo = new QComboBox(this);
    connect(m_targetModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtModelOutsideParentDialog::handleTargetModelChanged);
    layout->addWidget(m_targetModelCombo);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.model.target.bone"), this));
    m_targetBoneCombo = new QComboBox(this);
    connect(m_targetBoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtModelOutsideParentDialog::handleTargetBoneChanged);
    layout->addWidget(m_targetBoneCombo);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.model.subject.bone.translation"), this));
    m_translationLabel = new QLabel(this);
    m_translationLabel->setStyleSheet("font-family: monospace;");
    layout->addWidget(m_translationLabel);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.model.subject.bone.orientation"), this));
    m_orientationLabel = new QLabel(this);
    m_orientationLabel->setStyleSheet("font-family: monospace;");
    layout->addWidget(m_orientationLabel);
    auto *registerButton = new QPushButton(translate("nanoem.gui.window.op.model.register.model"), this);
    connect(registerButton, &QPushButton::clicked, this, &QtModelOutsideParentDialog::handleRegister);
    layout->addWidget(registerButton);
    /* initial population: subject bone combo with active model's bones */
    refresh();
    /* ImGuiWindow::draw closes this dialog when the active model goes away */
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_project->activeModel() || m_project->activeModel() != m_project->activeModel()) {
            close();
        }
    });
    timer->start();
}

void
QtModelOutsideParentDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
}

void
QtModelOutsideParentDialog::refresh()
{
    if (m_updating) {
        return;
    }
    m_updating = true;
    Model *activeModel = m_project->activeModel();
    if (!activeModel) {
        m_updating = false;
        return;
    }
    const nanoem_model_bone_t *subjectBonePtr = activeModel->activeOutsideParentSubjectBone();
    StringPair targetName = activeModel->findOutsideParent(subjectBonePtr);
    /* subject bone combo */
    m_subjectBoneCombo->clear();
    m_subjectBoneCombo->addItem(translate("nanoem.gui.window.op.model.subject.bone.none"), QVariant::fromValue(static_cast<quintptr>(0)));
    int subjectIndex = 0;
    nanoem_rsize_t numBones;
    nanoem_model_bone_t *const *bones = nanoemModelGetAllBoneObjects(activeModel->data(), &numBones);
    for (nanoem_rsize_t i = 0; i < numBones; i++) {
        const nanoem_model_bone_t *bonePtr = bones[i];
        if (const model::Bone *bone = model::Bone::cast(bonePtr)) {
            m_subjectBoneCombo->addItem(QString::fromUtf8(bone->nameConstString()), QVariant::fromValue(reinterpret_cast<quintptr>(bonePtr)));
            if (bonePtr == subjectBonePtr) {
                subjectIndex = m_subjectBoneCombo->count() - 1;
            }
        }
    }
    m_subjectBoneCombo->setCurrentIndex(subjectIndex);
    /* target model combo */
    m_targetModelCombo->clear();
    m_targetModelCombo->addItem(translate("nanoem.gui.window.op.model.target.model.none"), QVariant::fromValue(static_cast<Model *>(nullptr)));
    int targetModelIndex = 0;
    Model *targetModel = nullptr;
    if (!targetName.first.empty()) {
        targetModel = m_project->findModelByName(targetName.first.c_str());
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
    /* target bone combo - populated by handleTargetModelChanged */
    handleTargetModelChanged(targetModelIndex);
    if (!targetName.second.empty()) {
        int boneIndex = m_targetBoneCombo->findData(QVariant::fromValue(targetName.second.c_str()));
        if (boneIndex >= 0) {
            m_targetBoneCombo->setCurrentIndex(boneIndex);
        }
    }
    updateTranslationOrientation();
    m_updating = false;
}

void
QtModelOutsideParentDialog::handleSubjectBoneChanged(int index)
{
    if (m_updating) {
        return;
    }
    Model *activeModel = m_project->activeModel();
    if (!activeModel) {
        return;
    }
    const nanoem_model_bone_t *bonePtr = index == 0 ? nullptr :
        reinterpret_cast<const nanoem_model_bone_t *>(m_subjectBoneCombo->itemData(index).value<quintptr>());
    activeModel->setActiveOutsideParentSubjectBone(bonePtr);
    refresh();
}

void
QtModelOutsideParentDialog::handleTargetModelChanged(int index)
{
    if (m_updating) {
        return;
    }
    Model *activeModel = m_project->activeModel();
    if (!activeModel) {
        return;
    }
    Model *targetModel = index == 0 ? nullptr :
        m_targetModelCombo->itemData(index).value<Model *>();
    const nanoem_model_bone_t *subjectBonePtr = activeModel->activeOutsideParentSubjectBone();
    StringPair targetName = activeModel->findOutsideParent(subjectBonePtr);
    if (targetModel) {
        targetName.first = targetModel->name();
    }
    else {
        targetName = StringPair();
    }
    activeModel->setOutsideParent(subjectBonePtr, targetName);
    /* rebuild target bone combo */
    m_targetBoneCombo->clear();
    m_targetBoneCombo->addItem(translate("nanoem.gui.window.op.model.target.bone.none"), QVariant::fromValue(static_cast<quintptr>(0)));
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
    m_updating = false; /* allow target bone handler */
}

void
QtModelOutsideParentDialog::handleTargetBoneChanged(int index)
{
    if (m_updating) {
        return;
    }
    Model *activeModel = m_project->activeModel();
    if (!activeModel) {
        return;
    }
    const nanoem_model_bone_t *subjectBonePtr = activeModel->activeOutsideParentSubjectBone();
    StringPair targetName = activeModel->findOutsideParent(subjectBonePtr);
    if (index > 0) {
        const QByteArray boneName = m_targetBoneCombo->itemData(index).toByteArray();
        targetName.second = boneName.constData();
    }
    else {
        targetName.second = String();
    }
    activeModel->setOutsideParent(subjectBonePtr, targetName);
}

void
QtModelOutsideParentDialog::handleRegister()
{
    if (Model *activeModel = m_project->activeModel()) {
        CommandRegistrator registrator(m_project);
        registrator.registerAddModelKeyframesCommandByCurrentLocalFrameIndex(activeModel);
    }
}

void
QtModelOutsideParentDialog::updateTranslationOrientation()
{
    Model *activeModel = m_project->activeModel();
    if (!activeModel) {
        return;
    }
    const nanoem_model_bone_t *subjectBonePtr = activeModel->activeOutsideParentSubjectBone();
    if (subjectBonePtr) {
        if (const model::Bone *subjectBone = model::Bone::cast(subjectBonePtr)) {
            Vector3 translation = subjectBone->localTranslation();
            Vector3 orientation = glm::eulerAngles(subjectBone->localOrientation());
            m_translationLabel->setText(QString("X: %1  Y: %2  Z: %3")
                .arg(translation.x, 0, 'f', 2)
                .arg(translation.y, 0, 'f', 2)
                .arg(translation.z, 0, 'f', 2));
            m_orientationLabel->setText(QString("X: %1  Y: %2  Z: %3")
                .arg(glm::degrees(orientation.x), 0, 'f', 2)
                .arg(glm::degrees(orientation.y), 0, 'f', 2)
                .arg(glm::degrees(orientation.z), 0, 'f', 2));
        }
    }
    else {
        m_translationLabel->setText("X: 0.00  Y: 0.00  Z: 0.00");
        m_orientationLabel->setText("X: 0.00  Y: 0.00  Z: 0.00");
    }
}

QString
QtModelOutsideParentDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */