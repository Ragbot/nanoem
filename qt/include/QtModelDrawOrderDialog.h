/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELDRAWORDERDIALOG_H_
#define NANOEM_EMAPP_QT_QTMODELDRAWORDERDIALOG_H_

#include "emapp/internal/imgui/DrawOrderState.h"

#include <QDialog>

class QListWidget;
class QPushButton;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ModelDrawOrderDialog.
 *
 *       Non-modal like its ImGui counterpart: reorders the project drawable order list with
 *       Up/Down buttons, applied live on every move (like ImGuiWindow::draw does through
 *       Project::setDrawableOrderList), while OK keeps the current order and Cancel or closing
 *       the window restores the list snapshot taken when the dialog opened. The
 *       DrawOrderState class is shared with the ImGui dialog (its header is imgui-free).
 */
class QtModelDrawOrderDialog final : public QDialog {
public:
    QtModelDrawOrderDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtModelDrawOrderDialog() override = default;

    /* used by QtUIWindow::closeNativeDialogs during project teardown: the restore-on-close path
       must not run there (and ImGuiWindow::reset did not restore either) */
    void forceClose();

protected:
    void reject() override;

private:
    void handleAccepted();
    void handleMove(int offset);
    void refresh();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    Project::DrawableList m_lastDrawableOrderList;
    internal::imgui::DrawOrderState m_orderState;
    QListWidget *m_listWidget;
    QPushButton *m_upButton;
    QPushButton *m_downButton;
    bool m_restoreOnReject = true;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELDRAWORDERDIALOG_H_ */
