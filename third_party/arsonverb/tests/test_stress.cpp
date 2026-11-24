// Deterministic real-time stress across sample rates and block sizes.
// Deliberately tests numerical safety and transport lifecycle, not sound quality.
#include <arverb/Reverb.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <atomic>

static std::atomic<unsigned> allocations{0};
static std::atomic<unsigned> releases{0};
void* operator new(std::size_t n) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p) releases.fetch_add(1, std::memory_order_relaxed); std::free(p); }
void operator delete[](void* p) noexcept { if (p) releases.fetch_add(1, std::memory_order_relaxed); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) releases.fetch_add(1, std::memory_order_relaxed); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) releases.fetch_add(1, std::memory_order_relaxed); std::free(p); }
static void require(bool pass, const char* why) {
    if (!pass) { std::fprintf(stderr, "STRESS FAIL: %s\n",why); std::abort(); }
}
static uint32_t next(uint32_t& s) noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
int main() {
    using namespace arverb;
    constexpr std::array<std::size_t,8> sizes{0, 1, 3, 17, 64, 255, 513, 2048};
    std::array<float,2048> inL{}, inR{}, outL{}, outR{};
    uint32_t seed = 0x537a9b21u;
    Engine core;
    for (double fs : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        core.prepare(fs);
        for (int iter = 0; iter < 300; ++iter) {
            const std::size_t n = sizes[iter % static_cast<int>(sizes.size())];
            for (std::size_t p = 0; p < parameterCount; ++p) {
                const auto param = static_cast<Parameter>(p);
                const auto& info = parameterInfo(param);
                const auto rnd = static_cast<float>(next(seed) & 0xffffffu) / 16777215.f;
                core.setParameter(param, info.minimum + rnd * (info.maximum - info.minimum));
            }
            for (std::size_t i = 0; i < n; ++i) {
                const auto a = static_cast<float>(static_cast<int32_t>(next(seed))) / 2147483648.f;
                inL[i] = 0.2f * a;
                inR[i] = -0.18f * a;
            }
            if (n > 12 && iter % 20 == 0) {
                inL[1] = std::numeric_limits<float>::quiet_NaN();
                inR[7] = std::numeric_limits<float>::infinity();
                inL[10] = std::numeric_limits<float>::max();
            }
            const unsigned before = allocations.load(std::memory_order_relaxed);
            core.process(inL.data(), inR.data(), outL.data(), outR.data(), n);
            require(before == allocations.load(std::memory_order_relaxed),
                    "RT processing allocated heap while automating all parameters");
            for (std::size_t i = 0; i < n; ++i) {
                require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "nonfinite sample");
                require(std::fabs(outL[i]) < 1.0e5f && std::fabs(outR[i]) < 1.0e5f,
                        "recursive feedback diverged");
            }
            if (iter % 79 == 0) core.reset();
        }
    }
    // A separate lifecycle engine is repeatedly re-prepared/released at
    // different rates. Track the net number of *live* new/delete allocations
    // rather than assuming no leak merely because process() never allocates.
    const unsigned liveBefore = allocations.load(std::memory_order_relaxed) -
                                releases.load(std::memory_order_relaxed);
    {
        Engine lifecycle;
        for (int repeat = 0; repeat < 12; ++repeat) {
            for (double fs : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
                lifecycle.prepare(fs);
                lifecycle.reset();
                inL.fill(0.0f);
                inR.fill(0.0f);
                lifecycle.process(inL.data(), inR.data(), outL.data(), outR.data(), 64);
            }
        }
    }
    const unsigned liveAfter = allocations.load(std::memory_order_relaxed) -
                               releases.load(std::memory_order_relaxed);
    require(liveAfter == liveBefore, "repeated prepare/reset/destruction leaked live heap blocks");
    std::puts("PASS P8: 5 sample rates, 1500 randomized automation blocks, no callback heap and no lifecycle net leaks");
}
