/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTVIEWPANEL_H_
#define NANOEM_EMAPP_QT_QTVIEWPANEL_H_

#include <QGroupBox>

class QCheckBox;
class QPushButton;
class QTimer;

namespace nanoem {

class Project;

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui view panel (ImGuiWindow::drawViewPanel).
 *
 *       Provides the six camera direction preset buttons (Front/Back/Up/Left/Right/Bottom) and the
 *       follow model/follow bone checkboxes. The direction buttons reuse the camera preset menu
 *       dispatch path so behavior matches the ImGui panel and the View menu exactly.
 */
class QtViewPanel final : public QGroupBox {
public:
    explicit QtViewPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    enum ViewDirectionType {
        kViewDirectionTypeFront = 0,
        kViewDirectionTypeBack,
        kViewDirectionTypeUp,
        kViewDirectionTypeLeft,
        kViewDirectionTypeRight,
        kViewDirectionTypeBottom,
        kViewDirectionTypeMaxEnum,
    };

    Project *currentProject();
    void refresh();
    void applyViewDirection(ViewDirectionType value);

    QtApplicationService *m_service;
    QTimer *m_timer;
    QPushButton *m_viewDirectionButtons[kViewDirectionTypeMaxEnum];
    QCheckBox *m_followModelCheckBox = nullptr;
    QCheckBox *m_followBoneCheckBox = nullptr;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTVIEWPANEL_H_ */
