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

#include <akari/core/image.hpp>
#include <memory>

#include <akari/core/parallel.h>
#include <mutex>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace akari {
    class DefaultImageWriter : public ImageWriter {
      public:
        bool write(const RGBAImage &_image, const fs::path &path, const PostProcessor &postProcessor) override {
            const auto ext = path.extension().string();
            RGBAImage image;
            postProcessor.process(_image, image);
            auto &texels = image.texels();
            auto dimension = image.resolution();
            std::vector<uint8_t> buffer(texels.size() * 3);
            parallel_for(
                    texels.size(),
                    [&](uint32_t i, uint32_t) {
                        auto pixel = static_cast<uint8_t *>(&buffer[i * 3]);
                        auto rgb = vec3(texels[i]);
                        rgb = clamp(rgb, vec3(0), vec3(1));
                        for (int comp = 0; comp < 3; comp++) {
                            pixel[comp] = (uint8_t) std::clamp<int>((int) std::round(rgb[comp] * 255.5), 0, 255);
                        }
                    },
                    1024u);
            if (ext == ".png")
                return stbi_write_png(path.string().c_str(), dimension.x, dimension.y, 3, buffer.data(), 0);
            else if (ext == ".jpg")
                return stbi_write_jpg(path.string().c_str(), dimension.x, dimension.y, 3, buffer.data(), 0);
            return false;
        }
    };

    void GammaCorrection::process(const RGBAImage &in, RGBAImage &out) const {
        out.Resize(in.resolution());
        parallel_for(
                in.resolution().y,
                [&](uint32_t y, uint32_t) {
                    for (int i = 0; i < in.resolution().x; i++)
                        out(i, y) = vec4(pow(vec3(in(i, y)), vec3(gamma)), in(i, y).w);
                },
                1024);
    }

    std::shared_ptr<ImageWriter> default_image_writer() { return std::make_shared<DefaultImageWriter>(); }

    class DefaultImageReader : public ImageReader {
      public:
        std::shared_ptr<RGBAImage> read(const fs::path &path) override {
            std::shared_ptr<RGBAImage> image;
            int x, y, channel;
            const auto *data = stbi_load(path.string().c_str(), &x, &y, &channel, 3);
            image = std::make_shared<RGBAImage>(ivec2(x, y));
            parallel_for(
                    image->resolution().y,
                    [=, &image](uint32_t y, uint32_t) {
                        for (int x = 0; x < image->resolution().x; x++) {
                            vec3 rgb;
                            if (channel == 1) {
                                rgb = vec3((float) data[x + y * image->resolution().x] / 255.0f);
                            } else {
                                rgb[0] = (float) data[3 * (x + y * image->resolution().x) + 0] / 255.0f;
                                rgb[1] = (float) data[3 * (x + y * image->resolution().x) + 1] / 255.0f;
                                rgb[2] = (float) data[3 * (x + y * image->resolution().x) + 2] / 255.0f;
                            }
                            (*image)(x, y) = vec4(rgb, 1.0f);
                        }
                    },
                    1024);
            return image;
        }
    };

    std::shared_ptr<ImageReader> default_image_reader() { return std::make_shared<DefaultImageReader>(); }
} // namespace akari
