/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtTimelinePanel.h"
#include "QtApplicationService.h"

#include "emapp/Accessory.h"
#include "emapp/CommandRegistrator.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IMotionKeyframeSelection.h"
#include "emapp/IModelObjectSelection.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITrack.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace nanoem {
namespace qt {
namespace {

/* matches ImGuiWindow::kTimelineTrackMaxWidth */
const int kTimelineTrackMaxWidth = 150;
const int kTimelineScrollStep = 3;
const int kTimelineExpandIconWidth = 18;
const int kFrameIndexMaximum = 999999;
/* matches ImGuiWindow::drawAllMarkersPanel and drawMarkerRhombus */
const QColor kFrameLineColorCurrent(0x00, 0xff, 0x00);
const QColor kFrameLineColorMajor(0x7f, 0x7f, 0x7f);
const QColor kFrameLineColorMinor(0x4f, 0x4f, 0x4f);
const QColor kRowLineColor(0x4f, 0x4f, 0x4f);
const QColor kKeyframeColorDefault(0xaf, 0xaf, 0xaf);
const QColor kKeyframeColorSelected(0xff, 0x7f, 0x00);
const QColor kTrackNameColorSelected(0xff, 0x7f, 0x00);

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

/* mirrors the deselect pass at the end of ImGuiWindow::drawAllTracksPanel */
static void
clearTrackListSelection(const Project::TrackList *allTracks)
{
    for (Project::TrackList::const_iterator it = allTracks->begin(), end = allTracks->end(); it != end; ++it) {
        ITrack *track = *it;
        track->setSelected(false);
        if (track->isExpandable()) {
            const Project::TrackList children(track->children());
            for (Project::TrackList::const_iterator it2 = children.begin(), end2 = children.end(); it2 != end2; ++it2) {
                (*it2)->setSelected(false);
            }
        }
    }
}

/* mirrors ImGuiWindow::searchNearestKeyframe: closest keyframes of the active bone motion,
   falling back to the camera motion when there is no active model bone */
static void
searchNearestKeyframe(const Project *project, nanoem_frame_index_t frameIndex, nanoem_frame_index_t &previous,
    bool &hasPrevious, nanoem_frame_index_t &next, bool &hasNext)
{
    hasPrevious = hasNext = false;
    previous = next = 0;
    bool found = false;
    if (const Model *model = project->activeModel()) {
        const nanoem_model_bone_t *bonePtr = model->activeBone();
        const Motion *motion = project->resolveMotion(model);
        if (bonePtr && motion) {
            nanoem_motion_bone_keyframe_t *previousKeyframe = nullptr, *nextKeyframe = nullptr;
            nanoemMotionSearchClosestBoneKeyframes(motion->data(),
                nanoemModelBoneGetName(bonePtr, NANOEM_LANGUAGE_TYPE_JAPANESE), frameIndex, &previousKeyframe,
                &nextKeyframe);
            if (previousKeyframe) {
                previous = nanoemMotionKeyframeObjectGetFrameIndex(
                    nanoemMotionBoneKeyframeGetKeyframeObject(previousKeyframe));
                hasPrevious = true;
            }
            if (nextKeyframe) {
                next = nanoemMotionKeyframeObjectGetFrameIndex(nanoemMotionBoneKeyframeGetKeyframeObject(nextKeyframe));
                hasNext = true;
            }
            found = true;
        }
    }
    if (!found) {
        nanoem_motion_camera_keyframe_t *previousKeyframe = nullptr, *nextKeyframe = nullptr;
        nanoemMotionSearchClosestCameraKeyframes(
            project->cameraMotion()->data(), frameIndex, &previousKeyframe, &nextKeyframe);
        if (previousKeyframe) {
            previous = nanoemMotionKeyframeObjectGetFrameIndex(
                nanoemMotionCameraKeyframeGetKeyframeObject(previousKeyframe));
            hasPrevious = true;
        }
        if (nextKeyframe) {
            next = nanoemMotionKeyframeObjectGetFrameIndex(nanoemMotionCameraKeyframeGetKeyframeObject(nextKeyframe));
            hasNext = true;
        }
    }
}

} /* namespace anonymous */

QtTimelinePanel::QtTimelinePanel(QtApplicationService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    /* rows follow ImGuiWindow::drawTimeline: seeker, undo, then the tracks/markers grid with the
       keyframe action panels underneath */
    auto *seekerRow = new QHBoxLayout;
    seekerRow->setSpacing(2);
    m_prevKeyframeButton = new QPushButton(QStringLiteral("<<|"), this);
    m_backwardButton = new QPushButton(QStringLiteral("<"), this);
    m_frameIndexSpinBox = new QSpinBox(this);
    m_frameIndexSpinBox->setRange(0, kFrameIndexMaximum);
    m_forwardButton = new QPushButton(QStringLiteral(">"), this);
    m_nextKeyframeButton = new QPushButton(QStringLiteral("|>>"), this);
    /* matches ImGui::PushButtonRepeat(true) in drawSeekerPanel */
    QPushButton *seekButtons[] = { m_prevKeyframeButton, m_backwardButton, m_forwardButton, m_nextKeyframeButton };
    for (QPushButton *button : seekButtons) {
        button->setAutoRepeat(true);
        button->setAutoRepeatInterval(100);
        button->setFixedWidth(40);
        seekerRow->addWidget(button);
    }
    seekerRow->insertWidget(2, m_frameIndexSpinBox, 1);
    layout->addLayout(seekerRow);
    auto *undoRow = new QHBoxLayout;
    m_undoButton = new QPushButton(translate(service, "nanoem.gui.undo"), this);
    m_redoButton = new QPushButton(translate(service, "nanoem.gui.redo"), this);
    undoRow->addWidget(m_undoButton);
    undoRow->addWidget(m_redoButton);
    layout->addLayout(undoRow);
    m_grid = new QtTimelineGrid(service, this);
    layout->addWidget(m_grid, 1);
    auto *keyframeRow = new QHBoxLayout;
    keyframeRow->setSpacing(2);
    m_copyButton = new QPushButton(translate(service, "nanoem.gui.keyframe.copy"), this);
    m_pasteButton = new QPushButton(translate(service, "nanoem.gui.keyframe.paste"), this);
    m_cutButton = new QPushButton(translate(service, "nanoem.gui.keyframe.cut"), this);
    keyframeRow->addWidget(m_copyButton);
    keyframeRow->addWidget(m_pasteButton);
    keyframeRow->addWidget(m_cutButton);
    layout->addLayout(keyframeRow);
    auto *keyframeRow2 = new QHBoxLayout;
    keyframeRow2->setSpacing(2);
    m_symmetricPasteButton = new QPushButton(translate(service, "nanoem.gui.keyframe.inverse-paste"), this);
    m_selectColumnButton = new QPushButton(translate(service, "nanoem.gui.keyframe.select-column"), this);
    m_removeButton = new QPushButton(translate(service, "nanoem.gui.keyframe.remove"), this);
    keyframeRow2->addWidget(m_symmetricPasteButton);
    keyframeRow2->addWidget(m_selectColumnButton);
    keyframeRow2->addWidget(m_removeButton);
    layout->addLayout(keyframeRow2);
    connectSeekerSignals();
    connectUndoSignals();
    connectKeyframeActionSignals();
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        refresh();
        m_grid->refresh();
    });
    m_timer->start();
    refresh();
    m_grid->refresh();
}

Project *
QtTimelinePanel::currentProject() const
{
    return m_service->projectHolder()->currentProject();
}

void
QtTimelinePanel::connectSeekerSignals()
{
    connect(m_backwardButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
            if (frameIndex > 0) {
                project->seek(frameIndex - 1, false);
            }
        }
    });
    connect(m_forwardButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
            if (frameIndex < Motion::kMaxFrameIndex) {
                project->seek(frameIndex + 1, false);
            }
        }
    });
    connect(m_prevKeyframeButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
            nanoem_frame_index_t previous = 0, next = 0;
            bool hasPrevious = false, hasNext = false;
            searchNearestKeyframe(project, frameIndex, previous, hasPrevious, next, hasNext);
            if (hasPrevious && previous != frameIndex) {
                project->seek(previous, false);
            }
        }
    });
    connect(m_nextKeyframeButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
            nanoem_frame_index_t previous = 0, next = 0;
            bool hasPrevious = false, hasNext = false;
            searchNearestKeyframe(project, frameIndex, previous, hasPrevious, next, hasNext);
            if (hasNext && next > frameIndex) {
                project->seek(next, false);
            }
        }
    });
    connect(m_frameIndexSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (Project *project = currentProject()) {
            project->seek(nanoem_frame_index_t(value), false);
        }
    });
}

