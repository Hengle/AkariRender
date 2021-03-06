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

#include <akari/core/logger.h>
#include <akari/core/parallel.h>
#include <akari/core/plugin.h>
#include <akari/plugins/core_bidir.h>
#include <akari/plugins/mltsampler.h>
#include <akari/render/integrator.h>
#include <akari/render/scene.h>
#include <future>
#include <mutex>
#include <random>
#include <utility>

namespace akari {
    using namespace MLT;
    class MMLTRenderTask : public RenderTask {
        RenderContext ctx;
        std::mutex mutex;
        std::condition_variable filmAvailable, done;
        std::future<void> future;
        int spp;
        int maxDepth;
        size_t nBootstrap;
        size_t nChains;
        int directSamples = 1;
        float clamp;
        size_t maxConsecutiveRejects = 51200;

      public:
        MMLTRenderTask(RenderContext ctx, int spp, int maxDepth, size_t nBootstrap, size_t nChains, int nDirect,
                       float clamp, size_t maxConsecutiveRejects)
            : ctx(std::move(ctx)), spp(spp), maxDepth(maxDepth), nBootstrap(nBootstrap), nChains(nChains),
              directSamples(nDirect), clamp(clamp), maxConsecutiveRejects(maxConsecutiveRejects) {}
        bool has_film_update() override { return false; }
        std::shared_ptr<const Film> film_update() override { return ctx.camera->GetFilm(); }
        bool is_done() override { return false; }
        bool wait_event(Event event) override {
            if (event == Event::EFILM_AVAILABLE) {
                std::unique_lock<std::mutex> lock(mutex);

                filmAvailable.wait(lock, [=]() { return has_film_update() || is_done(); });
                return has_film_update();
            } else if (event == Event::ERENDER_DONE) {
                std::unique_lock<std::mutex> lock(mutex);

                done.wait(lock, [=]() { return is_done(); });
                return true;
            }
            return false;
        }
        void wait() override {
            future.wait();
            done.notify_all();
        }
        RadianceRecord Radiance(MLTSampler &sampler, MemoryArena &arena, bool bootstrap) {
            if (!bootstrap) {
                sampler.StartIteration();
            }
            sampler.StartStream(MLTSampler::ECamera);
            float pFilmX = sampler.next1d();
            float pFilmY = sampler.next1d();
            auto dim = ctx.camera->GetFilm()->resolution();
            ivec2 pRaster = round(vec2(pFilmX, pFilmY) * vec2(dim));
            pRaster.x = std::clamp(pRaster.x, 0, dim.x - 1);
            pRaster.y = std::clamp(pRaster.y, 0, dim.y - 1);
            RadianceRecord radianceRecord;
            radianceRecord.pRaster = pRaster;
            radianceRecord.radiance = Spectrum(0);
            auto &scene = *ctx.scene;
            auto &camera = *ctx.camera;
            int s, t, nStrategies;
            int depth = sampler.depth;
            if (directSamples == 0) {
                if (depth == 0) {
                    nStrategies = 1;
                    s = 0;
                    t = 2;
                } else {
                    nStrategies = depth + 2;
                    s = std::min((int)(sampler.next1d() * nStrategies), nStrategies - 1);
                    t = nStrategies - s;
                }
            } else {
                if (depth <= 1) {
                    return radianceRecord;
                } else {
                    nStrategies = depth + 2;
                    s = std::min((int)(sampler.next1d() * nStrategies), nStrategies - 1);
                    t = nStrategies - s;
                }
            }
            auto eyePath = arena.allocN<PathVertex>(t + 1);
            size_t nCamera = trace_eye_path(scene, arena, camera, pRaster, sampler, eyePath, t);
            if ((int)nCamera != t) {
                return radianceRecord;
            }
            sampler.StartStream(MLTSampler::ELight);
            auto lightPath = arena.allocN<PathVertex>(s + 1);
            size_t nLight = trace_light_path(scene, arena, sampler, lightPath, s);
            if ((int)nLight != s) {
                return radianceRecord;
            }
            sampler.StartStream(MLTSampler::EConnect);
            radianceRecord.radiance =
                glm::clamp(connect_path(scene, sampler, eyePath, t, lightPath, s, &radianceRecord.pRaster) * nStrategies, 0.0f,20.0f);
            return radianceRecord;
        }
        static Float ScalarContributionFunction(const Spectrum &L) { return std::max(0.0f, L.luminance()); }
        void start() override {
            future = std::async(std::launch::async, [=]() {
                DirectLighting();
                auto beginTime = std::chrono::high_resolution_clock::now();
                auto &scene = ctx.scene;
                auto &camera = ctx.camera;
                // auto &_sampler = ctx.sampler;
                auto film = camera->GetFilm();
                scene->ClearRayCounter();
                info("Start bootstrapping\n");
                std::random_device rd;
                std::uniform_int_distribution<uint64_t> dist;
                std::uniform_real_distribution<float> distF;
                // size_t nBootstrapSamples = nBootstrap * (maxDepth + 1);
                auto nCores = (int)GetConfig()->NumCore;
                std::vector<MemoryArena> arenas(nCores);
                std::vector<MarkovChain> markovChains(nChains);
                std::vector<float> depthWeight(maxDepth + 1);
                for (auto &chain : markovChains) {
                    chain.radianceRecords.resize(maxDepth + 1);
                    chain.samplers.resize(maxDepth + 1);
                }
                for (int depth = 0; depth < maxDepth + 1; depth++) {
                    std::vector<uint64_t> bootstrapSeeds(nBootstrap);
                    std::vector<MLTSampler> samplers(nBootstrap);
                    std::vector<Float> weights(nBootstrap);
                    for (auto &seed : bootstrapSeeds) {
                        seed = dist(rd);
                    }
                    for (size_t i = 0; i < nBootstrap; i++) {
                        samplers[i] = MLTSampler(bootstrapSeeds[i], depth);
                    }

                    parallel_for(nBootstrap, [&, this](uint32_t i, uint32_t tid) {
                        auto record = Radiance(samplers[i], arenas[tid], true);
                        arenas[tid].reset();
                        weights[i] = ScalarContributionFunction(record.radiance);
                    });
                    Distribution1D distribution(weights.data(), weights.size());
                    depthWeight[depth] = distribution.integral();
                    for (auto &chain : markovChains) {
                        auto seedIdx = distribution.sample_discrete(distF(rd));
                        auto seed = bootstrapSeeds[seedIdx];
                        chain.samplers[depth] = MLTSampler(seed, depth);
                    }
                }
                auto depthDist = std::make_shared<Distribution1D>(depthWeight.data(), depthWeight.size());
                Float avgLuminance = std::accumulate(depthWeight.begin(), depthWeight.end(), 0.0f);
                info("Average luminance: {}\n", avgLuminance);
                if (avgLuminance == 0.0f) {
                    error("Average luminance is ZERO; Improper scene setup?\n");
                    error("Aborting...\n");
                    return;
                }
                parallel_for(nChains, [&, this](uint32_t i, uint32_t tid) {
                    for (int depth = 0; depth < maxDepth + 1; depth++) {
                        markovChains[i].radianceRecords[depth] =
                            Radiance(markovChains[i].samplers[depth], arenas[tid], true);
                        markovChains[i].samplers[depth].seed = dist(rd);
                        markovChains[i].samplers[depth].rng = Rng(markovChains[i].samplers[depth].seed);
                        arenas[tid].reset();
                    }
                });
                info("Running {} Markov Chains\n", nChains);
                // done bootstrapping
                auto dim = film->resolution();
                int64_t nTotalMutations = spp * dim.x * dim.y;
                parallel_for(nChains, [&](uint32_t i, uint32_t tid) {
                    Rng rng(i);
                    auto &chain = markovChains[i];
                    chain.depthDist = depthDist;
                    int64_t nChainMutations = std::min<int64_t>((i + 1) * nTotalMutations / nChains, nTotalMutations) -
                                              i * nTotalMutations / nChains;
                    for (int64_t iter = 0; iter < nChainMutations; iter++) {
                        Float depthPdf = 0;
                        int depth = chain.depthDist->sample_discrete(rng.uniformFloat(), &depthPdf);
                        depth = std::min(maxDepth + 1, depth);
                        AKR_ASSERT(depthPdf > 0);
                        auto &sampler = chain.samplers[depth];
                        auto record = Radiance(sampler, arenas[tid], false);
                        arenas[tid].reset();
                        auto &curRecord = chain.radianceRecords[depth];
                        auto LProposed = record.radiance;
                        auto FProposed = ScalarContributionFunction(record.radiance);
                        auto LCurrent = curRecord.radiance;
                        auto FCurrent = ScalarContributionFunction(curRecord.radiance);
                        auto accept = std::min<float>(1.0f, FProposed / FCurrent);
                        Float b = depthWeight[depth];
                        float newWeight =
                            (accept + (sampler.largeStep ? 1.0f : 0.0f)) / (FProposed / b + sampler.largeStepProb);
                        float oldWeight = (1 - accept) / (FCurrent / b + sampler.largeStepProb);
                        Spectrum Lnew = LProposed * newWeight / depthPdf;
                        Spectrum Lold = LCurrent * oldWeight / depthPdf;
                        Lnew = glm::clamp(Lnew, vec3(0), vec3(clamp));
                        Lold = glm::clamp(Lold, vec3(0), vec3(clamp));
                        if (accept > 0) {
                            film->add_splat(Lnew, record.pRaster);
                        }
                        film->add_splat(Lold, curRecord.pRaster);

                        if (sampler.consecutiveRejects >= maxConsecutiveRejects || rng.uniformFloat() < accept) {
                            sampler.Accept();
                            curRecord = record;
                        } else {
                            sampler.Reject();
                        }
                    }
                });
                film->splatScale = 1.0 / spp;
                if (directSamples > 0) {
                    film->splatScale *= (float)directSamples;
                }
                auto endTime = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed = (endTime - beginTime);
                info("Rendering done in {} secs, traced {} rays, {} M rays/sec\n", elapsed.count(),
                     scene->GetRayCounter(), scene->GetRayCounter() / elapsed.count() / 1e6);
            });
        }
        static Float MisWeight(Float pdfA, Float pdfB) {
            pdfA *= pdfA;
            pdfB *= pdfB;
            return pdfA / (pdfA + pdfB);
        }

