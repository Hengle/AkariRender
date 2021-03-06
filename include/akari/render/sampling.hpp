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

#ifndef AKARIRENDER_SAMPLING_HPP
#define AKARIRENDER_SAMPLING_HPP
#include <akari/core/math.h>
#include <algorithm>

namespace akari {
    template <typename Vector2f, typename Float = scalar_t<Vector2f>, typename Vector3f = tvec2<Float>>
    static inline Vector2f concentric_disk_sampling(const Vector2f &u) {
        AKR_USE_MATH_CONSTANTS()
        Vector2f uOffset = 2.f * u - Vector2f(1, 1);
        if (uOffset.x == 0 && uOffset.y == 0)
            return Vector2f(0, 0);

        Float theta, r;
        if (std::abs(uOffset.x) > std::abs(uOffset.y)) {
            r = uOffset.x;
            theta = Pi4 * (uOffset.y / uOffset.x);
        } else {
            r = uOffset.y;
            theta = Pi2 - Pi4 * (uOffset.x / uOffset.y);
        }
        return r * Vector2f(std::cos(theta), std::sin(theta));
    }
    template <typename Vector2f, typename Float = scalar_t<Vector2f>, typename Vector3f = tvec2<Float>>
    static inline Vector3f cosine_hemisphere_sampling(const Vector2f &u) {
        auto uv = concentric_disk_sampling(u);
        auto r = dot(uv, uv);
        auto h = std::sqrt(std::max(0.0f, 1 - r));
        return Vector3f(uv.x, h, uv.y);
    }

    template <typename Float, typename Vector2f = tvec2<Float>, typename Vector3f = tvec2<Float>>
    static inline Float cosine_hemisphere_pdf(Float cosTheta) {
        AKR_USE_MATH_CONSTANTS()
        return cosTheta * InvPi;
    }

    template <typename Float> static inline Float uniform_sphere_pdf() {
        AKR_USE_MATH_CONSTANTS()
        return 1.0f / (4 * Pi); }

    template <typename Float, typename Vector2f = tvec2<Float>, typename Vector3f = tvec2<Float>>
    static inline Vector3f uniform_sphere_sampling(const Vector2f &u) {
        AKR_USE_MATH_CONSTANTS()
        Float z = 1 - 2 * u[0];
        Float r = std::sqrt(std::max((Float)0, (Float)1 - z * z));
        Float phi = 2 * Pi * u[1];
        return Vector3f(r * std::cos(phi), r * std::sin(phi), z);
    }
} // namespace akari
#endif // AKARIRENDER_SAMPLING_HPP
