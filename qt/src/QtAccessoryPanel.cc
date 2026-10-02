/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtAccessoryPanel.h"
#include "QtApplicationService.h"

#include "emapp/Accessory.h"
#include "emapp/CommandRegistrator.h"
#include "emapp/Constants.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IFileManager.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/ModalDialogFactory.h"
#include "emapp/Project.h"
#include "emapp/internal/AccessoryValueState.h"
#include "emapp/internal/imgui/ActiveAccessorySelector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "glm/gtc/type_ptr.hpp"

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

} /* namespace anonymous */

QtAccessoryPanel::QtAccessoryPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.accessory.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    /* allow the panel to collapse horizontally like the ImGui version */
    setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this);
    m_nameStack = new QStackedWidget(this);
    m_accessoryComboBox = new QComboBox(this);
    m_accessoryComboBox->installEventFilter(this);
    m_nameLineEdit = new QLineEdit(this);
    m_nameStack->addWidget(m_accessoryComboBox);
    m_nameStack->addWidget(m_nameLineEdit);
    layout->addWidget(m_nameStack);
    m_loadButton = new QPushButton(translate(m_service, "nanoem.gui.panel.accessory.load"), this);
    m_deleteButton = new QPushButton(translate(m_service, "nanoem.gui.panel.accessory.delete"), this);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_loadButton);
    buttonRow->addWidget(m_deleteButton);
    layout->addLayout(buttonRow);
    m_visibleCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.accessory.visible"), this);
    m_outsideParentButton = new QPushButton(translate(m_service, "nanoem.gui.panel.accessory.op"), this);
    auto *toggleRow = new QHBoxLayout;
    toggleRow->addWidget(m_visibleCheckBox);
    toggleRow->addStretch(1);
    toggleRow->addWidget(m_outsideParentButton);
    layout->addLayout(toggleRow);
    auto *translationRow = new QHBoxLayout;
    auto *translationLabel = new QLabel(translate(m_service, "nanoem.gui.panel.accessory.translation"), this);
    translationLabel->setToolTip(translationLabel->text());
    translationRow->addWidget(translationLabel);
    for (int i = 0; i < 3; i++) {
        auto *spinBox = new QDoubleSpinBox(this);
        spinBox->setRange(-1000000.0, 1000000.0);
        spinBox->setSingleStep(0.01);
        spinBox->setDecimals(2);
        spinBox->setKeyboardTracking(false);
        spinBox->setToolTip(translationLabel->text());
        translationRow->addWidget(spinBox, 1);
        m_translationSpinBoxes[i] = spinBox;
    }
    layout->addLayout(translationRow);
    auto *orientationRow = new QHBoxLayout;
    auto *orientationLabel = new QLabel(translate(m_service, "nanoem.gui.panel.accessory.orientation"), this);
    orientationLabel->setToolTip(orientationLabel->text());
    orientationRow->addWidget(orientationLabel);
    for (int i = 0; i < 3; i++) {
        auto *spinBox = new QDoubleSpinBox(this);
        spinBox->setRange(-180.0, 180.0);
        spinBox->setSingleStep(0.1);
        spinBox->setDecimals(1);
        spinBox->setKeyboardTracking(false);
        spinBox->setToolTip(orientationLabel->text());
        orientationRow->addWidget(spinBox, 1);
        m_orientationSpinBoxes[i] = spinBox;
    }
    layout->addLayout(orientationRow);
    auto *scaleOpacityRow = new QHBoxLayout;
    scaleOpacityRow->addWidget(new QLabel(translate(m_service, "nanoem.gui.panel.accessory.scale"), this));
    m_scaleSpinBox = new QDoubleSpinBox(this);
    m_scaleSpinBox->setRange(0.01, 1000000.0);
    m_scaleSpinBox->setSingleStep(0.01);
    m_scaleSpinBox->setDecimals(2);
    m_scaleSpinBox->setKeyboardTracking(false);
    m_scaleSpinBox->setToolTip(translate(m_service, "nanoem.gui.panel.accessory.scale"));
    scaleOpacityRow->addWidget(m_scaleSpinBox, 1);
    scaleOpacityRow->addWidget(new QLabel(translate(m_service, "nanoem.gui.panel.accessory.opacity"), this));
    m_opacitySpinBox = new QDoubleSpinBox(this);
    m_opacitySpinBox->setRange(0.0, 1.0);
    m_opacitySpinBox->setSingleStep(0.01);
    m_opacitySpinBox->setDecimals(2);
    m_opacitySpinBox->setKeyboardTracking(false);
    m_opacitySpinBox->setToolTip(translate(m_service, "nanoem.gui.panel.accessory.opacity"));
    scaleOpacityRow->addWidget(m_opacitySpinBox, 1);
    layout->addLayout(scaleOpacityRow);
    m_registerButton = new QPushButton(translate(m_service, "nanoem.gui.panel.accessory.register"), this);
    layout->addWidget(m_registerButton);
    connect(m_accessoryComboBox, &QComboBox::activated, this, [this](int index) {
        Project *project = currentProject();
        if (project) {
            const internal::imgui::ActiveAccessorySelector selector(project);
            project->setActiveAccessory(selector.resolve(index));
            refresh();
        }
    });
    connect(m_nameLineEdit, &QLineEdit::editingFinished, this, [this]() { commitAccessoryName(); });
    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && !project->isPlaying()) {
            StringList extensions(Accessory::loadableExtensions());
            extensions.push_back("txt");
            project->eventPublisher()->publishQueryOpenSingleFileDialogEvent(
                IFileManager::kDialogTypeOpenModelFile, extensions);
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (Accessory *accessory = activeAccessory()) {
            m_service->addModalDialog(ModalDialogFactory::createConfirmDeletingAccessoryDialog(accessory, m_service));
        }
    });
    connect(m_visibleCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (Accessory *accessory = activeAccessory()) {
            accessory->setVisible(checked);
        }
    });
    connect(m_outsideParentButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project && project->activeAccessory()) {
            m_service->openAccessoryOutsideParentDialog(project);
        }
    });
    for (int i = 0; i < 3; i++) {
        connect(m_translationSpinBoxes[i], &QDoubleSpinBox::valueChanged, this,
            [this](double) { handleTranslationChanged(); });
        connect(m_translationSpinBoxes[i], &QDoubleSpinBox::editingFinished, this,
            [this]() { commitTranslation(); });
        connect(m_orientationSpinBoxes[i], &QDoubleSpinBox::valueChanged, this,
            [this](double) { handleOrientationChanged(); });
        connect(m_orientationSpinBoxes[i], &QDoubleSpinBox::editingFinished, this,
            [this]() { commitOrientation(); });
    }
    connect(m_scaleSpinBox, &QDoubleSpinBox::valueChanged, this, &QtAccessoryPanel::handleScaleChanged);
    connect(m_scaleSpinBox, &QDoubleSpinBox::editingFinished, this, &QtAccessoryPanel::commitScale);
    connect(m_opacitySpinBox, &QDoubleSpinBox::valueChanged, this, &QtAccessoryPanel::handleOpacityChanged);
    connect(m_opacitySpinBox, &QDoubleSpinBox::editingFinished, this, &QtAccessoryPanel::commitOpacity);
    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (Accessory *accessory = activeAccessory()) {
            if (project) {
                CommandRegistrator registrator(project);
                registrator.registerAddAccessoryKeyframesCommandByCurrentLocalFrameIndex(accessory);
            }
        }
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

bool
QtAccessoryPanel::eventFilter(QObject *watched, QEvent *event)
{
    /* mirrors the ImGui panel: Ctrl+click on the selector starts editing the accessory name */
    if (watched == m_accessoryComboBox && event->type() == QEvent::MouseButtonPress && activeAccessory()) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->modifiers().testFlag(Qt::ControlModifier)) {
            beginAccessoryNameEditing();
            return true;
        }
    }
    return QGroupBox::eventFilter(watched, event);
}

Project *
QtAccessoryPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

Accessory *
QtAccessoryPanel::activeAccessory()
{
    Project *project = currentProject();
    return project ? project->activeAccessory() : nullptr;
}

void
QtAccessoryPanel::beginAccessoryNameEditing()
{
    if (Accessory *accessory = activeAccessory()) {
        m_editingAccessoryName = true;
        m_nameLineEdit->setText(QString::fromUtf8(accessory->nameConstString()));
        m_nameStack->setCurrentIndex(1);
        m_nameLineEdit->setFocus();
        m_nameLineEdit->selectAll();
    }
}

void
QtAccessoryPanel::commitAccessoryName()
{
    if (m_editingAccessoryName) {
        m_editingAccessoryName = false;
        if (Accessory *accessory = activeAccessory()) {
            const QByteArray utf8(m_nameLineEdit->text().toUtf8());
            accessory->setName(String(utf8.constData()));
        }
        m_nameStack->setCurrentIndex(0);
    }
}

void
QtAccessoryPanel::handleTranslationChanged()
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Accessory *accessory = activeAccessory();
        if (accessory) {
            if (!m_translationState) {
                m_translationState = new internal::AccessoryTranslationVectorValueState(accessory);
                m_stateProject = project;
                m_stateAccessory = accessory;
            }
            const Vector3 value(static_cast<nanoem_f32_t>(m_translationSpinBoxes[0]->value()),
                static_cast<nanoem_f32_t>(m_translationSpinBoxes[1]->value()),
                static_cast<nanoem_f32_t>(m_translationSpinBoxes[2]->value()));
            m_translationState->setValue(glm::value_ptr(value));
        }
    }
}

void
QtAccessoryPanel::handleOrientationChanged()
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Accessory *accessory = activeAccessory();
        if (accessory) {
            if (!m_orientationState) {
                m_orientationState = new internal::AccessoryOrientationVectorValueState(accessory);
                m_stateProject = project;
                m_stateAccessory = accessory;
            }
            const Vector3 degrees(static_cast<nanoem_f32_t>(m_orientationSpinBoxes[0]->value()),
                static_cast<nanoem_f32_t>(m_orientationSpinBoxes[1]->value()),
                static_cast<nanoem_f32_t>(m_orientationSpinBoxes[2]->value()));
            const Vector3 radians(glm::radians(degrees));
            m_orientationState->setValue(glm::value_ptr(radians));
        }
    }
}

void
QtAccessoryPanel::handleScaleChanged(double value)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Accessory *accessory = activeAccessory();
        if (accessory) {
            if (!m_scaleState) {
                m_scaleState = new internal::AccessoryScaleFactorValueState(accessory);
                m_stateProject = project;
                m_stateAccessory = accessory;
            }
            const nanoem_f32_t v = static_cast<nanoem_f32_t>(value);
            m_scaleState->setValue(&v);
        }
    }
}

void
QtAccessoryPanel::handleOpacityChanged(double value)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Accessory *accessory = activeAccessory();
        if (accessory) {
            if (!m_opacityState) {
                m_opacityState = new internal::AccessoryOpacityValueState(accessory);
                m_stateProject = project;
                m_stateAccessory = accessory;
            }
            const nanoem_f32_t v = static_cast<nanoem_f32_t>(value);
            m_opacityState->setValue(&v);
        }
    }
}

void
QtAccessoryPanel::commitStates()
{
    if (!m_translationState && !m_orientationState && !m_scaleState && !m_opacityState) {
        m_stateProject = nullptr;
        m_stateAccessory = nullptr;
    }
}

void
QtAccessoryPanel::commitTranslation()
{
    if (m_translationState) {
        m_translationState->commit();
        delete m_translationState;
        m_translationState = nullptr;
        commitStates();
    }
}

void
QtAccessoryPanel::commitOrientation()
{
    if (m_orientationState) {
        m_orientationState->commit();
        delete m_orientationState;
        m_orientationState = nullptr;
        commitStates();
    }
}

void
QtAccessoryPanel::commitScale()
{
    if (m_scaleState) {
        m_scaleState->commit();
        delete m_scaleState;
        m_scaleState = nullptr;
        commitStates();
    }
}

void
QtAccessoryPanel::commitOpacity()
{
    if (m_opacityState) {
        m_opacityState->commit();
        delete m_opacityState;
        m_opacityState = nullptr;
        commitStates();
    }
}

void
QtAccessoryPanel::validateStates(Project *project)
{
    if (!m_translationState && !m_orientationState && !m_scaleState && !m_opacityState) {
        return;
    }
    const bool stale = m_stateProject != project || project->isPlaying() ||
        static_cast<const Accessory *>(project->activeAccessory()) != m_stateAccessory;
    if (stale) {
        /* the target may be gone: drop without committing, mirroring ImGuiWindow::validateCameraState */
        delete m_translationState;
        m_translationState = nullptr;
        delete m_orientationState;
        m_orientationState = nullptr;
        delete m_scaleState;
        m_scaleState = nullptr;
        delete m_opacityState;
        m_opacityState = nullptr;
        m_stateProject = nullptr;
        m_stateAccessory = nullptr;
    }
}

void
QtAccessoryPanel::refreshAccessoryComboBox(Project *project, Accessory *activeAccessory)
{
    const internal::imgui::ActiveAccessorySelector selector(project);
    const int count = selector.count();
    bool rebuild = m_accessoryComboBox->count() != count;
    for (int i = 0; !rebuild && i < count; i++) {
        const char *text = nullptr;
        selector.select(i, &text);
        if (m_accessoryComboBox->itemText(i) != QString::fromUtf8(text)) {
            rebuild = true;
        }
    }
    if (rebuild) {
        m_accessoryComboBox->clear();
        for (int i = 0; i < count; i++) {
            const char *text = nullptr;
            selector.select(i, &text);
            m_accessoryComboBox->addItem(QString::fromUtf8(text));
        }
    }
    m_accessoryComboBox->setCurrentIndex(selector.index(activeAccessory));
}

void
QtAccessoryPanel::refresh()
{
    Project *project = currentProject();
    Accessory *activeAccessory = project ? project->activeAccessory() : nullptr;
    const bool visible = project != nullptr && project->activeModel() == nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project) {
        return;
    }
    validateStates(project);
    if (m_editingAccessoryName && !activeAccessory) {
        m_editingAccessoryName = false;
        m_nameStack->setCurrentIndex(0);
    }
    if (!m_editingAccessoryName) {
        refreshAccessoryComboBox(project, activeAccessory);
    }
    const bool hasAccessory = activeAccessory != nullptr;
    const bool buttonEnabled = !project->isPlaying();
    m_loadButton->setEnabled(buttonEnabled);
    m_deleteButton->setEnabled(hasAccessory);
    m_outsideParentButton->setEnabled(hasAccessory);
    m_registerButton->setEnabled(hasAccessory);
    m_visibleCheckBox->blockSignals(true);
    m_visibleCheckBox->setChecked(activeAccessory ? activeAccessory->isVisible() : false);
    m_visibleCheckBox->setEnabled(hasAccessory);
    m_visibleCheckBox->blockSignals(false);
    const bool translationEnabled = hasAccessory && buttonEnabled;
    const Vector3 translation(activeAccessory ? activeAccessory->translation() : Constants::kZeroV3);
    const nanoem_f32_t translationValues[3] = { translation.x, translation.y, translation.z };
    for (int i = 0; i < 3; i++) {
        m_translationSpinBoxes[i]->blockSignals(true);
        if (!m_translationState && !m_translationSpinBoxes[i]->hasFocus()) {
            m_translationSpinBoxes[i]->setValue(translationValues[i]);
        }
        m_translationSpinBoxes[i]->setEnabled(translationEnabled || m_translationSpinBoxes[i]->hasFocus());
        m_translationSpinBoxes[i]->blockSignals(false);
    }
    const Vector3 orientation(
        glm::degrees(activeAccessory ? activeAccessory->orientation() : Constants::kZeroV3));
    const nanoem_f32_t orientationValues[3] = { orientation.x, orientation.y, orientation.z };
    for (int i = 0; i < 3; i++) {
        m_orientationSpinBoxes[i]->blockSignals(true);
        if (!m_orientationState && !m_orientationSpinBoxes[i]->hasFocus()) {
            m_orientationSpinBoxes[i]->setValue(orientationValues[i]);
        }
        m_orientationSpinBoxes[i]->setEnabled(translationEnabled || m_orientationSpinBoxes[i]->hasFocus());
        m_orientationSpinBoxes[i]->blockSignals(false);
    }
    m_scaleSpinBox->blockSignals(true);
    if (!m_scaleState && !m_scaleSpinBox->hasFocus()) {
        m_scaleSpinBox->setValue(activeAccessory ? activeAccessory->scaleFactor() : 1.0);
    }
    m_scaleSpinBox->setEnabled(translationEnabled || m_scaleSpinBox->hasFocus());
    m_scaleSpinBox->blockSignals(false);
    m_opacitySpinBox->blockSignals(true);
    if (!m_opacityState && !m_opacitySpinBox->hasFocus()) {
        m_opacitySpinBox->setValue(activeAccessory ? activeAccessory->opacity() : 0.0);
    }
    m_opacitySpinBox->setEnabled(translationEnabled || m_opacitySpinBox->hasFocus());
    m_opacitySpinBox->blockSignals(false);
}

} /* namespace qt */
} /* namespace nanoem */
