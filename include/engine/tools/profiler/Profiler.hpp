#pragma once

#include <chrono>
#include <array>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cmath>

namespace engine
{
    namespace tools
    {
        struct CStrHash
        {
            std::size_t operator()(const char *s) const noexcept
            {
                std::size_t h = 0;
                for (; *s; ++s)
                    h = h * 131 + static_cast<unsigned char>(*s);
                return h;
            }
        };

        struct CStrEqual
        {
            bool operator()(const char *a, const char *b) const noexcept
            {
                return std::strcmp(a, b) == 0;
            }
        };

        class Profiler
        {
        public:
            enum class Mode
            {
                Disabled,
                Enabled
            };

            struct SampleData
            {
                int count = 0;
                float total = 0.0f;
                float min = 0.0f;
                float max = 0.0f;

                float average() const { return count > 0 ? total / count : 0.0f; }
            };

            using SampleMap = std::unordered_map<const char *, SampleData, CStrHash, CStrEqual>;

            static Profiler &get()
            {
                static Profiler instance;
                return instance;
            }

            void setMode(Mode newMode)
            {
                if (mode != newMode)
                {
                    mode = newMode;
                    if (mode == Mode::Disabled)
                        reset();
                }
            }

            Mode getMode() const { return mode; }
            bool isEnabled() const { return mode == Mode::Enabled; }

            void beginFrame()
            {
                if (mode == Mode::Disabled)
                    return;
                frameStart = std::chrono::high_resolution_clock::now();
            }

            void endFrame()
            {
                if (mode == Mode::Disabled)
                    return;

                auto frameEnd = std::chrono::high_resolution_clock::now();
                float ft = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();

                frameTimes[frameHead] = ft;
                frameHead = (frameHead + 1) % maxFrames;
                if (frameCount < maxFrames)
                    ++frameCount;
            }

            void beginSample(const char *name)
            {
                if (mode == Mode::Disabled)
                    return;
                sampleStack.push_back({name, std::chrono::high_resolution_clock::now()});
            }

            void endSample()
            {
                if (mode == Mode::Disabled || sampleStack.empty())
                    return;

                auto &sample = sampleStack.back();
                float duration = std::chrono::duration<float, std::milli>(
                                     std::chrono::high_resolution_clock::now() - sample.start)
                                     .count();

                auto it = samples.find(sample.name);
                if (it == samples.end())
                {
                    samples[sample.name] = {1, duration, duration, duration};
                }
                else
                {
                    auto &d = it->second;
                    d.count++;
                    d.total += duration;
                    if (duration < d.min)
                        d.min = duration;
                    if (duration > d.max)
                        d.max = duration;
                }

                sampleStack.pop_back();
            }

            void reset()
            {
                frameHead = 0;
                frameCount = 0;
                samples.clear();
                sampleStack.clear();
            }

            float getAverageFPS() const
            {
                if (frameCount == 0)
                    return 0.0f;
                float sum = 0.0f;
                for (size_t i = 0; i < frameCount; ++i)
                    sum += frameTimes[i];
                float avg = sum / static_cast<float>(frameCount);
                return avg > 0.0f ? 1000.0f / avg : 0.0f;
            }

            float getFrameTime() const
            {
                if (frameCount == 0)
                    return 0.0f;
                size_t last = (frameHead + maxFrames - 1) % maxFrames;
                return frameTimes[last];
            }

            size_t getFrameHistoryCount() const { return frameCount; }

            /// Последние `frameCount` времён кадра (мс), по возрастанию времени (хронологически).
            size_t copyRecentFrameTimesMs(float *out, size_t maxOut) const
            {
                size_t n = maxOut < frameCount ? maxOut : frameCount;
                for (size_t i = 0; i < n; ++i)
                {
                    size_t idx = (frameHead + maxFrames - frameCount + i) % maxFrames;
                    out[i] = frameTimes[idx];
                }
                return n;
            }

            void getFrameTimeStats(float &outMin, float &outMax, float &outP95, float &outAvg) const
            {
                outMin = outMax = outP95 = outAvg = 0.0f;
                if (frameCount == 0)
                    return;
                std::vector<float> buf(frameCount);
                for (size_t i = 0; i < frameCount; ++i)
                {
                    size_t idx = (frameHead + maxFrames - frameCount + i) % maxFrames;
                    buf[i] = frameTimes[idx];
                }
                auto mm = std::minmax_element(buf.begin(), buf.end());
                outMin = *mm.first;
                outMax = *mm.second;
                outAvg = 0.0f;
                for (float v : buf)
                    outAvg += v;
                outAvg /= static_cast<float>(buf.size());
                std::sort(buf.begin(), buf.end());
                const size_t pidx = static_cast<size_t>(std::floor(0.95 * static_cast<double>(buf.size() - 1)));
                outP95 = buf.empty() ? 0.0f : buf[pidx];
            }

            const SampleMap &getSamples() const { return samples; }

        private:
            Profiler() = default;

            struct Sample
            {
                const char *name;
                std::chrono::high_resolution_clock::time_point start;
            };

            Mode mode = Mode::Disabled;
            static constexpr size_t maxFrames = 600;

            std::array<float, maxFrames> frameTimes{};
            size_t frameHead = 0;
            size_t frameCount = 0;

            std::chrono::high_resolution_clock::time_point frameStart;
            std::vector<Sample> sampleStack;
            SampleMap samples;
        };

        struct ScopedSample
        {
            ScopedSample(const char *name)
            {
                if (Profiler::get().isEnabled())
                    Profiler::get().beginSample(name);
            }

            ~ScopedSample()
            {
                if (Profiler::get().isEnabled())
                    Profiler::get().endSample();
            }
        };

    } // namespace tools
} // namespace engine

// ==================== Макросы ====================

#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_IMPL(a, b) a##b

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// Внутренний Profiler (DebugOverlay / агрегаты): только явные точки — ECS-системы и IService.
#ifdef ENGINE_ENABLE_PROFILING
#define PROFILE_SCOPE(name) engine::tools::ScopedSample CONCAT(profile_, __LINE__)(name)
#define PROFILE_BEGIN_FRAME() engine::tools::Profiler::get().beginFrame()
#define PROFILE_END_FRAME() engine::tools::Profiler::get().endFrame()
#else
#define PROFILE_SCOPE(name)
#define PROFILE_BEGIN_FRAME()
#define PROFILE_END_FRAME()
#endif

// Tracy — отдельно; не смешивать с PROFILE_SCOPE.
#ifdef TRACY_ENABLE
#define TRACY_ZONE(name) \
    ZoneScoped; \
    ZoneName(name, std::strlen(name))
/// Имя зоны из строки времени выполнения (например system->getName()).
#define TRACY_ZONE_DYNAMIC(name_cstr)             \
    ZoneScoped;                                   \
    ZoneName((name_cstr), std::strlen(name_cstr))
#define TRACY_FRAME_MARK() FrameMark
#else
#define TRACY_ZONE(name)
#define TRACY_ZONE_DYNAMIC(name_cstr)
#define TRACY_FRAME_MARK()
#endif
