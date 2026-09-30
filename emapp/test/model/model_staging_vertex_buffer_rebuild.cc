/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "../common.h"

#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/model/Morph.h"

#include <cstring>

using namespace nanoem;
using namespace test;

namespace {

inline bool
sameBytes(const ByteArray &lhs, const ByteArray &rhs)
{
    return lhs.size() == rhs.size() && (lhs.empty() || memcmp(lhs.data(), rhs.data(), lhs.size()) == 0);
}

inline bool
differentBytes(const ByteArray &lhs, const ByteArray &rhs)
{
    return !sameBytes(lhs, rhs);
}

} /* namespace */

TEST_CASE("model_staging_vertex_buffer_preserves_morph_state", "[emapp][model]")
{
    TestScope scope;
    {
        ProjectPtr o = scope.createProject();
        Project *project = o->withRecoverable();
        Model *model = o->createModel("test_vertex_morph.pmx");
        project->addModel(model);
        project->setActiveModel(model);
        nanoem_rsize_t numMorphs;
        nanoem_model_morph_t *const *morphs = nanoemModelGetAllMorphObjects(model->data(), &numMorphs);
        REQUIRE(numMorphs > 0);
        model::Morph *morph = model::Morph::cast(morphs[0]);
        REQUIRE(morph != nullptr);
        /* upload with the morph weight unset to capture the bind pose */
        model->markStagingVertexBufferDirty();
        model->updateStagingVertexBuffer();
        const ByteArray baseline = model->vertexBufferData();
        REQUIRE(!baseline.empty());
        /* apply the morph and upload once */
        morph->setWeight(0.5f);
        model->deformAllMorphs(true);
        model->markStagingVertexBufferDirty();
        model->updateStagingVertexBuffer();
        const ByteArray applied = model->vertexBufferData();
        REQUIRE(differentBytes(applied, baseline));
        /*
         * Upload again without any deform in between, which is what any subsequent action
         * (transforming bones or the model, seeking, stepping physics, ...) triggers: the
         * previous upload consumed the accumulated vertex morph delta, so it must be rebuilt
         * from the still intact weight before baking. Otherwise the model silently reverts to
         * the bind pose until the next explicit deform.
         */
        model->markStagingVertexBufferDirty();
        model->updateStagingVertexBuffer();
        const ByteArray rebuilt = model->vertexBufferData();
        REQUIRE(sameBytes(rebuilt, applied));
        CHECK(morph->weight() == 0.5f);
    }
}
