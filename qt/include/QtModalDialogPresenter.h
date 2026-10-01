/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODALDIALOGPRESENTER_H_
#define NANOEM_EMAPP_QT_QTMODALDIALOGPRESENTER_H_

#include "emapp/Forward.h"

#include <QtCore/QList>
#include <QtCore/QObject>

class QDialog;
class QWidget;

namespace nanoem {

class BaseApplicationService;
class IModalDialog;

namespace qt {

/**
 * @brief Native Qt replacement for the ImGui modal dialog window.
 *
 * BaseApplicationService forwards addModalDialog/hasModalDialog/currentModalDialog/
 * clearAllModalDialog to this presenter instead of the ImGuiWindow list for dialogs
 * reporting IModalDialog::isNativelyRendered(). Dialogs are queued, shown one at a
 * time as QMessageBox instances running a nested event loop (rendering keeps going),
 * and the button result is dispatched back through the IModalDialog onAccepted/
 * onCancelled/onSaved/onDiscarded callbacks, honouring chained follow-up dialogs
 * exactly like ImGuiWindow::swapModalDialog does.
 */
class QtModalDialogPresenter final : public QObject {
public:
    explicit QtModalDialogPresenter(BaseApplicationService *service);
    ~QtModalDialogPresenter() override;

    void setParentWidget(QWidget *value) NANOEM_DECL_NOEXCEPT
    {
        m_parentWidget = value;
    }
    void add(IModalDialog *dialog);
    void clearAll();
    bool has() const NANOEM_DECL_NOEXCEPT
    {
        return !m_dialogs.isEmpty();
    }
    IModalDialog *current() const NANOEM_DECL_NOEXCEPT;

private:
    void scheduleShow();
    void showNext();
    void removeAllDialogs();

    BaseApplicationService *m_service;
    QWidget *m_parentWidget;
    QList<IModalDialog *> m_dialogs;
    IModalDialog *m_shownDialog;
    QDialog *m_activeDialog;
    bool m_showScheduled;
    bool m_clearRequested;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODALDIALOGPRESENTER_H_ */
