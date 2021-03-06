// MIT License
//
// Copyright (c) 2019 椎名深雪
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <akari/core/plugin.h>
#include <akari/plugins/matte.h>
#include <akari/render/geometry.hpp>
#include <akari/render/material.h>
#include <akari/render/reflection.h>
#include <akari/render/texture.h>
#include <utility>

namespace akari {
    class MatteMaterial final : public Material {
        [[refl]] std::shared_ptr<Texture> color;

      public:
        MatteMaterial() = default;
        explicit MatteMaterial(std::shared_ptr<Texture> color) : color(std::move(color)) {}
        AKR_IMPLS(Material)
        void compute_scattering_functions(SurfaceInteraction *si, MemoryArena &arena, TransportMode mode,
                                          Float scale) const override {
            auto c = color->evaluate(si->sp);
            si->bsdf->add_component(arena.alloc<LambertianReflection>(c * scale));
        }
        bool support_bidirectional() const override { return true; }
    };
#include "generated/MatteMaterial.hpp"
    AKR_EXPORT_PLUGIN(p) {
        auto c = class_<MatteMaterial>();
        c.method("support_bidirectional", &MatteMaterial::support_bidirectional);
    }
    std::shared_ptr<Material> create_matte_material(const std::shared_ptr<Texture> &color) {
        return std::make_shared<MatteMaterial>(color);
    }

} // namespace akari