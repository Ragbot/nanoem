/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTPLAYPANEL_H_
#define NANOEM_EMAPP_QT_QTPLAYPANEL_H_

#include <QGroupBox>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QSpinBox;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui play panel (ImGuiWindow::drawPlayPanel).
 *
 *       Provides the play/pause/resume button, the loop toggle, the volume slider and the
 *       playing segment (from/to frame) editors. All controls mutate the Project directly,
 *       mirroring the ImGui panel behavior.
 */
class QtPlayPanel final : public QGroupBox {
public:
    explicit QtPlayPanel(QtApplicationService *service, QWidget *parent);

private:
    Project *currentProject();
    void refresh();
    void applyPlayingSegment();
    void updateVolumeLabel(int volume);

    QtApplicationService *m_service;
    QTimer *m_timer;
    QPushButton *m_playPauseButton = nullptr;
    QCheckBox *m_loopCheckBox = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QCheckBox *m_enableFromCheckBox = nullptr;
    QSpinBox *m_fromSpinBox = nullptr;
    QCheckBox *m_enableToCheckBox = nullptr;
    QSpinBox *m_toSpinBox = nullptr;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTPLAYPANEL_H_ */
