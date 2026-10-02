/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPORTSETTINGDIALOG_H_
#define NANOEM_EMAPP_QT_QTVIEWPORTSETTINGDIALOG_H_

#include "emapp/Forward.h"

#include <QDialog>

class QComboBox;
class QSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui ViewportSettingDialog.
 *
 *       Non-modal like its ImGui counterpart: size (DragInt2 + preset combo),
 *       background color (ColorDialog), transparent checkbox. Color and transparent
 *       changes apply live; OK applies size, Cancel restores color and transparent
 *       to the values at dialog open.
 */
class QtViewportSettingDialog final : public QDialog {
public:
    QtViewportSettingDialog(QtApplicationService *service, Project *project, QWidget *parent);
    ~QtViewportSettingDialog() override = default;

protected:
    void reject() override;

private:
    void handleAccepted();
    void handlePresetChanged(int index);
    void handleColorChanged();
    void handleTransparentChanged(int state);
    void refreshSizeFromPreset(int index);
    QString translate(const char *key) const;

    QtApplicationService *m_service;
    Project *m_project;
    QSpinBox *m_sizeX;
    QSpinBox *m_sizeY;
    QComboBox *m_presetCombo;
    Vector4 m_lastViewportColor;
    bool m_lastViewportWithTransparent;
    Vector2SI32 m_viewportImageSize;
    bool m_viewportWithTransparent;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPORTSETTINGDIALOG_H_ */