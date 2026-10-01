/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTMORPHPANEL_H_
#define NANOEM_EMAPP_QT_QTMORPHPANEL_H_

#include <QGroupBox>

#include <array>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QTimer;

namespace nanoem {

class Model;
class Project;

namespace internal {
class DraggingMorphSliderState;
} /* namespace internal */

namespace qt {

class QtApplicationService;

/**
 * @brief Native replacement of the ImGui morph panel (ImGuiWindow::drawMorphPanel).
 *
 *       Provides the four morph categories (eye/lip/eyebrow/other), each with its morph selector,
 *       weight editor (committed through DraggingMorphSliderState so a drag becomes one undo entry
 *       with bind pose restore semantics) and register keyframe button. Shown only while a model
 *       is active.
 */
class QtMorphPanel final : public QGroupBox {
public:
    QtMorphPanel(QtApplicationService *service, QWidget *parent);

    /* compressible: the dock row must fit every panel like the fixed width ImGui strip */
    QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
    struct Row {
        int category;
        QComboBox *comboBox = nullptr;
        QDoubleSpinBox *weightSpinBox = nullptr;
        QPushButton *registerButton = nullptr;
        const Model *selectorModel = nullptr;
        internal::DraggingMorphSliderState *state = nullptr;
        const Model *stateModel = nullptr;
    };

    Project *currentProject();
    Model *activeModel();
    void refresh();
    void refreshRow(Row &row, Project *project, Model *model);
    void selectMorph(Row &row, int index);
    void handleWeightChanged(Row &row, double value);
    void commitWeight(Row &row);

    QtApplicationService *m_service;
    QTimer *m_timer;
    std::array<Row, 4> m_rows;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTMORPHPANEL_H_ */
