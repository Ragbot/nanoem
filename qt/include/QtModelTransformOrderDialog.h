/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELTRANSFORMORDERDIALOG_H_
#define NANOEM_EMAPP_QT_QTMODELTRANSFORMORDERDIALOG_H_

#include "emapp/internal/imgui/TransformOrderState.h"

#include <QDialog>

class QListWidget;
class QPushButton;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ModelTransformOrderDialog.
 *
 *       Non-modal like its ImGui counterpart: reorders the project transform order list with
 *       Up/Down buttons, applied live on every move (like ImGuiWindow::draw does through
 *       Project::setTransformOrderList), while OK keeps the current order and Cancel or closing
 *       the window restores the list snapshot taken when the dialog opened. The
 *       TransformOrderState class is shared with the ImGui dialog (its header is imgui-free).
 */
class QtModelTransformOrderDialog final : public QDialog {
public:
    QtModelTransformOrderDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtModelTransformOrderDialog() override = default;

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
    Project::ModelList m_lastTransformOrderList;
    internal::imgui::TransformOrderState m_orderState;
    QListWidget *m_listWidget;
    QPushButton *m_upButton;
    QPushButton *m_downButton;
    bool m_restoreOnReject = true;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELTRANSFORMORDERDIALOG_H_ */