void
QtTimelinePanel::connectUndoSignals()
{
    connect(m_undoButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            project->handleUndoAction();
        }
    });
    connect(m_redoButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            project->handleRedoAction();
        }
    });
}

void
QtTimelinePanel::connectKeyframeActionSignals()
{
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            Error error;
            project->copyAllSelectedKeyframes(error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_pasteButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            Error error;
            project->pasteAllSelectedKeyframes(project->currentLocalFrameIndex(), error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_cutButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            Error error;
            project->copyAllSelectedKeyframes(error);
            CommandRegistrator registrator(project);
            registrator.registerRemoveAllSelectedKeyframesCommand();
            error.notify(project->eventPublisher());
        }
    });
    connect(m_symmetricPasteButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            Error error;
            project->symmetricPasteAllSelectedKeyframes(project->currentLocalFrameIndex(), error);
            error.notify(project->eventPublisher());
        }
    });
    connect(m_selectColumnButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            project->selectAllKeyframesInColumn();
        }
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        if (Project *project = currentProject()) {
            CommandRegistrator registrator(project);
            registrator.registerRemoveAllSelectedKeyframesCommand();
        }
    });
}

void
QtTimelinePanel::refresh()
{
    Project *project = currentProject();
    if (!project) {
        return;
    }
    const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
    const bool buttonEnabled = !project->isPlaying();
    nanoem_frame_index_t previous = 0, next = 0;
    bool hasPrevious = false, hasNext = false;
    if (buttonEnabled) {
        searchNearestKeyframe(project, frameIndex, previous, hasPrevious, next, hasNext);
    }
    m_prevKeyframeButton->setEnabled(buttonEnabled && hasPrevious);
    m_nextKeyframeButton->setEnabled(buttonEnabled && hasNext && next > frameIndex);
    m_backwardButton->setEnabled(buttonEnabled && frameIndex > 0);
    m_forwardButton->setEnabled(buttonEnabled && frameIndex < Motion::kMaxFrameIndex);
    {
        const QSignalBlocker blocker(m_frameIndexSpinBox);
        m_frameIndexSpinBox->setValue(int(qMin(frameIndex, nanoem_frame_index_t(kFrameIndexMaximum))));
    }
    m_undoButton->setEnabled(project->canUndo());
    m_redoButton->setEnabled(project->canRedo());
    /* mirrors ImGuiWindow::drawKeyframeActionPanel including the enabled states it published */
    const bool hasKeyframeSelection = buttonEnabled && project->hasKeyframeSelection(),
               hasKeyframeCopied = buttonEnabled && !project->isMotionClipboardEmpty();
    m_copyButton->setEnabled(hasKeyframeSelection);
    m_cutButton->setEnabled(hasKeyframeSelection);
    m_removeButton->setEnabled(hasKeyframeSelection);
    m_pasteButton->setEnabled(hasKeyframeCopied);
    m_symmetricPasteButton->setEnabled(hasKeyframeCopied);
    m_selectColumnButton->setEnabled(buttonEnabled);
    /* ImGuiWindow published these through lazy commands; with the timeline gated the menus only
       get their copy/paste state from here */
    if (m_lastCanCopyState != (hasKeyframeSelection ? 1 : 0)) {
        m_lastCanCopyState = hasKeyframeSelection ? 1 : 0;
        project->eventPublisher()->publishCanCopyEvent(hasKeyframeSelection);
    }
    if (m_lastCanPasteState != (hasKeyframeCopied ? 1 : 0)) {
        m_lastCanPasteState = hasKeyframeCopied ? 1 : 0;
        project->eventPublisher()->publishCanPasteEvent(hasKeyframeCopied);
    }
}

QtTimelineGrid::QtTimelineGrid(QtApplicationService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    setMinimumHeight(80);
}

Project *
QtTimelineGrid::currentProject() const
{
    return m_service->projectHolder()->currentProject();
}

void
QtTimelineGrid::refresh()
{
    rebuildRows();
    clampScrollOffset();
    update();
}

void
QtTimelineGrid::rebuildRows()
{
    m_rows.clear();
    Project *project = currentProject();
    if (!project) {
        return;
    }
    const Project::TrackList *allTracks = project->allTracks();
    for (Project::TrackList::const_iterator it = allTracks->begin(), end = allTracks->end(); it != end; ++it) {
        ITrack *track = *it;
        m_rows.push_back(track);
        if (track->isExpanded()) {
            const Project::TrackList children(track->children());
            for (Project::TrackList::const_iterator it2 = children.begin(), end2 = children.end(); it2 != end2; ++it2) {
                m_rows.push_back(*it2);
            }
        }
    }
}