        void DirectLighting() {
            if (directSamples == 0)
                return;
            ;
            std::string pathTracerSetting = fmt::format(
                R"(
{{
    "type": "PathTracer",
    "props": {{
      "spp": {},
      "enableRR": false,
      "maxDepth":1,
      "minDepth":1
    }}
}}
)",
                directSamples);
            auto setting = json::parse(pathTracerSetting);
            auto pathTracer = serialize::load_from_json<std::shared_ptr<Integrator>>(setting);
            AKR_ASSERT(pathTracer);
            info("Render direct samples\n");
            (void)pathTracer;
            auto task = pathTracer->create_render_task(ctx);
            task->start();
            task->wait();
        }
    };
    class MMLT : public Integrator {
        [[refl]] int spp = 16;
        [[refl]] int max_depth = 7;
        [[refl]] size_t n_bootstrap = 100000u;
        [[refl]] size_t n_chains = 100;
        [[refl]] int n_direct = 16;
        [[refl]] Float clamp = 1e5;
        [[refl]] size_t max_consecutive_rejects = 102400;

      public:
        AKR_IMPLS(Integrator)
        bool supports_mode(RenderMode mode) const {return mode == RenderMode::EProgressive;}
        std::shared_ptr<RenderTask> create_render_task(const RenderContext &ctx) override {
            return std::make_shared<MMLTRenderTask>(ctx, spp, max_depth, n_bootstrap, n_chains, n_direct, clamp,
                                                    max_consecutive_rejects);
        }
    };
#include "generated/MMLT.hpp"
    AKR_EXPORT_PLUGIN(p){}
} // namespace akari
