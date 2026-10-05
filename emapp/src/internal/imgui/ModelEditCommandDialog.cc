/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "emapp/internal/imgui/ModelEditCommandDialog.h"

#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/internal/ModelEditingState.h"

namespace nanoem {
namespace internal {
namespace imgui {

const char *const ModelEditCommandDialog::kIdentifier = "dialog.model.edit";

void
ModelEditCommandDialog::beforeToggleEditingMode(
    IModelObjectSelection::ObjectType editingType, Model *activeModel, Project *project)
{
    internal::beforeToggleModelEditingMode(editingType, activeModel, project);
}

void
ModelEditCommandDialog::afterToggleEditingMode(
    IModelObjectSelection::ObjectType editingType, Model *activeModel, Project *project)
{
    internal::afterToggleModelEditingMode(editingType, activeModel, project);
}

ModelEditCommandDialog::ModelEditCommandDialog(Model *model, BaseApplicationService *applicationPtr)
    : BaseNonModalDialogWindow(applicationPtr)
    , m_activeModel(model)
{
}

} /* namespace imgui */
} /* namespace internal */
} /* namespace nanoem */
