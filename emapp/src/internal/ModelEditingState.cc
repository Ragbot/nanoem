/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "emapp/internal/ModelEditingState.h"

#include "emapp/Error.h"
#include "emapp/Grid.h"
#include "emapp/ICamera.h"
#include "emapp/IEventPublisher.h"
#include "emapp/ILight.h"
#include "emapp/IModelObjectSelection.h"
#include "emapp/Motion.h"
#include "emapp/PhysicsEngine.h"
#include "emapp/StringUtils.h"
#include "emapp/model/IGizmo.h"
#include "emapp/private/CommonInclude.h"

namespace nanoem {
namespace internal {
namespace {

void
saveLastModel(const Model *activeModel, const ModelEditingState::ModelState &state, Error &error)
{
    String filePath(activeModel->fileURI().absolutePathByDeletingPathExtension());
    filePath.append("-");
    filePath.append(state.m_datetime);
    filePath.append(
        nanoemModelGetFormatType(activeModel->data()) == NANOEM_MODEL_FORMAT_TYPE_PMD_1_0 ? ".pmd" : ".pmx");
#if !defined(NANOEM_ENABLE_DEBUG_LABEL)
    FileWriterScope scope;
    if (scope.open(URI::createFromFilePath(filePath), error)) {
        FileUtils::write(scope.writer(), state.m_modelData, error);
        if (!error.hasReason()) {
            scope.commit(error);
        }
        else {
            scope.rollback(error);
        }
    }
#else
    BX_UNUSED_1(error);
    EMLOG_INFO("saveLastModel(path=%s)", filePath.c_str());
#endif /* NANOEM_ENABLE_DEBUG_LABEL */
}

void
saveCurrentModel(const Model *activeModel, Error &error)
{
    if (!activeModel) {
        return;
    }
#if !defined(NANOEM_ENABLE_DEBUG_LABEL)
    FileWriterScope scope;
    if (scope.open(activeModel->fileURI(), error)) {
        if (activeModel->save(scope.writer(), error)) {
            scope.commit(error);
        }
        else {
            scope.rollback(error);
        }
    }
#else
    BX_UNUSED_1(error);
    EMLOG_INFO("saveCurrentModel(path={})", activeModel->fileURI().absolutePathConstString());
#endif /* NANOEM_ENABLE_DEBUG_LABEL */
}

} /* namespace anonymous */

ModelEditingState::ModelEditingState(Project::EditingMode editingMode)
    : m_activeModel(nullptr)
    , m_state(nullptr)
    , m_lastEditingMode(editingMode)
{
}

ModelEditingState::~ModelEditingState() NANOEM_DECL_NOEXCEPT
{
}

void
captureModelState(ModelEditingState *state, Model *activeModel, Project *project, Error &error)
{
    const Project::DrawableList *drawables = project->drawableOrderList();
    for (Project::DrawableList::const_iterator it = drawables->begin(), end = drawables->end(); it != end; ++it) {
        IDrawable *drawable = *it;
        drawable->setVisible(false);
    }
    if (Motion *motion = project->resolveMotion(activeModel)) {
        ModelEditingState::ModelStateMap::const_iterator it = state->m_modelStates.find(activeModel);
        if (it == state->m_modelStates.end()) {
            ModelEditingState::ModelState modelState;
            const URI modelFileURI(activeModel->fileURI());
            FileReaderScope scope(project->translator());
            if (scope.open(modelFileURI, error)) {
                FileUtils::read(scope, modelState.m_modelData, error);
                if (error.hasReason()) {
                    error = Error();
                    activeModel->save(modelState.m_modelData, error);
                }
            }
            if (!error.hasReason() &&
                motion->save(modelState.m_motionData, activeModel, NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL, error)) {
                motion->clearAllKeyframes();
                if (const model::Bone *activeBone = model::Bone::cast(activeModel->activeBone())) {
                    modelState.m_activeBoneName = activeBone->name();
                }
                if (const model::Morph *activeMorph = model::Morph::cast(activeModel->activeMorph())) {
                    modelState.m_activeMorphName = activeMorph->name();
                }
                StringUtils::formatDateTimeLocal(modelState.m_datetime, sizeof(modelState.m_datetime), "%Y%m%d_%H%M%S");
                state->m_modelStates.insert(tinystl::make_pair(activeModel, modelState));
            }
        }
    }
    activeModel->selection()->clearAll();
    activeModel->setActiveBone(nullptr);
    activeModel->setActiveMorph(nullptr);
    activeModel->setVisible(true);
    activeModel->resetAllBoneTransforms();
    activeModel->resetAllMaterials();
    activeModel->resetAllMorphs();
    activeModel->resetAllVertices();
    activeModel->performAllBonesTransform();
    activeModel->updateStagingVertexBuffer();
    project->activeCamera()->reset();
    project->activeLight()->reset();
}

void
enableModelEditing(ModelEditingState *state, Model *activeModel, Project *project, Error &error)
{
    project->setModelEditingEnabled(true);
    state->m_activeModel = activeModel;
    project->saveState(state->m_state);
    project->activeCamera()->reset();
    project->activeLight()->reset();
    project->grid()->setVisible(true);
    project->setPhysicsSimulationMode(PhysicsEngine::kSimulationModeDisable);
    captureModelState(state, activeModel, project, error);
}

void
disableModelEditing(ModelEditingState **statePtr, Project *project, Error &error)
{
    ModelEditingState *state = *statePtr;
    if (!state) {
        return;
    }
    const Project::DrawableList *drawables = project->drawableOrderList();
    for (ModelEditingState::ModelStateMap::const_iterator it = state->m_modelStates.begin(),
                                                          end = state->m_modelStates.end();
         it != end; ++it) {
        const ModelEditingState::ModelState &modelState = it->second;
        Model *model = it->first;
        if (const nanoem_model_bone_t *activeBonePtr = model->findBone(modelState.m_activeBoneName)) {
            model->setActiveBone(activeBonePtr);
        }
        if (const nanoem_model_morph_t *activeMorphPtr = model->findMorph(modelState.m_activeMorphName)) {
            model->setActiveMorph(activeMorphPtr);
        }
        saveLastModel(model, modelState, error);
        if (Motion *motion = project->resolveMotion(model)) {
            motion->load(modelState.m_motionData, 0, error);
        }
    }
    Model *activeModel = state->m_activeModel;
    for (Project::DrawableList::const_iterator it = drawables->begin(), end = drawables->end(); it != end; ++it) {
        IDrawable *drawable = *it;
        if (drawable != activeModel) {
            drawable->setVisible(true);
        }
    }
    if (activeModel) {
        activeModel->restoreBindPose(state->m_bindPose);
    }
    project->restoreState(state->m_state, true);
    project->destroyState(state->m_state);
    project->rebuildAllTracks();
    project->setEditingMode(state->m_lastEditingMode);
    project->setModelEditingEnabled(false);
    saveCurrentModel(activeModel, error);
    nanoem_delete_safe(*statePtr);
}

void
destroyModelEditingState(ModelEditingState *state, Model *activeModel, Project *project, Error &error)
{
    if (state) {
        for (ModelEditingState::ModelStateMap::const_iterator it = state->m_modelStates.begin(),
                                                              end = state->m_modelStates.end();
             it != end; ++it) {
            saveLastModel(it->first, it->second, error);
        }
        saveCurrentModel(activeModel, error);
        project->destroyState(state->m_state);
    }
}

void
beforeToggleModelEditingMode(IModelObjectSelection::ObjectType editingType, Model *activeModel, Project *project)
{
    activeModel->setShowAllVertexFaces(false);
    activeModel->setShowAllVertexPoints(false);
    activeModel->setShowAllVertexWeights(false);
    activeModel->setShowAllMaterialOverlays(false);
    activeModel->setShowAllBones(false);
    activeModel->setShowAllRigidBodyShapes(false);
    activeModel->setShowAllJointShapes(false);
    switch (editingType) {
    case IModelObjectSelection::kObjectTypeBone: {
        project->setEditingMode(Project::kEditingModeSelect);
        break;
    }
    case IModelObjectSelection::kObjectTypeMorph: {
        if (Motion *motion = project->resolveMotion(activeModel)) {
            activeModel->synchronizeMotion(
                motion, project->currentLocalFrameIndex(), 0, PhysicsEngine::kSimulationTimingBefore);
            activeModel->resetAllVertices();
            activeModel->deformAllMorphs(false);
            activeModel->markStagingVertexBufferDirty();
        }
        break;
    }
    default:
        break;
    }
}

void
afterToggleModelEditingMode(IModelObjectSelection::ObjectType editingType, Model *activeModel, Project *project)
{
    switch (editingType) {
    case IModelObjectSelection::kObjectTypeNull: {
        if (model::IGizmo *gizmo = activeModel->gizmo()) {
            gizmo->setPivotMatrix(Matrix4x4(0));
        }
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeVertex: {
        activeModel->setShowAllVertexPoints(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeFace: {
        activeModel->setShowAllVertexFaces(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeMaterial: {
        activeModel->setShowAllMaterialOverlays(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeBone: {
        activeModel->selection()->setObjectType(editingType);
        activeModel->setShowAllBones(true);
        project->setEditingMode(Project::kEditingModeSelect);
        break;
    }
    case IModelObjectSelection::kObjectTypeRigidBody: {
        activeModel->setShowAllRigidBodyShapes(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeJoint: {
        activeModel->setShowAllJointShapes(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    case IModelObjectSelection::kObjectTypeSoftBody: {
        activeModel->setShowAllVertexFaces(true);
        activeModel->selection()->setObjectType(editingType);
        project->setEditingMode(Project::kEditingModeNone);
        break;
    }
    default:
        break;
    }
}

} /* namespace internal */
} /* namespace nanoem */
