/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtEffectParameterDialog.h"
#include "QtApplicationService.h"

#include "emapp/Accessory.h"
#include "emapp/Effect.h"
#include "emapp/Error.h"
#include "emapp/FileUtils.h"
#include "emapp/IDrawable.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IFileManager.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Progress.h"
#include "emapp/URI.h"
#include "emapp/effect/RenderTargetColorImageContainer.h"
#include "emapp/internal/ModelEffectSetting.h"
#include "emapp/internal/IUIWindow.h"
#include "emapp/model/Material.h"
#include "emapp/private/CommonInclude.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cstring>

namespace nanoem {
namespace qt {
namespace {

const int kListItemRoleKind = Qt::UserRole;
const int kListItemRoleKey = Qt::UserRole + 1;
enum ParameterKind {
    kParameterKindDefault,
    kParameterKindUniformBuffer,
    kParameterKindRenderTarget,
    kParameterKindOffscreenRenderTarget,
};

void
clearLayout(QLayout *layout)
{
    QLayoutItem *item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        else if (QLayout *child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

QScrollArea *
createDetailArea(QWidget **outContainer, QVBoxLayout **outLayout, QWidget *parent)
{
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget(area);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    area->setWidget(container);
    *outContainer = container;
    *outLayout = layout;
    return area;
}

QWidget *
createSeparator(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::HLine);
    frame->setFrameShadow(QFrame::Sunken);
    return frame;
}

nanoem_rsize_t
parameterComponentCount(effect::ParameterType type)
{
    if (type >= effect::kParameterTypeFloat && type <= effect::kParameterTypeFloat4) {
        return nanoem_rsize_t(type) - nanoem_rsize_t(effect::kParameterTypeFloat) + 1;
    }
    if (type >= effect::kParameterTypeInt && type <= effect::kParameterTypeInt4) {
        return nanoem_rsize_t(type) - nanoem_rsize_t(effect::kParameterTypeInt) + 1;
    }
    return 0;
}

/* sokol exposes no image -> GL texture accessor, so the preview box reports what ImGui::Image
   would have drawn instead of the pixels (see the class comment) */
QWidget *
createTexturePlaceholder(const String &name, const sg_image_desc &desc, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setFrameShadow(QFrame::Sunken);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(2);
    auto *sizeLabel = new QLabel(
        QString::fromUtf8("%1 x %2").arg(desc.width).arg(desc.height), frame);
    sizeLabel->setAlignment(Qt::AlignCenter);
    QFont font(sizeLabel->font());
    font.setBold(true);
    sizeLabel->setFont(font);
    layout->addWidget(sizeLabel);
    auto *note = new QLabel(QString::fromUtf8("%1\n%2")
                                .arg(QString::fromUtf8(name.c_str()),
                                    QStringLiteral("texture preview is not available in the Qt shell")),
        frame);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    note->setAlignment(Qt::AlignCenter);
    layout->addWidget(note);
    return frame;
}

} /* namespace anonymous */

QtEffectParameterDialog::QtEffectParameterDialog(
    QtApplicationService *service, Project *project, internal::IUIWindow *window, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_window(window)
    , m_activeOffscreenRenderTargetIndex(0)
    , m_activeModelTargetIndex(-1)
    , m_activeParameterIndex(0)
    , m_uniformBuffersBoundIndex(1)
    , m_renderTargetsBoundIndex(1)
{
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createOffscreenTab(), translate("nanoem.gui.window.project.effect.tab.offscreen"));
    m_tabs->addTab(createModelTab(), translate("nanoem.gui.window.project.effect.tab.model"));
    m_tabs->addTab(createParameterTab(), translate("nanoem.gui.window.project.effect.tab.parameter"));
    layout->addWidget(m_tabs);
    m_tabs->setMinimumSize(600, 400);
    resize(800, 640);
    /* ImGuiWindow::drawEffectParameterWindow only creates the Parameter tab when the active
       drawable resolves to an effect */
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0) {
            rebuildOffscreenList();
        }
        else if (index == 1) {
            rebuildModelList();
        }
        else if (index == 2) {
            rebuildParameterList();
        }
    });
}

void
QtEffectParameterDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    rebuildAll();
}

QWidget *
QtEffectParameterDialog::createOffscreenTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    m_offscreenList = new QListWidget(widget);
    m_offscreenList->setFixedWidth(200);
    layout->addWidget(m_offscreenList);
    QWidget *container = nullptr;
    QVBoxLayout *containerLayout = nullptr;
    layout->addWidget(createDetailArea(&container, &containerLayout, widget), 1);
    m_offscreenDetail = container;
    m_offscreenDetailLayout = containerLayout;
    connect(m_offscreenList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_activeOffscreenRenderTargetIndex = row;
            rebuildOffscreenDetail();
        }
    });
    return widget;
}

