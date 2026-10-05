/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#ifndef NANOEM_EMAPP_INTERNAL_MODELEDITINGSTATE_H_
#define NANOEM_EMAPP_INTERNAL_MODELEDITINGSTATE_H_

#include "emapp/Forward.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/model/BindPose.h"

namespace nanoem {
namespace internal {

/**
 * @brief The model editing session the model parameter dialog owns.
 *
 *        Opening the dialog turns model editing on and snapshots the project plus every model it
 *        shows (its file data, its motion keyframes and the active bone/morph); closing it puts
 *        all of that back and turns model editing off again. The state is what connects the two
 *        halves, so it lives outside the ImGui dialog and both shells run the same lifecycle.
 */
struct ModelEditingState {
    struct ModelState {
        String m_activeBoneName;
        String m_activeMorphName;
        ByteArray m_modelData;
        ByteArray m_motionData;
        char m_datetime[32];
    };
    typedef tinystl::unordered_map<Model *, ModelState, TinySTLAllocator> ModelStateMap;

    explicit ModelEditingState(Project::EditingMode editingMode);
    ~ModelEditingState() NANOEM_DECL_NOEXCEPT;

    Model *m_activeModel;
    Project::SaveState *m_state;
    Project::EditingMode m_lastEditingMode;
    ModelStateMap m_modelStates;
    model::BindPose m_bindPose;
};

/**
 * @brief Hides every drawable, snapshots `activeModel` into `state` (unless it already did) and
 *        resets the model, camera and light for editing. Called when the dialog opens and each
 *        time the active model changes while it is open.
 */
void captureModelState(ModelEditingState *state, Model *activeModel, Project *project, Error &error);
/**
 * @brief Turns model editing on: saves the project state and captures `activeModel`.
 */
void enableModelEditing(ModelEditingState *state, Model *activeModel, Project *project, Error &error);
/**
 * @brief Restores everything `enableModelEditing` saved (project state, motion keyframes, active
 *        bone/morph, drawable visibility, bind pose, editing mode), writes the model files back
 *        and turns model editing off. Takes ownership of `*state` and nulls it.
 */
void disableModelEditing(ModelEditingState **state, Project *project, Error &error);
/**
 * @brief Writes the captured model files back and drops the project state without restoring the
 *        rest: what happens when the dialog goes away without running the disable path.
 */
void destroyModelEditingState(ModelEditingState *state, Model *activeModel, Project *project, Error &error);

} /* namespace internal */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_INTERNAL_MODELEDITINGSTATE_H_ */
