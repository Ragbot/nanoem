/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_INTERNAL_BLITPASS_H_
#define NANOEM_EMAPP_INTERNAL_BLITPASS_H_

#include "emapp/internal/BasePass.h"

namespace nanoem {

class Project;

namespace internal {

class BlitPass NANOEM_DECL_SEALED : public BasePass {
public:
    BlitPass(Project *project, bool flipY);
    ~BlitPass() NANOEM_DECL_NOEXCEPT;

    void blit(sg::PassBlock::IDrawQueue *drawQueue, const sg::NamedPass &dest, const sg::NamedImage &source,
        const Vector4 &rect, const PixelFormat &format);
    void blit(sg::PassBlock::IDrawQueue *drawQueue, const sg::NamedPass &dest, const sg::NamedImage &source,
        const Vector4 &rect, const PixelFormat &format, const Vector4 &viewport);
    /**
     * @brief Draws `source` onto `rect` (device pixels, top-left origin) of the currently active
     *       pass (e.g. the window/default pass).
     *
     *       Unlike blit() this does not begin/end a render pass: the caller must have begun the
     *       destination pass already. Used by shells that composite the viewport image onto the
     *       window framebuffer without going through ImGui.
     */
    void present(
        const sg::NamedImage &source, const Vector4 &rect, const Vector2UI16 &passSize, const PixelFormat &format);
    void markAsDirty();

private:
    void draw(sg::PassBlock::IDrawQueue *drawQueue, sg_pipeline pipeline, sg_pass dest, sg_image source);
    void setupVertexBuffer(const Vector4 &rect);
    void setupShaderDescription(sg_shader_desc &desc) NANOEM_DECL_OVERRIDE;
    void setupPipelineDescription(sg_pipeline_desc &desc) NANOEM_DECL_OVERRIDE;
    const char *name() const NANOEM_DECL_NOEXCEPT_OVERRIDE;

    sg::QuadVertexUnit m_vertices[4];
    bool m_dirty;
};

} /* namespace internal */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_INTERNAL_BLITPASS_H_ */