QWidget *
QtEffectParameterDialog::createModelTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *leftColumn = new QWidget(widget);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_modelList = new QListWidget(leftColumn);
    leftLayout->addWidget(m_modelList, 1);
    auto *buttonRow = new QHBoxLayout;
    m_loadEmdButton = new QPushButton(translate("nanoem.gui.window.project.effect.emd.load"), leftColumn);
    m_saveEmdButton = new QPushButton(translate("nanoem.gui.window.project.effect.emd.save"), leftColumn);
    buttonRow->addWidget(m_loadEmdButton);
    buttonRow->addWidget(m_saveEmdButton);
    leftLayout->addLayout(buttonRow);
    leftColumn->setFixedWidth(200);
    layout->addWidget(leftColumn);
    QWidget *container = nullptr;
    QVBoxLayout *containerLayout = nullptr;
    layout->addWidget(createDetailArea(&container, &containerLayout, widget), 1);
    m_modelDetail = container;
    m_modelDetailLayout = containerLayout;
    connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_activeModelTargetIndex = row;
        rebuildModelDetail();
    });
    connect(m_loadEmdButton, &QPushButton::clicked, this, [this]() {
        const Project::ModelList *models = m_project->allModels();
        if (m_activeModelTargetIndex >= 0 && m_activeModelTargetIndex < int(models->size())) {
            Model *model = models->data()[m_activeModelTargetIndex];
            const IFileManager::QueryFileDialogCallbacks callbacks = { model, handleLoadingModelEffectSetting,
                nullptr, nullptr };
            m_project->fileManager()->setTransientQueryFileDialogCallback(callbacks);
            StringList extensions;
            extensions.push_back("emd");
            m_project->eventPublisher()->publishQueryOpenSingleFileDialogEvent(
                IFileManager::kDialogTypeUserCallback, extensions);
        }
    });
    connect(m_saveEmdButton, &QPushButton::clicked, this, [this]() {
        const Project::ModelList *models = m_project->allModels();
        if (m_activeModelTargetIndex >= 0 && m_activeModelTargetIndex < int(models->size())) {
            Model *model = models->data()[m_activeModelTargetIndex];
            const IFileManager::QueryFileDialogCallbacks callbacks = { model, handleSaveingModelEffectSetting,
                nullptr, nullptr };
            m_project->fileManager()->setTransientQueryFileDialogCallback(callbacks);
            StringList extensions;
            extensions.push_back("emd");
            m_project->eventPublisher()->publishQuerySaveFileDialogEvent(
                IFileManager::kDialogTypeUserCallback, extensions);
        }
    });
    return widget;
}

QWidget *
QtEffectParameterDialog::createParameterTab()
{
    auto *widget = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tools = new QHBoxLayout;
    m_reloadButton = new QPushButton(translate("nanoem.gui.window.project.effect.parameter.reload"), widget);
    connect(m_reloadButton, &QPushButton::clicked, this, [this]() {
        IDrawable *drawable = nullptr;
        if (Effect *effect = currentEffect(&drawable)) {
            if (!effect->fileURI().hasFragment()) {
                m_window->reloadActiveEffect(drawable);
            }
        }
    });
    tools->addWidget(m_reloadButton);
    m_inspectCheckBox = new QCheckBox(translate("nanoem.gui.window.project.effect.parameter.inspect"), widget);
    connect(m_inspectCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (Effect *effect = currentEffect(nullptr)) {
            effect->setPassUniformBufferInspectionEnabled(checked);
            m_activeParameterIndex = 0;
            rebuildParameterList();
        }
    });
    tools->addWidget(m_inspectCheckBox);
    tools->addStretch(1);
    layout->addLayout(tools);
    auto *body = new QWidget(widget);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_parameterList = new QListWidget(body);
    m_parameterList->setFixedWidth(200);
    bodyLayout->addWidget(m_parameterList);
    QWidget *container = nullptr;
    QVBoxLayout *containerLayout = nullptr;
    bodyLayout->addWidget(createDetailArea(&container, &containerLayout, body), 1);
    layout->addWidget(body, 1);
    m_parameterDetail = container;
    m_parameterDetailLayout = containerLayout;
    connect(m_parameterList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_activeParameterIndex = row;
            rebuildParameterDetail();
        }
    });
    return widget;
}

Effect *
QtEffectParameterDialog::currentEffect(IDrawable **drawable) const
{
    Effect *effect = nullptr;
    IDrawable *drawablePtr = nullptr;
    if (Effect *modelEffect = m_project->resolveEffect(m_project->activeModel())) {
        effect = modelEffect;
        drawablePtr = m_project->activeModel();
    }
    else if (Effect *accessoryEffect = m_project->resolveEffect(m_project->activeAccessory())) {
        effect = accessoryEffect;
        drawablePtr = m_project->activeAccessory();
    }
    if (drawable) {
        *drawable = drawablePtr;
    }
    return effect;
}