int
QtTimelineGrid::rowHeight() const
{
    return fontMetrics().height() + 4;
}

int
QtTimelineGrid::trackWidth() const
{
    return qBound(fontMetrics().height() * 4, int(width() * 0.3f), kTimelineTrackMaxWidth);
}

int
QtTimelineGrid::markerColumnWidth() const
{
    return fontMetrics().height() + 1;
}

nanoem_frame_index_t
QtTimelineGrid::firstVisibleFrameIndex() const
{
    if (Project *project = currentProject()) {
        const int center = qMax(1, (width() - trackWidth()) / markerColumnWidth()) / 2;
        const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
        if (frameIndex >= nanoem_frame_index_t(center)) {
            return frameIndex - nanoem_frame_index_t(center);
        }
    }
    return 0;
}

nanoem_frame_index_t
QtTimelineGrid::frameAtWithBase(const QPoint &pos, nanoem_frame_index_t baseFrameIndex) const
{
    const int column = (pos.x() - (trackWidth() + 1)) / markerColumnWidth();
    return column >= 0 ? baseFrameIndex + nanoem_frame_index_t(column) : baseFrameIndex;
}

int
QtTimelineGrid::rowAt(const QPoint &pos) const
{
    const int row = (pos.y() + m_scrollY) / rowHeight();
    return (pos.y() >= 0 && row >= 0 && row < int(m_rows.size())) ? row : -1;
}

void
QtTimelineGrid::clampScrollOffset()
{
    const int maximum = qMax(0, int(m_rows.size()) * rowHeight() - height());
    m_scrollY = qBound(0, m_scrollY, maximum);
}

void
QtTimelineGrid::seekTo(Project *project, nanoem_frame_index_t frameIndex)
{
    /* matches LazySetFrameIndexCommand: the confirmer path used by the ImGui timeline */
    project->seek(frameIndex, false);
}

bool
QtTimelineGrid::queryKeyframeState(
    Project *project, ITrack *track, nanoem_frame_index_t frameIndex, bool &selected, bool &physicsEnabled) const
{
    selected = physicsEnabled = false;
    switch (track->type()) {
    case ITrack::kTypeCamera: {
        Motion *motion = project->cameraMotion();
        if (const nanoem_motion_camera_keyframe_t *keyframe = motion->findCameraKeyframe(frameIndex)) {
            selected = motion->selection()->contains(keyframe);
            return true;
        }
        return false;
    }
    case ITrack::kTypeLight: {
        Motion *motion = project->lightMotion();
        if (const nanoem_motion_light_keyframe_t *keyframe = motion->findLightKeyframe(frameIndex)) {
            selected = motion->selection()->contains(keyframe);
            return true;
        }
        return false;
    }
    case ITrack::kTypeSelfShadow: {
        Motion *motion = project->selfShadowMotion();
        if (const nanoem_motion_self_shadow_keyframe_t *keyframe = motion->findSelfShadowKeyframe(frameIndex)) {
            selected = motion->selection()->contains(keyframe);
            return true;
        }
        return false;
    }
    case ITrack::kTypeAccessory: {
        Accessory *accessory = static_cast<Accessory *>(track->opaque());
        Motion *motion = accessory ? project->resolveMotion(accessory) : nullptr;
        if (motion) {
            if (const nanoem_motion_accessory_keyframe_t *keyframe = motion->findAccessoryKeyframe(frameIndex)) {
                selected = motion->selection()->contains(keyframe);
                return true;
            }
        }
        return false;
    }
    case ITrack::kTypeModelRoot: {
        Model *model = static_cast<Model *>(track->opaque());
        Motion *motion = model ? project->resolveMotion(model) : nullptr;
        if (motion) {
            if (const nanoem_motion_model_keyframe_t *keyframe = motion->findModelKeyframe(frameIndex)) {
                selected = motion->selection()->contains(keyframe);
                return true;
            }
        }
        return false;
    }
    case ITrack::kTypeModelBone: {
        Model *model = project->activeModel();
        Motion *motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_bone_t *bonePtr = static_cast<const nanoem_model_bone_t *>(track->opaque());
        if (motion && bonePtr) {
            const nanoem_unicode_string_t *name = nanoemModelBoneGetName(bonePtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
            if (const nanoem_motion_bone_keyframe_t *keyframe = motion->findBoneKeyframe(name, frameIndex)) {
                selected = motion->selection()->contains(keyframe);
                physicsEnabled =
                    nanoemMotionBoneKeyframeIsPhysicsSimulationEnabled(keyframe) && model->isRigidBodyBound(bonePtr);
                return true;
            }
        }
        return false;
    }
    case ITrack::kTypeModelMorph: {
        Model *model = project->activeModel();
        Motion *motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_morph_t *morphPtr = static_cast<const nanoem_model_morph_t *>(track->opaque());
        if (motion && morphPtr) {
            const nanoem_unicode_string_t *name = nanoemModelMorphGetName(morphPtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
            if (const nanoem_motion_morph_keyframe_t *keyframe = motion->findMorphKeyframe(name, frameIndex)) {
                selected = motion->selection()->contains(keyframe);
                return true;
            }
        }
        return false;
    }
    case ITrack::kTypeModelLabel: {
        /* like ImGuiWindow::drawMarkerRhombus an expanded label draws nothing and a collapsed
           one shows the keyframe of its first label item only */
        if (track->isExpanded()) {
            return false;
        }
        Model *model = project->activeModel();
        Motion *motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_label_t *labelPtr =
            motion ? static_cast<const nanoem_model_label_t *>(track->opaque()) : nullptr;
        if (labelPtr) {
            nanoem_rsize_t numItems = 0;
            nanoem_model_label_item_t *const *items = nanoemModelLabelGetAllItemObjects(labelPtr, &numItems);
            if (items && numItems > 0) {
                const nanoem_model_label_item_t *item = items[0];
                if (const nanoem_model_bone_t *bonePtr = nanoemModelLabelItemGetBoneObject(item)) {
                    const nanoem_unicode_string_t *name =
                        nanoemModelBoneGetName(bonePtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
                    if (const nanoem_motion_bone_keyframe_t *keyframe = motion->findBoneKeyframe(name, frameIndex)) {
                        selected = motion->selection()->contains(keyframe);
                        return true;
                    }
                }
                else if (const nanoem_model_morph_t *morphPtr = nanoemModelLabelItemGetMorphObject(item)) {
                    const nanoem_unicode_string_t *name =
                        nanoemModelMorphGetName(morphPtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
                    if (const nanoem_motion_morph_keyframe_t *keyframe = motion->findMorphKeyframe(name, frameIndex)) {
                        selected = motion->selection()->contains(keyframe);
                        return true;
                    }
                }
            }
        }
        return false;
    }
    default:
        return false;
    }
}

void
QtTimelineGrid::clearAllKeyframeSelection(Project *project)
{
    /* mirrors ImGuiWindow::clearAllKeyframeSelection */
    if (Motion *activeMotion = project->resolveMotion(project->activeModel())) {
        activeMotion->selection()->clearAllKeyframes(NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL);
    }
    else {
        project->cameraMotion()->selection()->clearAllKeyframes(NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL);
        project->lightMotion()->selection()->clearAllKeyframes(NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL);
        project->selfShadowMotion()->selection()->clearAllKeyframes(NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL);
        const Project::AccessoryList *accessories = project->allAccessories();
        for (Project::AccessoryList::const_iterator it = accessories->begin(), end = accessories->end(); it != end;
             ++it) {
            Motion *motion = project->resolveMotion(*it);
            motion->selection()->clearAllKeyframes(NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL);
        }
    }
}

void
QtTimelineGrid::selectKeyframe(Project *project, ITrack *track, nanoem_frame_index_t frameIndex, bool withModifier)
{
    /* like the release pass of ImGuiWindow::drawAllMarkersPanel: clear first unless the control
       key is held, then add the clicked keyframe(s) */
    if (!withModifier) {
        clearAllKeyframeSelection(project);
    }
    Motion *motion = nullptr;
    switch (track->type()) {
    case ITrack::kTypeCamera:
        motion = project->cameraMotion();
        if (const nanoem_motion_camera_keyframe_t *keyframe = motion->findCameraKeyframe(frameIndex)) {
            motion->selection()->add(keyframe);
        }
        break;
    case ITrack::kTypeLight:
        motion = project->lightMotion();
        if (const nanoem_motion_light_keyframe_t *keyframe = motion->findLightKeyframe(frameIndex)) {
            motion->selection()->add(keyframe);
        }
        break;
    case ITrack::kTypeSelfShadow:
        motion = project->selfShadowMotion();
        if (const nanoem_motion_self_shadow_keyframe_t *keyframe = motion->findSelfShadowKeyframe(frameIndex)) {
            motion->selection()->add(keyframe);
        }
        break;
    case ITrack::kTypeAccessory: {
        Accessory *accessory = static_cast<Accessory *>(track->opaque());
        motion = accessory ? project->resolveMotion(accessory) : nullptr;
        if (motion) {
            if (const nanoem_motion_accessory_keyframe_t *keyframe = motion->findAccessoryKeyframe(frameIndex)) {
                motion->selection()->add(keyframe);
            }
        }
        break;
    }
    case ITrack::kTypeModelRoot: {
        Model *model = static_cast<Model *>(track->opaque());
        motion = model ? project->resolveMotion(model) : nullptr;
        if (motion) {
            if (const nanoem_motion_model_keyframe_t *keyframe = motion->findModelKeyframe(frameIndex)) {
                motion->selection()->add(keyframe);
            }
        }
        break;
    }
    case ITrack::kTypeModelBone: {
        Model *model = project->activeModel();
        motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_bone_t *bonePtr = static_cast<const nanoem_model_bone_t *>(track->opaque());
        if (motion && bonePtr) {
            const nanoem_unicode_string_t *name = nanoemModelBoneGetName(bonePtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
            if (const nanoem_motion_bone_keyframe_t *keyframe = motion->findBoneKeyframe(name, frameIndex)) {
                motion->selection()->add(keyframe);
            }
        }
        break;
    }
    case ITrack::kTypeModelMorph: {
        Model *model = project->activeModel();
        motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_morph_t *morphPtr = static_cast<const nanoem_model_morph_t *>(track->opaque());
        if (motion && morphPtr) {
            const nanoem_unicode_string_t *name = nanoemModelMorphGetName(morphPtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
            if (const nanoem_motion_morph_keyframe_t *keyframe = motion->findMorphKeyframe(name, frameIndex)) {
                motion->selection()->add(keyframe);
            }
        }
        break;
    }
    case ITrack::kTypeModelLabel: {
        if (track->isExpanded()) {
            break;
        }
        Model *model = project->activeModel();
        motion = model ? project->resolveMotion(model) : nullptr;
        const nanoem_model_label_t *labelPtr =
            motion ? static_cast<const nanoem_model_label_t *>(track->opaque()) : nullptr;
        if (labelPtr) {
            nanoem_rsize_t numItems = 0;
            nanoem_model_label_item_t *const *items = nanoemModelLabelGetAllItemObjects(labelPtr, &numItems);
            if (items && numItems > 0) {
                const nanoem_model_label_item_t *item = items[0];
                if (const nanoem_model_bone_t *bonePtr = nanoemModelLabelItemGetBoneObject(item)) {
                    const nanoem_unicode_string_t *name =
                        nanoemModelBoneGetName(bonePtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
                    if (const nanoem_motion_bone_keyframe_t *keyframe = motion->findBoneKeyframe(name, frameIndex)) {
                        motion->selection()->add(keyframe);
                    }
                }
                else if (const nanoem_model_morph_t *morphPtr = nanoemModelLabelItemGetMorphObject(item)) {
                    const nanoem_unicode_string_t *name =
                        nanoemModelMorphGetName(morphPtr, NANOEM_LANGUAGE_TYPE_FIRST_ENUM);
                    if (const nanoem_motion_morph_keyframe_t *keyframe = motion->findMorphKeyframe(name, frameIndex)) {
                        motion->selection()->add(keyframe);
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }
    update();
}

void
QtTimelineGrid::handleTrackExpansion(ITrack *track)
{
    if (track->isExpandable()) {
        track->setExpanded(!track->isExpanded());
        refresh();
    }
}

void
QtTimelineGrid::handleTrackPressed(Project *project, ITrack *track, bool withModifier)
{
    /* mirrors ImGuiWindow::drawTrack: activation semantics per track type with the rest of the
       track list deselected first so a plain click yields a single selection */
    Model *activeModel = project->activeModel();
    const ITrack::Type type = track->type();
    const bool expandable = track->isExpandable();
    clearTrackListSelection(project->allTracks());
    if (expandable && activeModel) {
        const Project::TrackList children(track->children());
        IModelObjectSelection *selection = activeModel->selection();
        if (!withModifier) {
            selection->removeAllBones();
        }
        for (Project::TrackList::const_iterator it = children.begin(), end = children.end(); it != end; ++it) {
            ITrack *child = *it;
            if (child->type() == ITrack::kTypeModelBone) {
                const nanoem_model_bone_t *bonePtr = static_cast<const nanoem_model_bone_t *>(child->opaque());
                selection->addBone(bonePtr);
            }
            child->setSelected(true);
        }
        const Project::EditingMode editingMode = project->editingMode();
        if ((editingMode == Project::kEditingModeMove && !selection->areAllBonesMovable()) ||
            (editingMode == Project::kEditingModeRotate && !selection->areAllBonesRotateable())) {
            project->setEditingMode(Project::kEditingModeNone);
        }
    }
    else if (type == ITrack::kTypeAccessory) {
        project->setActiveAccessory(static_cast<Accessory *>(track->opaque()));
    }
    else if (type == ITrack::kTypeModelLabel && activeModel) {
        const nanoem_model_label_t *labelPtr = static_cast<const nanoem_model_label_t *>(track->opaque());
        nanoem_rsize_t numItems = 0;
        nanoem_model_label_item_t *const *items = nanoemModelLabelGetAllItemObjects(labelPtr, &numItems);
        if (items && numItems > 0) {
            const nanoem_model_label_item_t *item = items[0];
            IModelObjectSelection *selection = activeModel->selection();
            if (const nanoem_model_bone_t *bonePtr = nanoemModelLabelItemGetBoneObject(item)) {
                if (!withModifier) {
                    selection->removeAllBones();
                }
                activeModel->setActiveBone(bonePtr);
            }
            else if (const nanoem_model_morph_t *morphPtr = nanoemModelLabelItemGetMorphObject(item)) {
                activeModel->setActiveMorph(morphPtr);
            }
        }
    }
    else if (type == ITrack::kTypeModelBone && activeModel) {
        const nanoem_model_bone_t *bonePtr = static_cast<const nanoem_model_bone_t *>(track->opaque());
        IModelObjectSelection *selection = activeModel->selection();
        if (!withModifier) {
            selection->removeAllBones();
        }
        activeModel->setActiveBone(bonePtr);
    }
    else if (type == ITrack::kTypeModelMorph && activeModel) {
        const nanoem_model_morph_t *morphPtr = static_cast<const nanoem_model_morph_t *>(track->opaque());
        activeModel->setActiveMorph(morphPtr);
    }
    track->setSelected(true);
    update();
}

void
QtTimelineGrid::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    /* the track list may be rebuilt between timer ticks (e.g. a model load), so always flatten
       fresh rows before touching them */
    rebuildRows();
    Project *project = currentProject();
    if (!project || m_rows.isEmpty()) {
        return;
    }
    const int rh = rowHeight(), tw = trackWidth(), markersX = tw + 1, colW = markerColumnWidth(),
              radius = qMax(3, int(fontMetrics().height() * 0.4));
    const nanoem_frame_index_t startFrameIndex = firstVisibleFrameIndex(),
                               currentFrameIndex = project->currentLocalFrameIndex();
    const nanoem_u32_t numVisibleFrames = nanoem_u32_t(qMax(1, (width() - markersX) / colW));
    const int nameFlags = int(Qt::AlignLeft) | int(Qt::AlignVCenter) | int(Qt::TextSingleLine),
              iconFlags = int(Qt::AlignLeft) | int(Qt::AlignVCenter);
    /* frame column lines: green on the current frame, major every 5 frames, minor otherwise */
    for (nanoem_u32_t i = 0; i <= numVisibleFrames; i++) {
        const nanoem_frame_index_t frameIndex = startFrameIndex + i;
        const int x = markersX + int(i) * colW + radius;
        const QColor &color = frameIndex == currentFrameIndex   ? kFrameLineColorCurrent
                              : frameIndex % 5 == 0             ? kFrameLineColorMajor
                                                                : kFrameLineColorMinor;
        painter.setPen(QPen(color, 1.0));
        painter.drawLine(x, 0, x, height());
    }
    const int lastRow = qMin(int(m_rows.size()) - 1, (m_scrollY + height()) / rh);
    for (int row = qMax(0, m_scrollY / rh); row <= lastRow; row++) {
        const int y = row * rh - m_scrollY;
        ITrack *track = m_rows[row];
        painter.setPen(QPen(kRowLineColor, 1.0));
        painter.drawLine(0, y + rh, width(), y + rh);
        int textX = 4;
        painter.setPen(track->isSelected() ? kTrackNameColorSelected : palette().color(QPalette::Text));
        if (track->isExpandable()) {
            painter.drawText(QRectF(2, y, 14, rh), iconFlags,
                track->isExpanded() ? QStringLiteral("v") : QStringLiteral(">"));
            textX = kTimelineExpandIconWidth;
        }
        else if (track->isFixed()) {
            painter.drawText(QRectF(2, y, 14, rh), iconFlags, QStringLiteral("*"));
            textX = kTimelineExpandIconWidth;
        }
        painter.drawText(QRectF(textX, y, qMax(4, tw - textX - 2), rh), nameFlags,
            QString::fromUtf8(track->nameConstString()));
        for (nanoem_u32_t i = 0; i < numVisibleFrames; i++) {
            bool selected = false, physicsEnabled = false;
            const nanoem_frame_index_t frameIndex = startFrameIndex + i;
            if (queryKeyframeState(project, track, frameIndex, selected, physicsEnabled)) {
                const qreal cx = markersX + qreal(i) * colW + radius + 0.5, cy = y + rh * 0.5;
                const QColor &color = selected ? kKeyframeColorSelected : kKeyframeColorDefault;
                if (physicsEnabled) {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(color, qMax<qreal>(1.0, radius * 0.25)));
                    painter.drawLine(QPointF(cx - radius, cy - radius), QPointF(cx + radius, cy + radius));
                    painter.drawLine(QPointF(cx + radius, cy - radius), QPointF(cx - radius, cy + radius));
                }
                else {
                    QPolygonF diamond;
                    diamond << QPointF(cx, cy - radius) << QPointF(cx + radius, cy) << QPointF(cx, cy + radius)
                            << QPointF(cx - radius, cy);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(color);
                    painter.drawPolygon(diamond);
                }
            }
        }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kRowLineColor, 1.0));
    painter.drawLine(tw + 1, 0, tw + 1, height());
}

void
QtTimelineGrid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    rebuildRows();
    Project *project = currentProject();
    if (!project || m_rows.isEmpty()) {
        return;
    }
    const QPoint pos(event->position().toPoint());
    const bool withModifier = (event->modifiers() & Qt::ControlModifier) != 0;
    const int row = rowAt(pos);
    if (pos.x() <= trackWidth()) {
        if (row >= 0) {
            ITrack *track = m_rows[row];
            if (track->isExpandable() && pos.x() < kTimelineExpandIconWidth) {
                handleTrackExpansion(track);
            }
            else {
                handleTrackPressed(project, track, withModifier);
            }
        }
        return;
    }
    const nanoem_frame_index_t baseFrameIndex = firstVisibleFrameIndex();
    const nanoem_frame_index_t frameIndex = frameAtWithBase(pos, baseFrameIndex);
    if (row >= 0) {
        bool selected = false, physicsEnabled = false;
        if (queryKeyframeState(project, m_rows[row], frameIndex, selected, physicsEnabled)) {
            m_pressMode = kPressModeSelectingKeyframe;
            selectKeyframe(project, m_rows[row], frameIndex, withModifier);
            return;
        }
    }
    /* clicking empty marker space seeks to the frame and drops the keyframe selection unless the
       control key is held (the ImGui markers panel cleared on release); dragging scrubs */
    m_pressMode = kPressModeSeeking;
    m_pressBaseFrameIndex = baseFrameIndex;
    if (!withModifier) {
        clearAllKeyframeSelection(project);
    }
    seekTo(project, frameIndex);
    update();
}

void
QtTimelineGrid::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressMode == kPressModeSeeking && (event->buttons() & Qt::LeftButton)) {
        if (Project *project = currentProject()) {
            /* keep the column mapping anchored at the press so scrubbing does not chase the
               auto-centered ruler */
            seekTo(project, frameAtWithBase(event->position().toPoint(), m_pressBaseFrameIndex));
        }
    }
}

void
QtTimelineGrid::mouseReleaseEvent(QMouseEvent *release)
{
    Q_UNUSED(release);
    m_pressMode = kPressModeNone;
}

void
QtTimelineGrid::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0 && !m_rows.isEmpty()) {
        m_scrollY -= (delta / 120) * rowHeight() * kTimelineScrollStep;
        clampScrollOffset();
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

} /* namespace qt */
} /* namespace nanoem */
