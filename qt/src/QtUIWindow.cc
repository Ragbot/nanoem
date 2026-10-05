/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtUIWindow.h"
#include "QtApplicationService.h"
#include "QtAccessoryOutsideParentDialog.h"
#include "QtBoneBiasDialog.h"
#include "QtBoneCorrectionDialog.h"
#include "QtBoneParametersDialog.h"
#include "QtCameraCorrectionDialog.h"
#include "QtCameraParametersDialog.h"
#include "QtEffectParameterDialog.h"
#include "QtIOPluginDialog.h"
#include "QtInterpolationCurveGraphDialog.h"
#include "QtModelDrawOrderDialog.h"
#include "QtModelEdgeDialog.h"
#include "QtModelOutsideParentDialog.h"
#include "QtModelTransformOrderDialog.h"
#include "QtMorphCorrectionDialog.h"
#include "QtPhysicsEngineDialog.h"
#include "QtPreferenceDialog.h"
#include "QtScaleAllSelectedKeyframesDialog.h"
#include "QtSelfShadowDialog.h"
#include "QtViewportSettingDialog.h"

#include "emapp/IEventPublisher.h"
#include "emapp/IFileManager.h"
#include "emapp/IProjectHolder.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/private/CommonInclude.h"

#include "emapp/src/protoc/plugin.pb-c.h"

namespace nanoem {
namespace qt {

QtUIWindow::QtUIWindow(QtApplicationService *service, internal::IUIWindow *impl)
    : m_service(service)
    , m_impl(impl)
{
}

QtUIWindow::~QtUIWindow()
{
    closeNativeDialogs();
    nanoem_delete(m_impl);
}

void
QtUIWindow::closeNativeDialogs()
{
    /* every native dialog holds a raw Project pointer: close them whenever the wrapped window
       resets or is destroyed, mirroring how ImGuiWindow::reset clears m_dialogWindows */
    if (QtSelfShadowDialog *dialog = m_selfShadowDialog.data()) {
        dialog->close();
    }
    if (QtMorphCorrectionDialog *dialog = m_morphCorrectionDialog.data()) {
        dialog->close();
    }
    if (QtBoneCorrectionDialog *dialog = m_boneCorrectionDialog.data()) {
        dialog->close();
    }
    if (QtCameraCorrectionDialog *dialog = m_cameraCorrectionDialog.data()) {
        dialog->close();
    }
    if (QtModelDrawOrderDialog *dialog = m_modelDrawOrderDialog.data()) {
        dialog->forceClose();
    }
    if (QtModelTransformOrderDialog *dialog = m_modelTransformOrderDialog.data()) {
        dialog->forceClose();
    }
    if (QtModelOutsideParentDialog *dialog = m_modelOutsideParentDialog.data()) {
        dialog->close();
    }
    if (QtAccessoryOutsideParentDialog *dialog = m_accessoryOutsideParentDialog.data()) {
        dialog->close();
    }
    if (QtPhysicsEngineDialog *dialog = m_physicsEngineDialog.data()) {
        dialog->close();
    }
    if (QtScaleAllSelectedKeyframesDialog *dialog = m_scaleAllSelectedKeyframesDialog.data()) {
        dialog->close();
    }
    if (QtPreferenceDialog *dialog = m_preferenceDialog.data()) {
        dialog->close();
    }
    if (QtBoneParametersDialog *dialog = m_boneParametersDialog.data()) {
        dialog->close();
    }
    if (QtCameraParametersDialog *dialog = m_cameraParametersDialog.data()) {
        dialog->close();
    }
    if (QtEffectParameterDialog *dialog = m_effectParameterDialog.data()) {
        dialog->close();
    }
    if (QtInterpolationCurveGraphDialog *dialog = m_boneCurveGraphDialog.data()) {
        dialog->close();
    }
    if (QtInterpolationCurveGraphDialog *dialog = m_cameraCurveGraphDialog.data()) {
        dialog->close();
    }
    for (QList<QPointer<QtIOPluginDialog>>::iterator it = m_ioPluginDialogs.begin();
         it != m_ioPluginDialogs.end(); ++it) {
        if (QtIOPluginDialog *dialog = it->data()) {
            dialog->close();
        }
    }
    m_ioPluginDialogs.clear();
}

void
QtUIWindow::setNativePanelMask(nanoem_u32_t value)
{
    m_impl->setNativePanelMask(value);
}

void
QtUIWindow::openModelOutsideParentDialog(Project *project)
{
    if (QtModelOutsideParentDialog *dialog = m_modelOutsideParentDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project && project->activeModel()) {
        auto *dialog = new QtModelOutsideParentDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_modelOutsideParentDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openAccessoryOutsideParentDialog(Project *project)
{
    if (QtAccessoryOutsideParentDialog *dialog = m_accessoryOutsideParentDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project && project->activeAccessory()) {
        auto *dialog = new QtAccessoryOutsideParentDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_accessoryOutsideParentDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openBoneKeyframeBezierInterpolationCurveGraphDialog(Project *project)
{
    openCurveGraphDialog(project, true);
}

void
QtUIWindow::openCameraKeyframeBezierInterpolationCurveGraphDialog(Project *project)
{
    openCurveGraphDialog(project, false);
}

void
QtUIWindow::openCurveGraphDialog(Project *project, bool boneVariant)
{
    /* ImGuiWindow drew these over the GL viewport from its interpolation panels, which the Qt
       shell masks out: QtInterpolationPanel owns the dialog it opens itself, so this only serves
       the service level entry points (BaseApplicationService::open*KeyframeBezierInterpolation-
       CurveGraphDialog), which used to fall back to the ImGui renderer. The bone variant needs an
       active model the way the ImGui one did. */
    if (boneVariant && !project->activeModel()) {
        return;
    }
    QPointer<QtInterpolationCurveGraphDialog> &dialog =
        boneVariant ? m_boneCurveGraphDialog : m_cameraCurveGraphDialog;
    if (dialog && dialog->isVisible()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    dialog = new QtInterpolationCurveGraphDialog(
        m_service, project, boneVariant, m_service->modalDialogParentWidget());
    dialog->show();
}

void
QtUIWindow::openSelfShadowConfigurationDialog()
{
    if (QtSelfShadowDialog *dialog = m_selfShadowDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (Project *project = m_service->projectHolder()->currentProject()) {
        auto *dialog = new QtSelfShadowDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_selfShadowDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::initialize(nanoem_f32_t windowDevicePixelRatio, nanoem_f32_t viewportDevicePixelRatio)
{
    m_impl->initialize(windowDevicePixelRatio, viewportDevicePixelRatio);
}

void
QtUIWindow::reset(Project *project)
{
    closeNativeDialogs();
    m_impl->reset(project);
}

void
QtUIWindow::destroy() NANOEM_DECL_NOEXCEPT
{
    closeNativeDialogs();
    m_impl->destroy();
}

void
QtUIWindow::setFontPointSize(nanoem_f32_t pointSize)
{
    m_impl->setFontPointSize(pointSize);
}

void
QtUIWindow::setKeyPressed(BaseApplicationService::KeyType value)
{
    m_impl->setKeyPressed(value);
}

void
QtUIWindow::setKeyReleased(BaseApplicationService::KeyType value)
{
    m_impl->setKeyReleased(value);
}

void
QtUIWindow::setUnicodePressed(nanoem_u32_t value)
{
    m_impl->setUnicodePressed(value);
}

void
QtUIWindow::setCurrentCPUPercentage(nanoem_f32_t value)
{
    m_impl->setCurrentCPUPercentage(value);
}

void
QtUIWindow::setCurrentMemoryBytes(nanoem_u64_t value)
{
    m_impl->setCurrentMemoryBytes(value);
}

void
QtUIWindow::setMaxMemoryBytes(nanoem_u64_t value)
{
    m_impl->setMaxMemoryBytes(value);
}

void
QtUIWindow::setScreenCursorPress(int type, int x, int y, int modifiers)
{
    m_impl->setScreenCursorPress(type, x, y, modifiers);
}

void
QtUIWindow::setScreenCursorMove(int x, int y, int modifiers)
{
    m_impl->setScreenCursorMove(x, y, modifiers);
}

void
QtUIWindow::setScreenCursorRelease(int type, int x, int y, int modifiers)
{
    m_impl->setScreenCursorRelease(type, x, y, modifiers);
}

void
QtUIWindow::openBoneParametersDialog(Project *project)
{
    if (QtBoneParametersDialog *dialog = m_boneParametersDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    /* ImGuiWindow::drawBoneParameterPanel only renders while the model has an active bone */
    if (project && project->activeModel() && project->activeModel()->activeBone()) {
        auto *dialog = new QtBoneParametersDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_boneParametersDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openBoneCorrectionDialog(Project *project)
{
    if (QtBoneCorrectionDialog *dialog = m_boneCorrectionDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    /* ImGuiWindow::openBoneCorrectionDialog only opens with an active model */
    if (project && project->activeModel()) {
        auto *dialog = new QtBoneCorrectionDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_boneCorrectionDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openBoneBiasDialog(Project *project)
{
    if (QtBoneBiasDialog *dialog = m_boneBiasDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project && project->activeModel()) {
        auto *dialog = new QtBoneBiasDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_boneBiasDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openCameraParametersDialog(Project *project)
{
    if (QtCameraParametersDialog *dialog = m_cameraParametersDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project && project->globalCamera()) {
        auto *dialog = new QtCameraParametersDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_cameraParametersDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openCameraCorrectionDialog(Project *project)
{
    if (QtCameraCorrectionDialog *dialog = m_cameraCorrectionDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtCameraCorrectionDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_cameraCorrectionDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openMorphCorrectionDialog(Project *project)
{
    if (QtMorphCorrectionDialog *dialog = m_morphCorrectionDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    /* ImGuiWindow::openMorphCorrectionDialog only opens with an active model */
    if (project && project->activeModel()) {
        auto *dialog = new QtMorphCorrectionDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_morphCorrectionDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openViewportDialog(Project *project)
{
    if (QtViewportSettingDialog *dialog = m_viewportSettingDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtViewportSettingDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_viewportSettingDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openModelDrawOrderDialog(Project *project)
{
    if (QtModelDrawOrderDialog *dialog = m_modelDrawOrderDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtModelDrawOrderDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_modelDrawOrderDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openModelTransformOrderDialog(Project *project)
{
    if (QtModelTransformOrderDialog *dialog = m_modelTransformOrderDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtModelTransformOrderDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_modelTransformOrderDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openPhysicsEngineDialog(Project *project)
{
    if (QtPhysicsEngineDialog *dialog = m_physicsEngineDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtPhysicsEngineDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_physicsEngineDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openModelAllObjectsDialog()
{
    m_impl->openModelAllObjectsDialog();
}

void
QtUIWindow::openModelEdgeDialog(Project *project)
{
    if (QtModelEdgeDialog *dialog = m_modelEdgeDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project && project->activeModel()) {
        auto *dialog = new QtModelEdgeDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_modelEdgeDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openScaleAllSelectedKeyframesDialog()
{
    if (QtScaleAllSelectedKeyframesDialog *dialog = m_scaleAllSelectedKeyframesDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    /* the ImGui dialog is opened from the timeline keyframe action panel without a Project */
    if (Project *project = m_service->projectHolder()->currentProject()) {
        auto *dialog = new QtScaleAllSelectedKeyframesDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_scaleAllSelectedKeyframesDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::openEffectParameterDialog(Project *project)
{
    if (QtEffectParameterDialog *dialog = m_effectParameterDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (project) {
        auto *dialog = new QtEffectParameterDialog(m_service, project, this, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_effectParameterDialog = dialog;
        dialog->show();
    }
}

void
QtUIWindow::reloadActiveEffect(IDrawable *value)
{
    m_impl->reloadActiveEffect(value);
}

void
QtUIWindow::openModelParameterDialog(Project *project, Error &error)
{
    m_impl->openModelParameterDialog(project, error);
}

void
QtUIWindow::openPreferenceDialog()
{
    if (QtPreferenceDialog *dialog = m_preferenceDialog.data()) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    /* ImGuiWindow::openPreferenceDialog indexes the dialog windows map with a project-less open;
       the dialog itself reads/writes project state so it only opens with a live project */
    if (Project *project = m_service->projectHolder()->currentProject()) {
        auto *dialog = new QtPreferenceDialog(m_service, project, m_service->modalDialogParentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        m_preferenceDialog = dialog;
        dialog->show();
    }
}

bool
QtUIWindow::openModelIOPluginDialog(Project *project, plugin::ModelIOPlugin *plugin, const ByteArray &input,
    const ByteArray &layout, int functionIndex)
{
    return openIOPluginDialog(project, plugin, nullptr, input, layout, functionIndex);
}

bool
QtUIWindow::openMotionIOPluginDialog(Project *project, plugin::MotionIOPlugin *plugin, const ByteArray &input,
    const ByteArray &layout, int functionIndex)
{
    return openIOPluginDialog(project, nullptr, plugin, input, layout, functionIndex);
}

bool
QtUIWindow::openIOPluginDialog(Project *project, plugin::ModelIOPlugin *modelPlugin,
    plugin::MotionIOPlugin *motionPlugin, const ByteArray &input, const ByteArray &layout, int functionIndex)
{
    Nanoem__Application__Plugin__UIWindow *window =
        nanoem__application__plugin__uiwindow__unpack(g_protobufc_allocator, layout.size(), layout.data());
    if (!window) {
        return false;
    }
    const QString windowId(QString::fromUtf8(window->id));
    /* ImGuiWindow kept one dialog per window id in its m_dialogWindows map: a second open of the
       same window frees the layout unpacked for it and raises the dialog that is already up */
    for (QList<QPointer<QtIOPluginDialog>>::iterator it = m_ioPluginDialogs.begin();
         it != m_ioPluginDialogs.end();) {
        QtIOPluginDialog *dialog = it->data();
        if (!dialog) {
            it = m_ioPluginDialogs.erase(it);
            continue;
        }
        if (windowId == QString::fromUtf8(dialog->windowId())) {
            nanoem__application__plugin__uiwindow__free_unpacked(window, g_protobufc_allocator);
            dialog->raise();
            dialog->activateWindow();
            return true;
        }
        ++it;
    }
    auto *dialog = new QtIOPluginDialog(m_service, project, modelPlugin, motionPlugin, input, window, functionIndex,
        m_service->modalDialogParentWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    m_ioPluginDialogs.append(QPointer<QtIOPluginDialog>(dialog));
    dialog->show();
    return true;
}

void
QtUIWindow::openSaveProjectDialog(Project * /* project */)
{
    /* ImGuiWindow::openSaveProjectDialog forwards to the ImGui menu builder, which the Qt shell
       never creates (menubarApplicationClient() is null), so saving an unnamed project used to be
       a silent no-op here. Publish the same query the File menu's Save/Save As use instead: the
       Qt menu builder's file dialog listener runs QFileDialog and replies with
       sendSaveFileMessage, which lands in the SAVE_FILE command and persists the project. */
    m_service->eventPublisher()->publishQuerySaveFileDialogEvent(
        IFileManager::kDialogTypeSaveProjectFile, Project::loadableExtensions());
}

void
QtUIWindow::resizeDevicePixelWindowSize(const Vector2UI16 &value)
{
    m_impl->resizeDevicePixelWindowSize(value);
}

void
QtUIWindow::setDevicePixelRatio(float value)
{
    m_impl->setDevicePixelRatio(value);
}

void
QtUIWindow::setAntiAliasEnabled(bool value)
{
    m_impl->setAntiAliasEnabled(value);
}

void
QtUIWindow::drawAllWindows(Project *project, IState *state, nanoem_u32_t flags)
{
    m_impl->drawAllWindows(project, state, flags);
}

const IModalDialog *
QtUIWindow::currentModalDialog() const NANOEM_DECL_NOEXCEPT
{
    return m_impl->currentModalDialog();
}

IModalDialog *
QtUIWindow::currentModalDialog() NANOEM_DECL_NOEXCEPT
{
    return m_impl->currentModalDialog();
}

void
QtUIWindow::addModalDialog(IModalDialog *value)
{
    m_impl->addModalDialog(value);
}

void
QtUIWindow::clearAllModalDialogs()
{
    m_impl->clearAllModalDialogs();
}

bool
QtUIWindow::hasModalDialog() const NANOEM_DECL_NOEXCEPT
{
    return m_impl->hasModalDialog();
}

void
QtUIWindow::swapModalDialog(IModalDialog *value)
{
    m_impl->swapModalDialog(value);
}

bool
QtUIWindow::isVisible() const NANOEM_DECL_NOEXCEPT
{
    return m_impl->isVisible();
}

void
QtUIWindow::setVisible(bool value)
{
    m_impl->setVisible(value);
}

IPrimitive2D *
QtUIWindow::primitiveContext() NANOEM_DECL_NOEXCEPT
{
    return m_impl->primitiveContext();
}

} /* namespace qt */
} /* namespace nanoem */