void
QtEffectParameterDialog::rebuildAll()
{
    IDrawable *drawable = nullptr;
    Effect *effect = currentEffect(&drawable);
    const QString title = drawable
        ? QStringLiteral("%1 - %2")
              .arg(QString::fromUtf8(translate("nanoem.gui.window.project.effect.title").toUtf8().constData()),
                  QString::fromUtf8(drawable->nameConstString()))
        : translate("nanoem.gui.window.project.effect.title");
    setWindowTitle(title);
    m_tabs->setTabVisible(2, effect != nullptr);
    rebuildOffscreenList();
    rebuildModelList();
    if (effect) {
        rebuildParameterList();
    }
}

void
QtEffectParameterDialog::rebuildOffscreenList()
{
    const int previous = m_activeOffscreenRenderTargetIndex;
    m_offscreenOptions.clear();
    {
        const QSignalBlocker blocker(m_offscreenList);
        m_offscreenList->clear();
        m_offscreenList->addItem(QString::fromUtf8(Effect::kOffscreenOwnerNameMain.c_str()));
        const Project::DrawableList *drawables = m_project->drawableOrderList();
        for (Project::DrawableList::const_iterator it = drawables->begin(), end = drawables->end(); it != end; ++it) {
            IDrawable *drawable = *it;
            if (const Effect *effect = m_project->resolveEffect(drawable)) {
                effect::OffscreenRenderTargetOptionList options;
                effect->getAllOffscreenRenderTargetOptions(options);
                for (effect::OffscreenRenderTargetOptionList::const_iterator it2 = options.begin(),
                                                                             end2 = options.end();
                     it2 != end2; ++it2) {
                    if (drawable->isVisible()) {
                        m_offscreenOptions.push_back(*it2);
                    }
                }
            }
        }
        for (effect::OffscreenRenderTargetOptionList::const_iterator it = m_offscreenOptions.begin(),
                                                                     end = m_offscreenOptions.end();
             it != end; ++it) {
            m_offscreenList->addItem(QString::fromUtf8(it->m_name.c_str()));
        }
    }
    const int count = m_offscreenList->count();
    m_activeOffscreenRenderTargetIndex = qBound(0, previous, qMax(0, count - 1));
    m_offscreenList->setCurrentRow(m_activeOffscreenRenderTargetIndex);
    rebuildOffscreenDetail();
}

void
QtEffectParameterDialog::rebuildOffscreenDetail()
{
    clearLayout(m_offscreenDetailLayout);
    const int index = m_activeOffscreenRenderTargetIndex;
    const bool hasOption =
        index > 0 && index <= int(m_offscreenOptions.size());
    const effect::OffscreenRenderTargetOption *option = hasOption ? &m_offscreenOptions[index - 1] : nullptr;
    auto *description = new QLabel(option ? QString::fromUtf8(option->m_description.c_str())
                                          : QString::fromUtf8("Main Render Target"),
        m_offscreenDetail);
    description->setWordWrap(true);
    description->setMinimumHeight(description->fontMetrics().lineSpacing() * 3);
    m_offscreenDetailLayout->addWidget(description);
    const String ownerName(option ? option->m_name : Effect::kOffscreenOwnerNameMain);
    const Project::DrawableList *drawables = m_project->drawableOrderList();
    for (Project::DrawableList::const_iterator it = drawables->begin(), end = drawables->end(); it != end; ++it) {
        appendDrawableRow(m_offscreenDetailLayout, *it, ownerName, option == nullptr);
    }
    if (option) {
        m_offscreenDetailLayout->addWidget(createSeparator(m_offscreenDetail));
        auto *title = new QLabel(
            translate("nanoem.gui.window.project.effect.offscreen.display-texture"), m_offscreenDetail);
        QFont font(title->font());
        font.setBold(true);
        title->setFont(font);
        m_offscreenDetailLayout->addWidget(title);
        m_offscreenDetailLayout->addWidget(
            createTexturePlaceholder(option->m_name, option->m_colorImageDescription, m_offscreenDetail));
    }
    m_offscreenDetailLayout->addStretch(1);
}

void
QtEffectParameterDialog::appendDrawableRow(
    QVBoxLayout *layout, IDrawable *drawable, const String &ownerName, bool mainRenderTarget)
{
    auto *row = new QWidget(m_offscreenDetail);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);
    const Effect *effect = m_project->upcastEffect(drawable->findOffscreenPassiveRenderTargetEffect(ownerName));
    const bool selectable = drawable->activeEffect() != effect ||
        (effect && effect->scriptOrder() == IEffect::kScriptOrderTypeStandard);
    if (mainRenderTarget) {
        auto *visible = new QCheckBox(row);
        visible->setToolTip(QStringLiteral("Visible"));
        visible->setChecked(drawable->isVisible());
        connect(visible, &QCheckBox::toggled, this, [drawable](bool checked) { drawable->setVisible(checked); });
        rowLayout->addWidget(visible);
    }
    else {
        auto *enabled = new QCheckBox(row);
        enabled->setToolTip(QStringLiteral("Enable offscreen passive render target effect"));
        enabled->setChecked(drawable->isOffscreenPassiveRenderTargetEffectEnabled(ownerName));
        enabled->setEnabled(effect != nullptr);
        connect(enabled, &QCheckBox::toggled, this, [this, drawable, ownerName](bool checked) {
            drawable->setOffscreenPassiveRenderTargetEffectEnabled(ownerName, checked);
            m_project->forceResetAllPasses();
        });
        rowLayout->addWidget(enabled);
    }
    /* the ImGui Selectable toggles the "drawable to attach" set; a checkable row is the natural
       Qt mapping of the same multi select (no Shift modifier needed) */
    auto *attach = new QCheckBox(QString::fromUtf8(drawable->nameConstString()), row);
    attach->setChecked(m_project->containsDrawableToAttachOffscreenRenderTargetEffect(ownerName, drawable));
    attach->setEnabled(selectable);
    connect(attach, &QCheckBox::toggled, this, [this, drawable, ownerName](bool checked) {
        if (checked) {
            m_project->addDrawableToAttachOffscreenRenderTargetEffect(ownerName, drawable);
        }
        else {
            m_project->removeDrawableToAttachOffscreenRenderTargetEffect(ownerName, drawable);
        }
    });
    rowLayout->addWidget(attach, 1);
    auto *openButton = new QPushButton(row);
    if (effect) {
        const URI &fileURI = effect->fileURI();
        const String &filename =
            fileURI.hasFragment() ? URI::lastPathComponent(fileURI.fragment()) : fileURI.lastPathComponent();
        openButton->setText(QString::fromUtf8(filename.c_str()));
        openButton->setToolTip(QString::fromUtf8(fileURI.absolutePathConstString()));
    }
    else {
        openButton->setText(translate("nanoem.gui.window.project.effect.offscreen.none"));
    }
    openButton->setEnabled(effect != nullptr ? selectable : true);
    connect(openButton, &QPushButton::clicked, this, [this, drawable, ownerName, effect]() {
        if (!effect) {
            m_project->clearAllDrawablesToAttachOffscreenRenderTargetEffect(ownerName);
        }
        m_project->addDrawableToAttachOffscreenRenderTargetEffect(ownerName, drawable);
        queryFileDialog(IFileManager::kDialogTypeOpenEffectFile);
    });
    rowLayout->addWidget(openButton, 2);
    auto *clearButton = new QPushButton(QStringLiteral("×"), row);
    clearButton->setFixedWidth(28);
    clearButton->setToolTip(QStringLiteral("Clear attached effect"));
    clearButton->setEnabled(selectable && effect != nullptr);
    connect(clearButton, &QPushButton::clicked, this,
        [drawable, ownerName]() { drawable->removeOffscreenPassiveRenderTargetEffect(ownerName); });
    rowLayout->addWidget(clearButton);
    layout->addWidget(row);
}

void
QtEffectParameterDialog::rebuildModelList()
{
    const int previous = m_activeModelTargetIndex;
    const Project::ModelList *models = m_project->allModels();
    {
        const QSignalBlocker blocker(m_modelList);
        m_modelList->clear();
        for (Project::ModelList::const_iterator it = models->begin(), end = models->end(); it != end; ++it) {
            m_modelList->addItem(QString::fromUtf8((*it)->nameConstString()));
        }
    }
    const int count = m_modelList->count();
    m_activeModelTargetIndex = qBound(-1, previous, count - 1);
    m_modelList->setCurrentRow(m_activeModelTargetIndex);
    rebuildModelDetail();
}

void
QtEffectParameterDialog::rebuildModelDetail()
{
    clearLayout(m_modelDetailLayout);
    const Project::ModelList *models = m_project->allModels();
    const bool modelSelected = m_activeModelTargetIndex >= 0 && m_activeModelTargetIndex < int(models->size());
    m_loadEmdButton->setEnabled(modelSelected);
    m_saveEmdButton->setEnabled(modelSelected);
    if (modelSelected) {
        Model *model = models->data()[m_activeModelTargetIndex];
        nanoem_rsize_t numMaterials = 0;
        (void)nanoemModelGetAllMaterialObjects(model->data(), &numMaterials);
        for (nanoem_rsize_t i = 0; i < numMaterials; i++) {
            appendMaterialRow(m_modelDetailLayout, model, i);
        }
    }
    m_modelDetailLayout->addStretch(1);
}

