/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTUIWINDOW_H_
#define NANOEM_EMAPP_QT_QTUIWINDOW_H_

#include "emapp/internal/IUIWindow.h"

#include <QPointer>

namespace nanoem {
namespace qt {

class QtApplicationService;
class QtAccessoryOutsideParentDialog;
class QtBoneBiasDialog;
class QtBoneCorrectionDialog;
class QtCameraCorrectionDialog;
class QtModelDrawOrderDialog;
class QtModelEdgeDialog;
class QtModelOutsideParentDialog;
class QtModelTransformOrderDialog;
class QtMorphCorrectionDialog;
class QtPhysicsEngineDialog;
class QtPreferenceDialog;
class QtScaleAllSelectedKeyframesDialog;
class QtSelfShadowDialog;
class QtViewportSettingDialog;

/**
 * @brief IUIWindow decorator installed through BaseApplicationService::createUIWindow.
 *
 *        The wrapped ImGui window keeps doing everything it did before (viewport rendering, the
 *        ImGui form dialogs without a native counterpart, modal dialog queue, input), while the
 *        form dialogs that have been ported to Qt are intercepted here. The default-bodied
 *        open* methods of IUIWindow are no-ops, so every one of them must be forwarded
 *        explicitly; a dialog override must do so too until it is ported.
 *
 *        Note the dialog identifier deduplication the ImGui m_dialogWindows map did: each native
 *        dialog is kept in a QPointer and re-raised instead of opened twice.
 */
class QtUIWindow final : public internal::IUIWindow {
public:
    QtUIWindow(QtApplicationService *service, internal::IUIWindow *impl);
    ~QtUIWindow() override;

    void setNativePanelMask(nanoem_u32_t value) override;
    void openModelOutsideParentDialog(Project *value) override;
    void openAccessoryOutsideParentDialog(Project *value) override;
    void openBoneKeyframeBezierInterpolationCurveGraphDialog(Project *value) override;
    void openCameraKeyframeBezierInterpolationCurveGraphDialog(Project *value) override;
    void openSelfShadowConfigurationDialog() override;
    void initialize(nanoem_f32_t windowDevicePixelRatio, nanoem_f32_t viewportDevicePixelRatio) override;
    void reset(Project *project) override;
    void destroy() NANOEM_DECL_NOEXCEPT override;
    void setFontPointSize(nanoem_f32_t pointSize) override;
    void setKeyPressed(BaseApplicationService::KeyType value) override;
    void setKeyReleased(BaseApplicationService::KeyType value) override;
    void setUnicodePressed(nanoem_u32_t value) override;
    void setCurrentCPUPercentage(nanoem_f32_t value) override;
    void setCurrentMemoryBytes(nanoem_u64_t value) override;
    void setMaxMemoryBytes(nanoem_u64_t value) override;
    void setScreenCursorPress(int type, int x, int y, int modifiers) override;
    void setScreenCursorMove(int x, int y, int modifiers) override;
    void setScreenCursorRelease(int type, int x, int y, int modifiers) override;
    void openBoneParametersDialog(Project *project) override;
    void openBoneCorrectionDialog(Project *project) override;
    void openBoneBiasDialog(Project *project) override;
    void openCameraParametersDialog(Project *project) override;
    void openCameraCorrectionDialog(Project *project) override;
    void openMorphCorrectionDialog(Project *project) override;
    void openViewportDialog(Project *project) override;
    void openModelDrawOrderDialog(Project *project) override;
    void openModelTransformOrderDialog(Project *project) override;
    void openPhysicsEngineDialog(Project *project) override;
    void openModelAllObjectsDialog() override;
    void openModelEdgeDialog(Project *project) override;
    void openScaleAllSelectedKeyframesDialog() override;
    void openEffectParameterDialog(Project *project) override;
    void openModelParameterDialog(Project *project, Error &error) override;
    void openPreferenceDialog() override;
    bool openModelIOPluginDialog(Project *project, plugin::ModelIOPlugin *plugin, const ByteArray &input,
        const ByteArray &layout, int functionIndex) override;
    bool openMotionIOPluginDialog(Project *project, plugin::MotionIOPlugin *plugin, const ByteArray &input,
        const ByteArray &layout, int functionIndex) override;
    void openSaveProjectDialog(Project *project) override;
    void resizeDevicePixelWindowSize(const Vector2UI16 &value) override;
    void setDevicePixelRatio(float value) override;
    void setAntiAliasEnabled(bool value) override;
    void drawAllWindows(Project *project, IState *state, nanoem_u32_t flags) override;
    const IModalDialog *currentModalDialog() const NANOEM_DECL_NOEXCEPT override;
    IModalDialog *currentModalDialog() NANOEM_DECL_NOEXCEPT override;
    void addModalDialog(IModalDialog *value) override;
    void clearAllModalDialogs() override;
    bool hasModalDialog() const NANOEM_DECL_NOEXCEPT override;
    void swapModalDialog(IModalDialog *value) override;
    bool isVisible() const NANOEM_DECL_NOEXCEPT override;
    void setVisible(bool value) override;
    IPrimitive2D *primitiveContext() NANOEM_DECL_NOEXCEPT override;

private:
    void closeNativeDialogs();

    QtApplicationService *m_service;
    internal::IUIWindow *m_impl;
    QPointer<QtSelfShadowDialog> m_selfShadowDialog;
    QPointer<QtMorphCorrectionDialog> m_morphCorrectionDialog;
    QPointer<QtBoneCorrectionDialog> m_boneCorrectionDialog;
    QPointer<QtBoneBiasDialog> m_boneBiasDialog;
    QPointer<QtCameraCorrectionDialog> m_cameraCorrectionDialog;
    QPointer<QtModelDrawOrderDialog> m_modelDrawOrderDialog;
    QPointer<QtModelEdgeDialog> m_modelEdgeDialog;
    QPointer<QtModelOutsideParentDialog> m_modelOutsideParentDialog;
    QPointer<QtModelTransformOrderDialog> m_modelTransformOrderDialog;
    QPointer<QtAccessoryOutsideParentDialog> m_accessoryOutsideParentDialog;
    QPointer<QtViewportSettingDialog> m_viewportSettingDialog;
    QPointer<QtPhysicsEngineDialog> m_physicsEngineDialog;
    QPointer<QtPreferenceDialog> m_preferenceDialog;
    QPointer<QtScaleAllSelectedKeyframesDialog> m_scaleAllSelectedKeyframesDialog;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTUIWINDOW_H_ */
