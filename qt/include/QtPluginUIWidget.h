/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTPLUGINUIWIDGET_H_
#define NANOEM_EMAPP_QT_QTPLUGINUIWIDGET_H_

#include "emapp/Forward.h"
#include "emapp/internal/PluginUI.h"

#include "emapp/src/protoc/plugin.pb-c.h"

#include <QWidget>

class QBoxLayout;
class QHBoxLayout;

namespace nanoem {

namespace qt {

/**
 * @brief Native Qt widget tree for the UI layout an IO plugin packs into a
 *        Nanoem__Application__Plugin__UIWindow.
 *
 *        Mirrors internal::PluginUI::drawComponent (the ImGui renderer over the same protobuf
 *        tree) one component at a time: every widget writes its value back into the component it
 *        was built from and notifies the reactor, so the plugin observes exactly the
 *        setUIComponentLayout calls it saw with the ImGui dialog. Components that ImGui validates
 *        before drawing (the scalar rows need a value buffer and, for drag/slider, min/max of the
 *        declared data type) are skipped the same way when their buffers are short.
 *
 *        Layout is a vertical flow; a SameLine component puts the following component on the same
 *        row, the way ImGui::SameLine does. A layout rebuild (rebuild()) is required whenever the
 *        plugin reloads its window, because the component pointers the widgets hold are owned by
 *        the unpacked window.
 */
class QtPluginUIWidget final : public QWidget {
public:
    QtPluginUIWidget(internal::PluginUI::IReactor *reactor, nanoem_f32_t devicePixelRatio, QWidget *parent);
    ~QtPluginUIWidget() override;

    void rebuild(const Nanoem__Application__Plugin__UIWindow *window);

private:
    QWidget *createComponent(Nanoem__Application__Plugin__UIComponent *component);
    QWidget *createScalarRow(const char *id, const char *text, Nanoem__Application__Plugin__UIComponent *component,
        Nanoem__Application__Plugin__UIComponent__DataType dataType, nanoem_rsize_t numComponents,
        ProtobufCBinaryData *value, const ProtobufCBinaryData *min, const ProtobufCBinaryData *max,
        double singleStep, bool bounded);
    void buildItems(
        QBoxLayout *layout, Nanoem__Application__Plugin__UIComponent **items, nanoem_rsize_t numItems);
    void appendRow(QBoxLayout *layout, QWidget *widget, QHBoxLayout **row, bool *pendingSameLine);
    static void clearLayout(QBoxLayout *layout);

    internal::PluginUI::IReactor *m_reactor;
    nanoem_f32_t m_devicePixelRatio;
    QBoxLayout *m_layout;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTPLUGINUIWIDGET_H_ */
