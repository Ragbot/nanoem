/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTEFFECTPARAMETERDIALOG_H_
#define NANOEM_EMAPP_QT_QTEFFECTPARAMETERDIALOG_H_

#include "emapp/Effect.h"
#include "emapp/Forward.h"
#include "emapp/IFileManager.h"
#include "emapp/Project.h"
#include "emapp/effect/Common.h"
#include "emapp/effect/OffscreenRenderTargetOption.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace nanoem {

class Error;
class IDrawable;
class IFileManager;
class Model;
class Project;
class URI;

namespace internal {
class IUIWindow;
} /* namespace internal */

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui EffectParameterDialog.
 *
 *       Non-modal like its ImGui counterpart with the same three tabs (Offscreen/Model/
 *       Parameter). Selection panes are QListWidgets, the per drawable/material rows are
 *       checkable rows driving the same Project attach sets the ImGui Selectable rows use, and
 *       the effect file queries go through IEventPublisher so the shell's own file dialog opens.
 *
 *       The Parameter tab's offscreen/render target previews are the one part that cannot be
 *       ported yet: ImGui draws them with ImGui::Image over the sg_image id, and sokol exposes no
 *       image -> GL texture accessor, so the Qt shell shows the size/description placeholder
 *       instead. Everything else (uniform buffer inspection and the UI widget parameter editors)
 *       is fully ported.
 */
class QtEffectParameterDialog final : public QDialog {
public:
    QtEffectParameterDialog(
        QtApplicationService *service, Project *project, internal::IUIWindow *window, QWidget *parent);
    ~QtEffectParameterDialog() override = default;

protected:
    void showEvent(QShowEvent *event) override;

private:
    static void handleLoadingModelEffectSetting(
        const URI &fileURI, Project *project, Error &error, void *userData);
    static void handleSaveingModelEffectSetting(
        const URI &fileURI, Project *project, Error &error, void *userData);

    QWidget *createOffscreenTab();
    QWidget *createModelTab();
    QWidget *createParameterTab();
    Effect *currentEffect(IDrawable **drawable) const;
    void rebuildAll();
    void rebuildOffscreenList();
    void rebuildOffscreenDetail();
    void rebuildModelList();
    void rebuildModelDetail();
    void rebuildParameterList();
    void rebuildParameterDetail();
    void appendDrawableRow(
        QVBoxLayout *layout, IDrawable *drawable, const String &ownerName, bool mainRenderTarget);
    void appendMaterialRow(QVBoxLayout *layout, Model *model, nanoem_rsize_t materialIndex);
    void appendParameterEditor(const effect::UIWidgetParameter *parameter);
    void appendUniformBufferDetail(const String &name, const ByteArray &bytes);
    void appendRenderTargetPlaceholder(const String &name, const String &description, const sg_image_desc &desc);
    void queryFileDialog(IFileManager::DialogType type);
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    internal::IUIWindow *m_window;
    QTabWidget *m_tabs;
    QPushButton *m_reloadButton;
    QCheckBox *m_inspectCheckBox;
    QListWidget *m_offscreenList;
    QWidget *m_offscreenDetail;
    QVBoxLayout *m_offscreenDetailLayout;
    QListWidget *m_modelList;
    QWidget *m_modelDetail;
    QVBoxLayout *m_modelDetailLayout;
    QListWidget *m_parameterList;
    QWidget *m_parameterDetail;
    QVBoxLayout *m_parameterDetailLayout;
    QPushButton *m_loadEmdButton;
    QPushButton *m_saveEmdButton;
    int m_activeOffscreenRenderTargetIndex;
    int m_activeModelTargetIndex;
    int m_activeParameterIndex;
    int m_uniformBuffersBoundIndex;
    int m_renderTargetsBoundIndex;
    Effect::PassUniformBufferMap m_uniformBuffers;
    Effect::NamedRenderTargetColorImageContainerMap m_renderTargets;
    effect::OffscreenRenderTargetOptionList m_offscreenRenderTargets;
    /* the options the offscreen list rows were built from, so the detail pane shows the option
       the selected row corresponds to */
    effect::OffscreenRenderTargetOptionList m_offscreenOptions;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTEFFECTPARAMETERDIALOG_H_ */
