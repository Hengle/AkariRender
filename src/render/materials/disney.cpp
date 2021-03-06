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
#include <akari/render/material.h>
#include <akari/render/reflection.h>
#include <akari/render/microfacet.h>
#include <akari/plugins/float_texture.h>
#include <akari/core/logger.h>

namespace akari {
    inline Float sqr(Float x){return x * x;}
    inline Float SchlickWeight(Float cosTheta) { return Power<5>(std::clamp<float>(1 - cosTheta, 0, 1)); }
    inline Float FrSchlick(Float R0, Float cosTheta) { return lerp(SchlickWeight(cosTheta), R0, 1.0f); }
    inline Spectrum FrSchlick(const Spectrum &R0, Float cosTheta) {
        return lerp(Spectrum(SchlickWeight(cosTheta)), R0, Spectrum(1));
    }
    class DisneyDiffuse : public BSDFComponent {
        Spectrum R;

      public:
        DisneyDiffuse(const Spectrum &R) : BSDFComponent(BSDFType(BSDF_DIFFUSE | BSDF_REFLECTION)), R(R) {}
        [[nodiscard]] Spectrum evaluate(const vec3 &wo, const vec3 &wi) const override {
            Float Fo = SchlickWeight(abs_cos_theta(wo));
            Float Fi = SchlickWeight(abs_cos_theta(wi));
            return R * InvPi * (1 - Fo * 0.5) * (1 - Fi * 0.5);
        }
    };

    inline Spectrum CalculateTint(const Spectrum &baseColor) {
        auto luminance = baseColor.luminance();
        return luminance > 0 ? baseColor / luminance : Spectrum(1);
    }

    class DisneySheen : public BSDFComponent {
        const Spectrum base_color;
        const Float sheen;
        const Spectrum sheenTint;

      public:
        DisneySheen(const Spectrum &base_color, Float sheen, const Spectrum &sheenTint)
            : BSDFComponent(BSDFType(BSDF_GLOSSY | BSDF_REFLECTION)), base_color(base_color), sheen(sheen),
              sheenTint(sheenTint) {}
        [[nodiscard]] Spectrum evaluate(const vec3 &wo, const vec3 &wi) const override {
            auto wh = wi + wo;
            if (all(equal(wh, vec3(0)))) {
                return Spectrum(0);
            }
            wh = normalize(wh);
            Float d = dot(wh, wi);
            return sheen * lerp(Spectrum(1), CalculateTint(base_color), sheenTint) * SchlickWeight(d);
        }
    };

    static inline Float D_GTR1(Float cosThetaH, Float alpha) {
        if (alpha >= 1)
            return InvPi;
        auto a2 = alpha * alpha;
        return (a2 - 1) / (Pi * log(a2)) * (1.0f / (1 + (a2 - 1) * cosThetaH * cosThetaH));
    }
    static inline Float SmithGGX_G1(Float cosTheta, Float alpha) {
        auto a2 = alpha * alpha;
        return 1.0 / (cosTheta + std::sqrt(a2 + cosTheta - a2 * cosTheta * cosTheta));
    }
    class DisneyClearCoat : public BSDFComponent {
        Float clearcoat;
        Float alpha;

      public:
        DisneyClearCoat(Float clearcoat, Float alpha)
            : BSDFComponent(BSDFType(BSDF_GLOSSY | BSDF_REFLECTION)), clearcoat(clearcoat), alpha(alpha) {}
        [[nodiscard]] Spectrum evaluate(const vec3 &wo, const vec3 &wi) const override {
            auto wh = wi + wo;
            if (all(equal(wh, vec3(0)))) {
                return Spectrum(0);
            }
            wh = normalize(wh);
            Float D = D_GTR1(abs_cos_theta(wh), alpha);
            Float F = FrSchlick(0.04, dot(wo, wh));
            Float G = SmithGGX_G1(abs_cos_theta(wi), 0.25) * SmithGGX_G1(abs_cos_theta(wo), 0.25);
            return Spectrum(0.25 * clearcoat * D * F * G);
        }
        [[nodiscard]] Float evaluate_pdf(const vec3 &wo, const vec3 &wi) const override {
            if (!same_hemisphere(wo, wi))
                return 0;
            auto wh = wi + wo;
            if (wh.x == 0 && wh.y == 0 && wh.z == 0)
                return 0;
            wh = normalize(wh);
            return D_GTR1(abs_cos_theta(wh), alpha) * abs_cos_theta(wh) / (4.0 * dot(wh, wo));
        }
        Spectrum sample(const vec2 &u, const vec3 &wo, vec3 *wi, Float *pdf, BSDFType *sampledType) const override {
            auto a2 = alpha * alpha;
            auto cosTheta = std::sqrt(std::fmax(0.0f, 1 - (std::pow(a2, 1 - u[0])) / (1 - a2)));
            auto sinTheta = std::sqrt(std::fmax(0.0f, 1 - cosTheta * cosTheta));
            auto phi = 2.0f * Pi * u[1];
            auto wh = vec3(std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta);
            if (!same_hemisphere(wo, wh))
                wh = -wh;
            *wi = reflect(wo, wh);
            *pdf = evaluate_pdf(wo, *wi);
            return evaluate(wo, *wi);
        }
    };
    inline Float SchlickR0FromEta(Float eta) { return sqr(eta - 1.0f) / sqr(eta + 1.0f); }
    class DisneyFresnel : public Fresnel {
        const Spectrum R0;
        const Float metallic, eta;

