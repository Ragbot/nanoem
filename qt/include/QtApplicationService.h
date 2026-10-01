/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTAPPLICATIONSERVICE_H_
#define NANOEM_EMAPP_QT_QTAPPLICATIONSERVICE_H_

#include "emapp/BaseApplicationService.h"

#include "QtApplicationClient.h"

#include <functional>

#include <QtCore/qnamespace.h>

class QWidget;

namespace nanoem {
namespace qt {

class QtModalDialogPresenter;

class QtApplicationService final : public BaseApplicationService {
public:
    static int convertCursorModifier(Qt::KeyboardModifiers value);
    static int convertCursorType(Qt::MouseButton value);
    static nanoem_u32_t convertKeyType(Qt::Key value);

    QtApplicationService(const JSON_Value *config, QtApplicationClient::Bridge *bridge);
    ~QtApplicationService() override;

    void draw();
    void consumeDispatchAllCommandMessages();
    void setFrameBufferResolver(std::function<nanoem_u32_t()> value);
    void setModalDialogParentWidget(QWidget *value);

private:
    void sendEventMessage(const Nanoem__Application__Event *event) override;
    void handleInitializeApplication() override;
    void beginDefaultPass(
        nanoem_u32_t windowID, const sg_pass_action &pa, int width, int height, int &sampleCount) override;
    void endDefaultPass() override;
    void rebindFrameBuffer();
    const IModalDialog *currentModalDialog() const NANOEM_DECL_NOEXCEPT override;
    IModalDialog *currentModalDialog() NANOEM_DECL_NOEXCEPT override;
    void addModalDialog(IModalDialog *value) override;
    void clearAllModalDialog() override;
    bool hasModalDialog() const NANOEM_DECL_NOEXCEPT override;

    QtApplicationClient::Bridge *m_bridge;
    QtModalDialogPresenter *m_presenter;
    std::function<nanoem_u32_t()> m_frameBufferResolver;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTAPPLICATIONSERVICE_H_ */
