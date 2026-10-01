/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMODELPANEL_H_
#define NANOEM_EMAPP_QT_QTMODELPANEL_H_

#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QEvent;
class QLineEdit;
class QObject;
class QPushButton;
class QRadioButton;
class QTimer;
class QStackedWidget;

namespace nanoem {

class Model;
class Project;

namespace qt {

class QtApplicationClient;
class QtApplicationService;

/**
 * @brief Native replacement of the ImGui model panel (ImGuiWindow::drawModelPanel).
 *
 *       Provides the active model selector (with the Ctrl+click name editing affordance), load/delete
 *       buttons, the V/S/A visibility toggles, the constraint selector with its ON/OFF radios, the
 *       outside parent form dialog opener (still rendered by ImGui) and the register keyframe button.
 *       Model activation goes through sendSetActiveModelMessage exactly like the native Model menu.
 */
class QtModelPanel final : public QGroupBox {
public:
    QtModelPanel(QtApplicationService *service, QtApplicationClient *client, QWidget *parent);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Project *currentProject();
    Model *activeModel();
    void refresh();
    void refreshModelComboBox(Project *project, Model *activeModel);
    void refreshConstraintComboBox(Model *activeModel);
    void beginModelNameEditing();
    void commitModelName();
    void applyConstraintEnabled(bool value);

    QtApplicationService *m_service;
    QtApplicationClient *m_client;
    QTimer *m_timer;
    QStackedWidget *m_nameStack = nullptr;
    QComboBox *m_modelComboBox = nullptr;
    QLineEdit *m_nameLineEdit = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QCheckBox *m_visibleCheckBox = nullptr;
    QCheckBox *m_shadowCheckBox = nullptr;
    QCheckBox *m_addBlendCheckBox = nullptr;
    QComboBox *m_constraintComboBox = nullptr;
    QPushButton *m_outsideParentButton = nullptr;
    QRadioButton *m_constraintOnRadioButton = nullptr;
    QRadioButton *m_constraintOffRadioButton = nullptr;
    QPushButton *m_registerButton = nullptr;
    const Model *m_constraintSelectorModel = nullptr;
    bool m_editingModelName = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMODELPANEL_H_ */