      public:
        DisneyFresnel(const Spectrum &R0, Float metallic, Float eta) : R0(R0), metallic(metallic), eta(eta) {}
        [[nodiscard]] Spectrum evaluate(Float cosThetaI) const override {
            return lerp(Spectrum(fr_dielectric(cosThetaI, 1.0f, eta)), FrSchlick(R0, cosThetaI), Spectrum(metallic));
        }
    };

    class DisneyMaterial : public Material {
        [[refl]] std::shared_ptr<Texture> basecolor;
        [[refl]] std::shared_ptr<Texture> subsurface;
        [[refl]] std::shared_ptr<Texture> metallic;
        [[refl]] std::shared_ptr<Texture> specular;
        [[refl]] std::shared_ptr<Texture> specular_tint;
        [[refl]] std::shared_ptr<Texture> roughness;
        [[refl]] std::shared_ptr<Texture> anisotropic;
        [[refl]] std::shared_ptr<Texture> sheen;
        [[refl]] std::shared_ptr<Texture> sheen_tint;
        [[refl]] std::shared_ptr<Texture> clearcoat;
        [[refl]] std::shared_ptr<Texture> clearcoat_gloss;
        [[refl]] std::shared_ptr<Texture> ior;
        [[refl]] std::shared_ptr<Texture> spec_trans;

      public:
        DisneyMaterial() = default;
        DisneyMaterial(std::shared_ptr<Texture> base_color, std::shared_ptr<Texture> subsurface,
                       std::shared_ptr<Texture> metallic, std::shared_ptr<Texture> specular,
                       std::shared_ptr<Texture> specular_tint, std::shared_ptr<Texture> roughness,
                       std::shared_ptr<Texture> anisotropic, std::shared_ptr<Texture> sheen,
                       std::shared_ptr<Texture> sheen_tint, std::shared_ptr<Texture> clearcoat,
                       std::shared_ptr<Texture> clearcoat_gloss, std::shared_ptr<Texture> ior,
                       std::shared_ptr<Texture> spec_trans)
            : basecolor(std::move(base_color)), subsurface(std::move(subsurface)), metallic(std::move(metallic)),
              specular(std::move(specular)), specular_tint(std::move(specular_tint)), roughness(std::move(roughness)),
              anisotropic(std::move(anisotropic)), sheen(std::move(sheen)), sheen_tint(std::move(sheen_tint)),
              clearcoat(std::move(clearcoat)), clearcoat_gloss(std::move(clearcoat_gloss)), ior(std::move(ior)),
              spec_trans(std::move(spec_trans)) {}
        AKR_IMPLS(Material)
        [[refl]] void compute_scattering_functions(SurfaceInteraction *si, MemoryArena &arena, TransportMode mode,
                                                   Float scale) const override {
            Spectrum color = basecolor->evaluate(si->sp) * scale;
            Float metallicWeight = metallic->evaluate(si->sp)[0];
            Float eta = ior->evaluate(si->sp)[0];
            Float trans = spec_trans->evaluate(si->sp)[0];
            Float diffuseWeight = (1.0f - trans) * (1.0f - metallicWeight);
            Float transWeight = trans * (1.0f - metallicWeight);
            Float alpha = roughness->evaluate(si->sp)[0];
            Float specTint = specular_tint->evaluate(si->sp)[0];
            alpha *= alpha;
            if (diffuseWeight > 0) {
                si->bsdf->add_component(arena.alloc<DisneyDiffuse>(color * diffuseWeight));
                // TODO: subsurface

                Float sheenWeight = sheen->evaluate(si->sp)[0];
                if (sheenWeight > 0) {
                    Spectrum st = sheen_tint->evaluate(si->sp);
                    si->bsdf->add_component(arena.alloc<DisneySheen>(color * diffuseWeight, sheenWeight, st));
                }
            }
            auto tint = CalculateTint(color);
            Spectrum color_spec = SchlickR0FromEta(eta) * lerp(Spectrum(1), tint, Spectrum(specTint));
            color_spec = lerp(color_spec, color, Spectrum(metallicWeight));
            si->bsdf->add_component(
                arena.alloc<MicrofacetReflection>(Spectrum(1.0f), MicrofacetModel(EGGX, alpha),
                                                  arena.alloc<DisneyFresnel>(color_spec, metallicWeight, eta)));

            if (transWeight > 0) {
                Spectrum c = transWeight * sqrt(clamp(color, 0.001f, 1.0f));
                si->bsdf->add_component(arena.alloc<MicrofacetTransmission>(
                    c, MicrofacetModel(EGGX, alpha), 1.0f, eta, mode));
                // si->bsdf->add_component(arena.alloc<SpecularTransmission>(c, 1.0f, eta, mode));
            }
            Float cc = clearcoat->evaluate(si->sp)[0];
            if (cc > 0) {
                Float ccg = clearcoat_gloss->evaluate(si->sp)[0];
                si->bsdf->add_component(arena.alloc<DisneyClearCoat>(cc, ccg));
            }
        }
        [[refl]] bool support_bidirectional() const override { return true; }
        [[refl]] void commit() override;
    };

#include "generated/DisneyMaterial.hpp"

    void DisneyMaterial::commit() {
        Component::commit();

        StaticMeta<DisneyMaterial>::foreach_property(*this, [](const StaticProperty &meta, auto &&prop) {
            using T = std::decay_t<decltype(prop)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) {
                if (prop == nullptr) {
                    if (meta.name == "ior") {
                        prop = create_float_texture(1.45);
                    } else {
                        prop = std::make_shared<NullTexture>();
                    }
                }
            }
        });
    }

    AKR_EXPORT_PLUGIN(p) {}

} // namespace akari
