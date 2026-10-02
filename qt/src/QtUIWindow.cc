/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtUIWindow.h"
#include "QtApplicationService.h"
#include "QtAccessoryOutsideParentDialog.h"
#include "QtBoneBiasDialog.h"
#include "QtBoneCorrectionDialog.h"
#include "QtCameraCorrectionDialog.h"
#include "QtModelDrawOrderDialog.h"
#include "QtModelEdgeDialog.h"
#include "QtModelOutsideParentDialog.h"
#include "QtModelTransformOrderDialog.h"
#include "QtMorphCorrectionDialog.h"
#include "QtPhysicsEngineDialog.h"
#include "QtSelfShadowDialog.h"
#include "QtViewportSettingDialog.h"

#include "emapp/IProjectHolder.h"
#include "emapp/Project.h"
#include "emapp/private/CommonInclude.h"

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
QtUIWindow::openBoneKeyframeBezierInterpolationCurveGraphDialog(Project *value)
{
    m_impl->openBoneKeyframeBezierInterpolationCurveGraphDialog(value);
}

void
QtUIWindow::openCameraKeyframeBezierInterpolationCurveGraphDialog(Project *value)
{
    m_impl->openCameraKeyframeBezierInterpolationCurveGraphDialog(value);
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
    m_impl->openBoneParametersDialog(project);
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
    m_impl->openCameraParametersDialog(project);
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
    m_impl->openScaleAllSelectedKeyframesDialog();
}

void
QtUIWindow::openEffectParameterDialog(Project *project)
{
    m_impl->openEffectParameterDialog(project);
}

void
QtUIWindow::openModelParameterDialog(Project *project, Error &error)
{
    m_impl->openModelParameterDialog(project, error);
}

void
QtUIWindow::openPreferenceDialog()
{
    m_impl->openPreferenceDialog();
}

bool
QtUIWindow::openModelIOPluginDialog(Project *project, plugin::ModelIOPlugin *plugin, const ByteArray &input,
    const ByteArray &layout, int functionIndex)
{
    return m_impl->openModelIOPluginDialog(project, plugin, input, layout, functionIndex);
}

bool
QtUIWindow::openMotionIOPluginDialog(Project *project, plugin::MotionIOPlugin *plugin, const ByteArray &input,
    const ByteArray &layout, int functionIndex)
{
    return m_impl->openMotionIOPluginDialog(project, plugin, input, layout, functionIndex);
}

void
QtUIWindow::openSaveProjectDialog(Project *project)
{
    m_impl->openSaveProjectDialog(project);
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