void
QtEffectParameterDialog::appendMaterialRow(QVBoxLayout *layout, Model *model, nanoem_rsize_t materialIndex)
{
    nanoem_rsize_t numMaterials = 0;
    nanoem_model_material_t *const *materials = nanoemModelGetAllMaterialObjects(model->data(), &numMaterials);
    if (materialIndex >= numMaterials) {
        return;
    }
    nanoem_model_material_t *materialPtr = materials[materialIndex];
    model::Material *material = model::Material::cast(materialPtr);
    const nanoem_u16_t modelHandle = model->handle();
    auto *row = new QWidget(m_modelDetail);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);
    auto *attach = new QCheckBox(QString::fromUtf8(material->nameConstString()), row);
    attach->setChecked(m_project->containsIndexOfMaterialToAttachEffect(modelHandle, materialIndex));
    connect(attach, &QCheckBox::toggled, this, [this, modelHandle, materialIndex](bool checked) {
        if (checked) {
            m_project->addIndexOfMaterialToAttachEffect(modelHandle, materialIndex);
        }
        else {
            m_project->removeIndexOfMaterialToAttachEffect(modelHandle, materialIndex);
        }
    });
    rowLayout->addWidget(attach, 1);
    const Effect *effect = material->effect();
    auto *openButton = new QPushButton(row);
    if (effect) {
        openButton->setText(QString::fromUtf8(effect->nameConstString()));
        openButton->setToolTip(QString::fromUtf8(effect->fileURI().absolutePathConstString()));
    }
    else {
        openButton->setText(translate("nanoem.gui.window.project.effect.material.none"));
    }
    connect(openButton, &QPushButton::clicked, this, [this, modelHandle, materialIndex]() {
        m_project->clearAllIndicesOfMaterialToAttachEffect(modelHandle);
        m_project->addIndexOfMaterialToAttachEffect(modelHandle, materialIndex);
        queryFileDialog(IFileManager::kDialogTypeOpenEffectFile);
    });
    rowLayout->addWidget(openButton, 2);
    auto *clearButton = new QPushButton(QStringLiteral("×"), row);
    clearButton->setFixedWidth(28);
    clearButton->setToolTip(QStringLiteral("Clear attached effect"));
    clearButton->setEnabled(effect != nullptr);
    connect(clearButton, &QPushButton::clicked, this, [materialPtr]() {
        if (model::Material *material = model::Material::cast(materialPtr)) {
            material->setEffect(nullptr);
        }
    });
    rowLayout->addWidget(clearButton);
    layout->addWidget(row);
}

void
QtEffectParameterDialog::rebuildParameterList()
{
    clearLayout(m_parameterDetailLayout);
    m_uniformBuffers.clear();
    m_renderTargets.clear();
    m_offscreenRenderTargets.clear();
    const int previous = m_activeParameterIndex;
    Effect *effect = currentEffect(nullptr);
    {
        const QSignalBlocker reloadBlocker(m_reloadButton);
        const QSignalBlocker inspectBlocker(m_inspectCheckBox);
        m_reloadButton->setEnabled(effect != nullptr && !effect->fileURI().hasFragment());
        m_inspectCheckBox->setChecked(effect ? effect->isPassUniformBufferInspectionEnabled() : false);
    }
    {
        const QSignalBlocker blocker(m_parameterList);
        m_parameterList->clear();
        if (!effect) {
            m_activeParameterIndex = 0;
            return;
        }
        auto *first = new QListWidgetItem(QStringLiteral("Default"));
        first->setData(kListItemRoleKind, kParameterKindDefault);
        m_parameterList->addItem(first);
        if (effect->isPassUniformBufferInspectionEnabled()) {
            effect->getPassUniformBuffer(m_uniformBuffers);
            for (Effect::PassUniformBufferMap::const_iterator it = m_uniformBuffers.begin(),
                                                               end = m_uniformBuffers.end();
                 it != end; ++it) {
                auto *item = new QListWidgetItem(QString::fromUtf8(it->first.c_str()));
                item->setData(kListItemRoleKind, kParameterKindUniformBuffer);
                item->setData(kListItemRoleKey, QString::fromUtf8(it->first.c_str()));
                m_parameterList->addItem(item);
            }
        }
        m_uniformBuffersBoundIndex = m_parameterList->count();
        effect->getAllRenderTargetImageContainers(m_renderTargets);
        for (Effect::NamedRenderTargetColorImageContainerMap::const_iterator it = m_renderTargets.begin(),
                                                                             end = m_renderTargets.end();
             it != end; ++it) {
            auto *item = new QListWidgetItem(QString::fromUtf8(it->first.c_str()));
            item->setData(kListItemRoleKind, kParameterKindRenderTarget);
            item->setData(kListItemRoleKey, QString::fromUtf8(it->first.c_str()));
            m_parameterList->addItem(item);
        }
        m_renderTargetsBoundIndex = m_parameterList->count();
        effect->getAllOffscreenRenderTargetOptions(m_offscreenRenderTargets);
        for (effect::OffscreenRenderTargetOptionList::const_iterator it = m_offscreenRenderTargets.begin(),
                                                                     end = m_offscreenRenderTargets.end();
             it != end; ++it) {
            auto *item = new QListWidgetItem(QString::fromUtf8(it->m_name.c_str()));
            item->setData(kListItemRoleKind, kParameterKindOffscreenRenderTarget);
            item->setData(kListItemRoleKey, QString::fromUtf8(it->m_name.c_str()));
            m_parameterList->addItem(item);
        }
    }
    const int count = m_parameterList->count();
    m_activeParameterIndex = count > 0 ? qBound(0, previous, count - 1) : 0;
    m_parameterList->setCurrentRow(m_activeParameterIndex);
    rebuildParameterDetail();
}

void
QtEffectParameterDialog::rebuildParameterDetail()
{
    clearLayout(m_parameterDetailLayout);
    QListWidgetItem *item = m_parameterList->currentItem();
    if (!item) {
        return;
    }
    const int kind = item->data(kListItemRoleKind).toInt();
    const String key(item->data(kListItemRoleKey).toString().toUtf8().constData());
    switch (kind) {
    case kParameterKindOffscreenRenderTarget: {
        for (effect::OffscreenRenderTargetOptionList::const_iterator it = m_offscreenRenderTargets.begin(),
                                                                     end = m_offscreenRenderTargets.end();
             it != end; ++it) {
            if (it->m_name == key) {
                auto *name = new QLabel(QString::fromUtf8(it->m_name.c_str()), m_parameterDetail);
                QFont font(name->font());
                font.setBold(true);
                name->setFont(font);
                m_parameterDetailLayout->addWidget(name);
                auto *description = new QLabel(QString::fromUtf8(it->m_description.c_str()), m_parameterDetail);
                description->setWordWrap(true);
                m_parameterDetailLayout->addWidget(description);
                m_parameterDetailLayout->addWidget(createTexturePlaceholder(
                    it->m_name, it->m_colorImageDescription, m_parameterDetail));
                break;
            }
        }
        break;
    }
    case kParameterKindRenderTarget: {
        Effect::NamedRenderTargetColorImageContainerMap::const_iterator it = m_renderTargets.find(key);
        if (it != m_renderTargets.end()) {
            const effect::RenderTargetColorImageContainer *container = it->second;
            auto *name = new QLabel(QString::fromUtf8(it->first.c_str()), m_parameterDetail);
            QFont font(name->font());
            font.setBold(true);
            name->setFont(font);
            m_parameterDetailLayout->addWidget(name);
            m_parameterDetailLayout->addWidget(createTexturePlaceholder(
                it->first, container->colorImageDescription(), m_parameterDetail));
        }
        else {
            m_parameterDetailLayout->addWidget(new QLabel(QStringLiteral("(Unknown)"), m_parameterDetail));
        }
        break;
    }
    case kParameterKindUniformBuffer: {
        Effect::PassUniformBufferMap::const_iterator it = m_uniformBuffers.find(key);
        if (it != m_uniformBuffers.end() && !m_uniformBuffers.empty()) {
            const Effect::NamedByteArrayMap &map = it->second;
            QStringList parameterNames;
            for (Effect::NamedByteArrayMap::const_iterator it2 = map.begin(), end2 = map.end(); it2 != end2; ++it2) {
                parameterNames.append(QString::fromUtf8(it2->first.c_str()));
            }
            /* ImGui sorts the parameter names before laying them out */
            parameterNames.sort();
            for (const QString &parameterName : parameterNames) {
                const String key2(parameterName.toUtf8().constData());
                const Effect::NamedByteArrayMap::const_iterator entry = map.find(key2);
                if (entry != map.end()) {
                    appendUniformBufferDetail(entry->first, entry->second);
                }
            }
        }
        break;
    }
    default: {
        Effect *effect = currentEffect(nullptr);
        if (effect) {
            effect::UIWidgetParameterList parameters;
            effect->getAllUIWidgetParameters(parameters);
            for (effect::UIWidgetParameterList::const_iterator it = parameters.begin(), end = parameters.end();
                 it != end; ++it) {
                appendParameterEditor(&(*it));
            }
        }
        break;
    }
    }
    m_parameterDetailLayout->addStretch(1);
}

