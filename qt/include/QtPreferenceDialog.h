/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTPREFERENCEDIALOG_H_
#define NANOEM_EMAPP_QT_QTPREFERENCEDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QCloseEvent;
class QComboBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui PreferenceDialog.
 *
 *       Non-modal like its ImGui counterpart: three tabs (Global/Project/Special) where every
 *       control applies live through ApplicationPreference, Project or
 *       BaseApplicationService::dispatchMenuItemAction. Like the ImGui dialog, dismissing it
 *       persists the configuration with BaseApplicationService::saveApplicationConfiguration.
 *
 *       The Qt shell builds its menu bar once, so switching the language updates the project and
 *       the translator and closes the dialog; reopening it shows the new language.
 */
class QtPreferenceDialog final : public QDialog {
public:
    QtPreferenceDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtPreferenceDialog() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createGlobalTab();
    QWidget *createProjectTab();
    QWidget *createSpecialTab();
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QComboBox *m_fpsCombo;
    QComboBox *m_languageCombo;
    QComboBox *m_filePathModeCombo;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTPREFERENCEDIALOG_H_ */
