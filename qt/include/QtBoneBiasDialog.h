/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTBONEBIASDIALOG_H_
#define NANOEM_EMAPP_QT_QTBONEBIASDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui BoneBiasDialog.
 *
 *       The ImGui dialog is currently empty (opens with just a title and no controls); this
 *       native version mirrors that as a minimal non-modal QDialog showing the translated
 *       title, closing when the active model goes away (matching the ImGui guard).
 */
class QtBoneBiasDialog final : public QDialog {
public:
    QtBoneBiasDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtBoneBiasDialog() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void checkActiveModel();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    bool m_closing = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTBONEBIASDIALOG_H_ */