void
QtEffectParameterDialog::appendUniformBufferDetail(const String &name, const ByteArray &bytes)
{
    auto *label = new QLabel(QString::fromUtf8(name.c_str()), m_parameterDetail);
    label->setStyleSheet(QStringLiteral("color: #ffff00;"));
    m_parameterDetailLayout->addWidget(label);
    switch (bytes.size()) {
    case 64: {
        Matrix4x4 v;
        /* the uniform buffer is byte aligned only in theory: copy instead of reinterpret_cast */
        memcpy(&v, bytes.data(), 64);
        for (int i = 0; i < 4; i++) {
            const Vector4 row(v[i]);
            m_parameterDetailLayout->addWidget(
                new QLabel(QString::fromUtf8("%1, %2, %3, %4")
                               .arg(row.x, 0, 'f', 6)
                               .arg(row.y, 0, 'f', 6)
                               .arg(row.z, 0, 'f', 6)
                               .arg(row.w, 0, 'f', 6),
                    m_parameterDetail));
        }
        break;
    }
    case 16: {
        Vector4 vec;
        memcpy(&vec, bytes.data(), 16);
        const Vector4 *v = &vec;
        const Vector4::value_type e = glm::epsilon<Vector4::value_type>();
        QString text;
        if (glm::all(glm::epsilonEqual(*v, Vector4(v->x), e))) {
            text = QString::fromUtf8("%1").arg(v->x, 0, 'f', 6);
        }
        else if (glm::epsilonEqual(v->z, 0.0f, e) && glm::epsilonEqual(v->w, 0.0f, e)) {
            text = QString::fromUtf8("%1, %2").arg(v->x, 0, 'f', 3).arg(v->y, 0, 'f', 3);
        }
        else if (glm::epsilonEqual(v->w, 0.0f, e)) {
            text = QString::fromUtf8("%1, %2, %3").arg(v->x, 0, 'f', 3).arg(v->y, 0, 'f', 3).arg(v->z, 0, 'f', 3);
        }
        else {
            text = QString::fromUtf8("%1, %2, %3, %4")
                       .arg(v->x, 0, 'f', 3)
                       .arg(v->y, 0, 'f', 3)
                       .arg(v->z, 0, 'f', 3)
                       .arg(v->w, 0, 'f', 3);
        }
        m_parameterDetailLayout->addWidget(new QLabel(text, m_parameterDetail));
        break;
    }
    default:
        if (bytes.size() % sizeof(Vector4) == 0) {
            const nanoem_rsize_t numComponents = bytes.size() / sizeof(Vector4);
            const Vector4 *components = reinterpret_cast<const Vector4 *>(bytes.data());
            for (nanoem_rsize_t i = 0; i < numComponents; i++) {
                const Vector4 v(components[i]);
                m_parameterDetailLayout->addWidget(
                    new QLabel(QString::fromUtf8("[%1] (%2, %3, %4, %5)")
                                   .arg(i)
                                   .arg(v.x, 0, 'f', 6)
                                   .arg(v.y, 0, 'f', 6)
                                   .arg(v.z, 0, 'f', 6)
                                   .arg(v.w, 0, 'f', 6),
                        m_parameterDetail));
            }
        }
        else if (bytes.size() % sizeof(nanoem_f32_t) == 0) {
            const nanoem_rsize_t numComponents = bytes.size() / sizeof(nanoem_f32_t);
            const nanoem_f32_t *components = reinterpret_cast<const nanoem_f32_t *>(bytes.data());
            for (nanoem_rsize_t i = 0; i < numComponents; i++) {
                m_parameterDetailLayout->addWidget(new QLabel(
                    QString::fromUtf8("[%1] %2").arg(i).arg(components[i], 0, 'f', 6), m_parameterDetail));
            }
        }
        break;
    }
}

void
QtEffectParameterDialog::appendParameterEditor(const effect::UIWidgetParameter *parameter)
{
    if (!parameter->m_visible) {
        return;
    }
    const QString name = QString::fromUtf8(parameter->m_name.c_str());
    /* the value lives in the effect's own storage (the parameter list is only a descriptor copy),
       so capture the pointer and not a local reference to it */
    Vector4 *values = parameter->m_valuePtr->data();
    m_parameterDetailLayout->addWidget(new QLabel(name, m_parameterDetail));
    switch (parameter->m_widgetType) {
    case effect::UIWidgetParameter::kUIWidgetTypeCheckbox: {
        auto *checkBox = new QCheckBox(name, m_parameterDetail);
        checkBox->setChecked(values->x != 0);
        connect(checkBox, &QCheckBox::toggled, this, [values](bool checked) { values->x = checked ? 1.0f : 0.0f; });
        m_parameterDetailLayout->addWidget(checkBox);
        break;
    }
    case effect::UIWidgetParameter::kUIWidgetTypeColor: {
        auto *button = new QPushButton(m_parameterDetail);
        button->setFixedHeight(28);
        const QColor current = QColor::fromRgbF(values->x, values->y, values->z);
        button->setStyleSheet(QString::fromUtf8("background-color: %1;").arg(current.name(QColor::HexRgb)));
        connect(button, &QPushButton::clicked, this, [this, values, button]() {
            QColor color;
            if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
                static int s_offscreenColorCycle = 0;
                static const QColor s_offscreenColors[] = {
                    QColor(255, 0, 0, 255),
                    QColor(0, 255, 0, 255),
                    QColor(0, 0, 255, 255),
                    QColor(255, 255, 0, 255),
                };
                color = s_offscreenColors[s_offscreenColorCycle % 4];
                s_offscreenColorCycle++;
            }
            else {
                color = QColorDialog::getColor(QColor::fromRgbF(values->x, values->y, values->z), button);
            }
            if (color.isValid()) {
                *values = Vector4(color.redF(), color.greenF(), color.blueF(), values->w);
                button->setStyleSheet(QString::fromUtf8("background-color: %1;").arg(color.name(QColor::HexRgb)));
            }
        });
        m_parameterDetailLayout->addWidget(button);
        break;
    }
    case effect::UIWidgetParameter::kUIWidgetTypeNumeric:
    case effect::UIWidgetParameter::kUIWidgetTypeSlider:
    case effect::UIWidgetParameter::kUIWidgetTypeSpinner: {
        const nanoem_rsize_t count = parameterComponentCount(parameter->m_parameterType);
        if (count == 0) {
            break;
        }
        const bool integer = parameter->m_parameterType >= effect::kParameterTypeInt &&
            parameter->m_parameterType <= effect::kParameterTypeInt4;
        /* only slider/numeric carry a usable min/max: ImGuiWindow's DragFloat4 passes the range as
           (speed, min, max-default) so the spinner is left unclamped, like InputFloat4 */
        const bool bounded = parameter->m_widgetType != effect::UIWidgetParameter::kUIWidgetTypeNumeric;
        const glm::ivec2 range(parameter->range);
        auto *row = new QWidget(m_parameterDetail);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        for (nanoem_rsize_t i = 0; i < count; i++) {
            const int index = int(i);
            if (integer) {
                auto *spinBox = new QSpinBox(row);
                if (bounded) {
                    spinBox->setRange(range.x, range.y);
                }
                else {
                    spinBox->setRange(-999999, 999999);
                }
                spinBox->setValue(int((*values)[index]));
                connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [values, index](int v) {
                    Vector4SI32 updated(*values);
                    updated[index] = v;
                    *values = Vector4(updated);
                });
                rowLayout->addWidget(spinBox, 1);
            }
            else {
                auto *spinBox = new QDoubleSpinBox(row);
                spinBox->setDecimals(3);
                if (bounded) {
                    spinBox->setRange(parameter->range.x, parameter->range.y);
                }
                else {
                    spinBox->setRange(-1.0e9, 1.0e9);
                }
                spinBox->setSingleStep(
                    parameter->m_widgetType == effect::UIWidgetParameter::kUIWidgetTypeSpinner ? 0.01 : 0.1);
                spinBox->setValue((*values)[index]);
                connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [values, index](double v) { (*values)[index] = nanoem_f32_t(v); });
                rowLayout->addWidget(spinBox, 1);
            }
        }
        m_parameterDetailLayout->addWidget(row);
        break;
    }
    default:
        break;
    }
}

void
QtEffectParameterDialog::queryFileDialog(IFileManager::DialogType type)
{
    m_project->eventPublisher()->publishQueryOpenSingleFileDialogEvent(type, Effect::loadableExtensions());
}

void
QtEffectParameterDialog::handleLoadingModelEffectSetting(
    const URI &fileURI, Project *project, Error &error, void *userData)
{
    FileReaderScope scope(project->translator());
    if (scope.open(fileURI, error)) {
        ByteArray bytes;
        Progress progress(project, 0);
        FileUtils::read(scope, bytes, error);
        Model *activeModel = static_cast<Model *>(userData);
        bytes.push_back(0);
        internal::ModelEffectSetting settings(project);
        settings.load(reinterpret_cast<const char *>(bytes.data()), activeModel, progress, error);
    }
}

void
QtEffectParameterDialog::handleSaveingModelEffectSetting(
    const URI &fileURI, Project *project, Error &error, void *userData)
{
    const Model *activeModel = static_cast<const Model *>(userData);
    internal::ModelEffectSetting settings(project);
    MutableString content;
    settings.save(activeModel, content);
    FileWriterScope scope;
    if (scope.open(fileURI, error)) {
        ByteArray bytes;
        FileUtils::write(scope.writer(), content.data(), content.size(), error);
        error.hasReason() ? scope.rollback(error) : scope.commit(error);
    }
}

QString
QtEffectParameterDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
