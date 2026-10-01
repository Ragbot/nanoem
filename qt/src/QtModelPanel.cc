/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelPanel.h"
#include "QtApplicationClient.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IFileManager.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/ModalDialogFactory.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/internal/imgui/ActiveModelSelector.h"
#include "emapp/internal/imgui/ConstraintSelector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
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

QtModelPanel::QtModelPanel(QtApplicationService *service, QtApplicationClient *client, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.model.title"), parent)
    , m_service(service)
    , m_client(client)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    m_nameStack = new QStackedWidget(this);
    m_modelComboBox = new QComboBox(this);
    m_modelComboBox->installEventFilter(this);
    m_nameLineEdit = new QLineEdit(this);
    m_nameStack->addWidget(m_modelComboBox);
    m_nameStack->addWidget(m_nameLineEdit);
    layout->addWidget(m_nameStack);
    m_loadButton = new QPushButton(translate(m_service, "nanoem.gui.panel.model.load"), this);
    m_deleteButton = new QPushButton(translate(m_service, "nanoem.gui.panel.model.delete"), this);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_loadButton);
    buttonRow->addWidget(m_deleteButton);
    layout->addLayout(buttonRow);
    m_visibleCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.model.visible.short"), this);
    m_visibleCheckBox->setToolTip(translate(m_service, "nanoem.gui.panel.model.visible"));
    m_shadowCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.model.shadow.short"), this);
    m_shadowCheckBox->setToolTip(translate(m_service, "nanoem.gui.panel.model.shadow"));
    m_addBlendCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.model.blend-add.short"), this);
    m_addBlendCheckBox->setToolTip(translate(m_service, "nanoem.gui.panel.model.blend-add"));
    auto *toggleRow = new QHBoxLayout;
    toggleRow->addWidget(m_visibleCheckBox);
    toggleRow->addWidget(m_shadowCheckBox);
    toggleRow->addWidget(m_addBlendCheckBox);
    layout->addLayout(toggleRow);
    m_constraintComboBox = new QComboBox(this);
    m_outsideParentButton = new QPushButton(translate(m_service, "nanoem.gui.panel.model.outside-parent"), this);
    auto *constraintRow = new QHBoxLayout;
    constraintRow->addWidget(m_constraintComboBox, 1);
    constraintRow->addWidget(m_outsideParentButton);
    layout->addLayout(constraintRow);
    m_constraintOnRadioButton = new QRadioButton(QStringLiteral("ON"), this);
    m_constraintOffRadioButton = new QRadioButton(QStringLiteral("OFF"), this);
    auto *radioRow = new QHBoxLayout;
    radioRow->addWidget(m_constraintOnRadioButton);
    radioRow->addWidget(m_constraintOffRadioButton);
    radioRow->addStretch(1);
    layout->addLayout(radioRow);
    m_registerButton = new QPushButton(translate(m_service, "nanoem.gui.panel.model.register"), this);
    layout->addWidget(m_registerButton);
    connect(m_modelComboBox, &QComboBox::activated, this, [this](int index) {
        Project *project = currentProject();
        if (project) {
            const internal::imgui::ActiveModelSelector selector(project);
            const Model *model = selector.resolve(index);
            m_client->sendSetActiveModelMessage(model ? model->handle() : 0);
        }
    });
    connect(m_nameLineEdit, &QLineEdit::editingFinished, this, [this]() { commitModelName(); });
    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            StringList extensions(Model::loadableExtensions());
            extensions.push_back("txt");
            project->eventPublisher()->publishQueryOpenSingleFileDialogEvent(
                IFileManager::kDialogTypeOpenModelFile, extensions);
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (Model *model = activeModel()) {
            m_service->addModalDialog(ModalDialogFactory::createConfirmDeletingModelDialog(model, m_service));
        }
    });
    connect(m_visibleCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (Model *model = activeModel()) {
            model->setVisible(checked);
        }
    });
    connect(m_shadowCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (Model *model = activeModel()) {
            model->setShadowMapEnabled(checked);
        }
    });
    connect(m_addBlendCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (Model *model = activeModel()) {
            model->setAddBlendEnabled(checked);
        }
    });
    connect(m_constraintComboBox, &QComboBox::activated, this, [this](int index) {
        Model *model = activeModel();
        if (model) {
            const internal::imgui::ConstraintSelector selector(model, m_service->translator());
            model->setActiveConstraint(selector.activeConstraint(index));
        }
    });
    connect(m_outsideParentButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && project->activeModel()) {
            m_service->openModelOutsideParentDialog(project);
        }
    });
    connect(m_constraintOnRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            applyConstraintEnabled(true);
        }
    });
    connect(m_constraintOffRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            applyConstraintEnabled(false);
        }
    });
    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (Model *model = activeModel()) {
            CommandRegistrator registrator(project);
            registrator.registerAddModelKeyframesCommandByCurrentLocalFrameIndex(model);
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

bool
QtModelPanel::eventFilter(QObject *watched, QEvent *event)
{
    /* mirrors the ImGui panel: Ctrl+click on the selector starts editing the model name */
    if (watched == m_modelComboBox && event->type() == QEvent::MouseButtonPress && activeModel()) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->modifiers().testFlag(Qt::ControlModifier)) {
            beginModelNameEditing();
            return true;
        }
    }
    return QGroupBox::eventFilter(watched, event);
}

Project *
QtModelPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

Model *
QtModelPanel::activeModel()
{
    Project *project = currentProject();
    return project ? project->activeModel() : nullptr;
}

void
QtModelPanel::beginModelNameEditing()
{
    if (Model *model = activeModel()) {
        m_editingModelName = true;
        m_nameLineEdit->setText(QString::fromUtf8(model->nameConstString()));
        m_nameStack->setCurrentIndex(1);
        m_nameLineEdit->setFocus();
        m_nameLineEdit->selectAll();
    }
}

void
QtModelPanel::commitModelName()
{
    if (m_editingModelName) {
        m_editingModelName = false;
        if (Model *model = activeModel()) {
            const QByteArray utf8(m_nameLineEdit->text().toUtf8());
            model->setName(String(utf8.constData()));
        }
        m_nameStack->setCurrentIndex(0);
    }
}

void
QtModelPanel::applyConstraintEnabled(bool value)
{
    if (Model *model = activeModel()) {
        if (const nanoem_model_constraint_t *constraintPtr = model->activeConstraint()) {
            model::Constraint::cast(constraintPtr)->setEnabled(value);
            model->performAllBonesTransform();
        }
    }
}

void
QtModelPanel::refreshModelComboBox(Project *project, Model *activeModel)
{
    const internal::imgui::ActiveModelSelector selector(project);
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
    m_modelComboBox->setCurrentIndex(selector.index(activeModel));
}

void
QtModelPanel::refreshConstraintComboBox(Model *activeModel)
{
    const internal::imgui::ConstraintSelector selector(activeModel, m_service->translator());
    const int count = selector.count();
    bool rebuild = m_constraintComboBox->count() != count || m_constraintSelectorModel != activeModel;
    for (int i = 0; !rebuild && i < count; i++) {
        const char *text = nullptr;
        selector.select(i, &text);
        if (m_constraintComboBox->itemText(i) != QString::fromUtf8(text)) {
            rebuild = true;
        }
    }
    if (rebuild) {
        m_constraintComboBox->clear();
        for (int i = 0; i < count; i++) {
            const char *text = nullptr;
            selector.select(i, &text);
            m_constraintComboBox->addItem(QString::fromUtf8(text));
        }
        m_constraintSelectorModel = activeModel;
    }
    m_constraintComboBox->setCurrentIndex(selector.index(activeModel));
}

void
QtModelPanel::refresh()
{
    Project *project = currentProject();
    if (project) {
        Model *activeModel = project->activeModel();
        if (m_editingModelName && !activeModel) {
            m_editingModelName = false;
            m_nameStack->setCurrentIndex(0);
        }
        if (!m_editingModelName) {
            refreshModelComboBox(project, activeModel);
        }
        refreshConstraintComboBox(activeModel);
        const bool hasModel = activeModel != nullptr;
        m_loadButton->setEnabled(!project->isPlaying());
        m_deleteButton->setEnabled(hasModel);
        m_registerButton->setEnabled(hasModel);
        m_outsideParentButton->setEnabled(hasModel);
        m_visibleCheckBox->blockSignals(true);
        m_visibleCheckBox->setChecked(activeModel ? activeModel->isVisible() : false);
        m_visibleCheckBox->setEnabled(hasModel);
        m_visibleCheckBox->blockSignals(false);
        m_shadowCheckBox->blockSignals(true);
        m_shadowCheckBox->setChecked(activeModel ? activeModel->isShadowMapEnabled() : false);
        m_shadowCheckBox->setEnabled(hasModel);
        m_shadowCheckBox->blockSignals(false);
        m_addBlendCheckBox->blockSignals(true);
        m_addBlendCheckBox->setChecked(activeModel ? activeModel->isAddBlendEnabled() : false);
        m_addBlendCheckBox->setEnabled(hasModel);
        m_addBlendCheckBox->blockSignals(false);
        model::Constraint *constraint = nullptr;
        if (activeModel) {
            if (const nanoem_model_constraint_t *constraintPtr = activeModel->activeConstraint()) {
                constraint = model::Constraint::cast(constraintPtr);
            }
        }
        const bool enabled = constraint && constraint->isEnabled();
        m_constraintOnRadioButton->blockSignals(true);
        m_constraintOnRadioButton->setChecked(enabled);
        m_constraintOnRadioButton->setEnabled(constraint != nullptr);
        m_constraintOnRadioButton->blockSignals(false);
        m_constraintOffRadioButton->blockSignals(true);
        m_constraintOffRadioButton->setChecked(!enabled);
        m_constraintOffRadioButton->setEnabled(constraint != nullptr);
        m_constraintOffRadioButton->blockSignals(false);
    }
}

} /* namespace qt */
} /* namespace nanoem */
