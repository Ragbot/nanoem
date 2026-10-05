/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTIOPLUGINDIALOG_H_
#define NANOEM_EMAPP_QT_QTIOPLUGINDIALOG_H_

#include "emapp/Error.h"
#include "emapp/Forward.h"
#include "emapp/internal/PluginUI.h"

#include <QDialog>

namespace nanoem {

class Project;

namespace plugin {
class ModelIOPlugin;
class MotionIOPlugin;
} /* namespace plugin */

namespace qt {

class QtApplicationService;
class QtPluginUIWidget;

/**
 * @brief Native replacement of the ImGui BaseIOPluginDialog (ModelIOPluginDialog and
 *        MotionIOPluginDialog share this class, the ImGui split is only about which proxy runs).
 *
 *        Shown when an IO plugin returns a UI window layout instead of executing right away:
 *        QtPluginUIWidget renders the layout and every widget change is packed back to the
 *        plugin through setUIComponentLayout, exactly like the ImGui dialog did. A plugin that
 *        answers with reloadLayout rebuilds the widget tree on the next event loop tick (the
 *        component pointers the widgets hold are owned by the unpacked window). OK executes the
 *        selected plugin function and closes, Cancel just closes.
 *
 *        The dialog owns the unpacked window through PluginUI, so the window id stays valid for
 *        the lifetime of the dialog and QtUIWindow can deduplicate opens like ImGuiWindow's
 *        m_dialogWindows map did.
 */
class QtIOPluginDialog final : public QDialog, public internal::PluginUI::IReactor {
public:
    QtIOPluginDialog(QtApplicationService *service, Project *project, plugin::ModelIOPlugin *modelPlugin,
        plugin::MotionIOPlugin *motionPlugin, const ByteArray &input,
        Nanoem__Application__Plugin__UIWindow *window, int functionIndex, QWidget *parent);
    ~QtIOPluginDialog() override;

    const char *windowId() const NANOEM_DECL_NOEXCEPT;

    void handleUIComponent(const char *id, const Nanoem__Application__Plugin__UIComponent *component) override;
    bool reloadUILayout(ByteArray &bytes, Error &error) override;

private:
    void handleAccepted();
    void reloadLayout();

    QtApplicationService *m_service;
    Project *m_project;
    internal::PluginUI m_pluginUI;
    QtPluginUIWidget *m_uiWidget;
    ByteArray m_input;
    plugin::ModelIOPlugin *m_modelPlugin;
    plugin::MotionIOPlugin *m_motionPlugin;
    Error m_error;
    int m_functionIndex;
    bool m_reloadLayout;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTIOPLUGINDIALOG_H_ */
