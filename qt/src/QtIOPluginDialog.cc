/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtIOPluginDialog.h"
#include "QtApplicationService.h"
#include "QtPluginUIWidget.h"

#include "emapp/Model.h"
#include "emapp/PluginFactory.h"
#include "emapp/Project.h"
#include "emapp/plugin/ModelIOPlugin.h"
#include "emapp/plugin/MotionIOPlugin.h"
#include "emapp/private/CommonInclude.h"

#include "emapp/src/protoc/plugin.pb-c.h"

#include <QDialogButtonBox>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtIOPluginDialog::QtIOPluginDialog(QtApplicationService *service, Project *project,
    plugin::ModelIOPlugin *modelPlugin, plugin::MotionIOPlugin *motionPlugin, const ByteArray &input,
    Nanoem__Application__Plugin__UIWindow *window, int functionIndex, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_pluginUI(
          this, window, modelPlugin ? modelPlugin->name() : motionPlugin->name(), project->windowDevicePixelRatio())
    , m_uiWidget(nullptr)
    , m_input(input)
    , m_modelPlugin(modelPlugin)
    , m_motionPlugin(motionPlugin)
    , m_functionIndex(functionIndex)
    , m_reloadLayout(false)
{
    setWindowTitle(QString::fromUtf8(m_pluginUI.title()));
    auto *layout = new QVBoxLayout(this);
    m_uiWidget = new QtPluginUIWidget(this, m_project->windowDevicePixelRatio(), this);
    m_uiWidget->rebuild(m_pluginUI.window());
    layout->addWidget(m_uiWidget, 1);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    /* like the ImGui dialog OK executes the selected function and closes, Cancel only closes */
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtIOPluginDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtIOPluginDialog::reject);
    layout->addWidget(buttonBox);
}

QtIOPluginDialog::~QtIOPluginDialog()
{
}

const char *
QtIOPluginDialog::windowId() const NANOEM_DECL_NOEXCEPT
{
    const Nanoem__Application__Plugin__UIWindow *window = m_pluginUI.window();
    return window ? window->id : "";
}

void
QtIOPluginDialog::handleUIComponent(
    const char *id, const Nanoem__Application__Plugin__UIComponent *component)
{
    const nanoem_rsize_t size = nanoem__application__plugin__uicomponent__get_packed_size(component);
    if (size > 0) {
        ByteArray bytes(size);
        if (nanoem__application__plugin__uicomponent__pack(component, bytes.data())) {
            if (m_modelPlugin) {
                PluginFactory::ModelIOPluginProxy proxy(m_modelPlugin);
                proxy.setUIComponentLayout(id, bytes, m_reloadLayout, m_error);
            }
            else if (m_motionPlugin) {
                PluginFactory::MotionIOPluginProxy proxy(m_motionPlugin);
                proxy.setUIComponentLayout(id, bytes, m_reloadLayout, m_error);
            }
        }
    }
    if (m_reloadLayout) {
        m_reloadLayout = false;
        /* the widget that triggered this is still on the stack: rebuild once the event loop is
           back, after it has unwound (mirrors the ImGui dialog reloading on the next frame) */
        QTimer::singleShot(0, this, &QtIOPluginDialog::reloadLayout);
    }
}

bool
QtIOPluginDialog::reloadUILayout(ByteArray &bytes, Error &error)
{
    if (m_modelPlugin) {
        PluginFactory::ModelIOPluginProxy proxy(m_modelPlugin);
        return proxy.getUIWindowLayout(bytes, error);
    }
    else if (m_motionPlugin) {
        PluginFactory::MotionIOPluginProxy proxy(m_motionPlugin);
        return proxy.getUIWindowLayout(bytes, error);
    }
    return false;
}

void
QtIOPluginDialog::handleAccepted()
{
    Error error;
    if (m_modelPlugin) {
        if (Model *activeModel = m_project->activeModel()) {
            PluginFactory::ModelIOPluginProxy proxy(m_modelPlugin);
            proxy.execute(m_functionIndex, m_input, activeModel, m_project, error);
        }
    }
    else if (m_motionPlugin) {
        PluginFactory::MotionIOPluginProxy proxy(m_motionPlugin);
        proxy.execute(m_functionIndex, m_input, m_project, error);
    }
    error.addModalDialog(m_service);
    accept();
}

void
QtIOPluginDialog::reloadLayout()
{
    Error error;
    if (m_pluginUI.reload(error)) {
        /* every widget holds component pointers owned by the window the reload just freed */
        m_uiWidget->rebuild(m_pluginUI.window());
    }
}

} /* namespace qt */
} /* namespace nanoem */
