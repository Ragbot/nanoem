/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTTIMELINEPANEL_H_
#define NANOEM_EMAPP_QT_QTTIMELINEPANEL_H_

#include "emapp/Forward.h"

#include <QPoint>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QTimer;
class QWheelEvent;

namespace nanoem {

class ITrack;
class Project;

namespace qt {

class QtApplicationService;
class QtTimelineGrid;

/**
 * @brief Native replacement of the ImGui timeline column (ImGuiWindow::drawTimeline and its
 *        seeker/undo/track/marker/keyframe action children).
 *
 *       The top rows mirror the ImGui seeker, undo and keyframe clipboard panels, all driven
 *       through the same direct Project calls (including the confirmer seek path) the ImGui
 *       side used. The scrollable grid below shows the track list and the frame ruler with the
 *       per track keyframe markers: clicking a track activates its bone/morph/accessory like
 *       ImGuiWindow::drawTrack, clicking a marker selects the keyframe and clicking empty
 *       marker space seeks. The panel publishes the canCopy/canPaste events the menus subscribe
 *       to since ImGuiWindow no longer draws the timeline once kNativePanelTypeTimeline is set.
 */
class QtTimelinePanel final : public QWidget {
public:
    QtTimelinePanel(QtApplicationService *service, QWidget *parent);

private:
    Project *currentProject() const;
    void connectSeekerSignals();
    void connectUndoSignals();
    void connectKeyframeActionSignals();
    void connectSelectionSegmentSignals();
    void refresh();

    QtApplicationService *m_service;
    QtTimelineGrid *m_grid;
    QSpinBox *m_frameIndexSpinBox;
    QPushButton *m_prevKeyframeButton;
    QPushButton *m_nextKeyframeButton;
    QPushButton *m_backwardButton;
    QPushButton *m_forwardButton;
    QPushButton *m_undoButton;
    QPushButton *m_redoButton;
    QPushButton *m_copyButton;
    QPushButton *m_pasteButton;
    QPushButton *m_cutButton;
    QPushButton *m_symmetricPasteButton;
    QPushButton *m_selectColumnButton;
    QPushButton *m_removeButton;
    QSpinBox *m_selectionFromSpinBox;
    QSpinBox *m_selectionToSpinBox;
    QPushButton *m_scaleKeyframesButton;
    QTimer *m_timer;
    int m_lastCanCopyState = -1;
    int m_lastCanPasteState = -1;
};

/**
 * @brief The scrollable track list + keyframe marker grid of the timeline column.
 *
 *       Rows are the flattened Project::allTracks() list (expanded tracks contribute their
 *       children) shared by both halves so a single vertical scroll offset drives them, like
 *       the synced ImGui "tracks"/"markers" child windows. Frame columns center on the current
 *       frame like ImGuiWindow::drawAllMarkersPanel and rows are rebuilt at every entry point
 *       so a rebuilt track list can never be observed through stale pointers.
 */
class QtTimelineGrid final : public QWidget {
public:
    QtTimelineGrid(QtApplicationService *service, QWidget *parent);

    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *release) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum PressMode {
        kPressModeNone,
        kPressModeSeeking,
        kPressModeSelectingKeyframe,
    };

    Project *currentProject() const;
    void rebuildRows();
    int rowHeight() const;
    int trackWidth() const;
    int markerColumnWidth() const;
    nanoem_frame_index_t firstVisibleFrameIndex() const;
    nanoem_frame_index_t frameAtWithBase(const QPoint &pos, nanoem_frame_index_t baseFrameIndex) const;
    int rowAt(const QPoint &pos) const;
    void seekTo(Project *project, nanoem_frame_index_t frameIndex);
    void handleTrackPressed(Project *project, ITrack *track, bool withModifier);
    void handleTrackExpansion(ITrack *track);
    void selectKeyframe(Project *project, ITrack *track, nanoem_frame_index_t frameIndex, bool withModifier);
    void clearAllKeyframeSelection(Project *project);
    bool queryKeyframeState(
        Project *project, ITrack *track, nanoem_frame_index_t frameIndex, bool &selected, bool &physicsEnabled) const;
    void clampScrollOffset();

    QtApplicationService *m_service;
    QVector<ITrack *> m_rows;
    int m_scrollY = 0;
    PressMode m_pressMode = kPressModeNone;
    nanoem_frame_index_t m_pressBaseFrameIndex = 0;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTTIMELINEPANEL_H_ */
