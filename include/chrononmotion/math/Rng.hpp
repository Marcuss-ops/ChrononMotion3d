#ifndef CHRONONMOTION_RNG_HPP
#define CHRONONMOTION_RNG_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chrononmotion {

    // A seedable random generator whose output is a pure function of its
    // inputs: same seed, same draw sequence, same bits — on every platform,
    // every standard library, every thread.
    //
    // There is deliberately no global instance and no global seed. Streams are
    // per-component: a component owns an Rng (or forks one per element), so
    // adding an element cannot shift the numbers another element draws.
    //
    // State is 16 bytes and copying is well-defined: a copy replays the
    // original's future draws. That is a feature (checkpoint/replay), not an
    // accident.
    class Rng {

    public:
        explicit Rng(std::uint64_t seed = 0): seed_(seed), state_(seed) {}

        // ── Sequential draws ─────────────────────────────────────────────

        std::uint32_t nextUint() {

            return static_cast<std::uint32_t>(nextUint64() >> 32);
        }

        // [0, 1). 24 explicit mantissa bits: exact in fp32, never reaches 1,
        // and identical on every platform because no distribution object is
        // involved.
        float nextFloat() {

            return static_cast<float>(nextUint64() >> 40) * (1.f / 16777216.f);
        }

        // [lo, hi).
        float nextFloat(float lo, float hi) {

            return lo + (hi - lo) * nextFloat();
        }

        // [-range/2, range/2).
        float nextFloatSpread(float range) {

            return range * (nextFloat() - 0.5f);
        }

        // [0, 1) with 53 bits, for accumulators where fp32 granularity shows.
        double nextDouble() {

            return static_cast<double>(nextUint64() >> 11) * (1.0 / 9007199254740992.0);
        }

        // Integer in [lo, hi], both ends inclusive. Multiply-shift mapping:
        // no modulo bias spike, no rejection loop. hi < lo is answered with lo.
        int nextInt(int lo, int hi) {

            if (hi <= lo) return lo;
            const auto range = static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo) + 1u;
            const std::uint64_t m = static_cast<std::uint64_t>(nextUint()) * range;
            return lo + static_cast<int>(m >> 32);
        }

        // Standard normal via Marsaglia's polar method. The one honest caveat:
        // std::log is libm, so gaussian draws are bit-stable per binary but may
        // differ in the last ulps across toolchains. Uniform draws carry no
        // such caveat.
        float nextGaussian() {

            while (true) {
                const float u = nextFloat(-1.f, 1.f);
                const float v = nextFloat(-1.f, 1.f);
                const float s = u * u + v * v;
                if (s > 0.f && s < 1.f) {
                    return u * std::sqrt(-2.f * std::log(s) / s);
                }
            }
        }

        float nextGaussian(float mean, float stddev) {

            return mean + stddev * nextGaussian();
        }

        // Fisher-Yates. std::shuffle is not a substitute: its algorithm is
        // implementation-defined, so the same seed deals different orders on
        // different standard libraries.
        template<class It>
        void shuffle(It first, It last) {

            const auto n = static_cast<int>(last - first);
            for (int i = n - 1; i > 0; --i) {
                const int j = nextInt(0, i);
                auto tmp = first[i];
                first[i] = first[j];
                first[j] = tmp;
            }
        }

        // ── Substreams ───────────────────────────────────────────────────

        // An independent stream, a pure function of (constructor seed,
        // stream). It does not advance this generator, and it does not depend
        // on how many draws happened before the fork.
        [[nodiscard]] Rng fork(std::uint64_t stream) const {

            return Rng(mix(seed_ + 0x9e3779b97f4a7c15ull * (stream + 1u)));
        }

        // ── Stateless counter draws ──────────────────────────────────────

        [[nodiscard]] static std::uint64_t mixBits(std::uint64_t x) {

            return mix(x + 0x9e3779b97f4a7c15ull);
        }

        [[nodiscard]] static std::uint32_t hashUint(std::uint64_t seed, std::uint64_t slot, std::uint64_t stream) {

            return static_cast<std::uint32_t>(
                    mix(seed + 0x9e3779b97f4a7c15ull * slot + 0xd1b54a32d192ed03ull * stream) >> 32);
        }

        [[nodiscard]] static float hash01(std::uint64_t seed, std::uint64_t slot, std::uint64_t stream) {

            return static_cast<float>(hashUint(seed, slot, stream) >> 8) * (1.f / 16777216.f);
        }

        [[nodiscard]] static float hash01(std::uint64_t key) {

            return static_cast<float>(mixBits(key) >> 40) * (1.f / 16777216.f);
        }

        [[nodiscard]] std::uint64_t seed() const { return seed_; }

    private:
        std::uint64_t seed_;
        std::uint64_t state_;

        // splitmix64 (Steele/Lea/Flood; public-domain constants). Chosen over
        // the in-house 32-bit hashes because the 64-bit finalizer passes the
        // usual statistical batteries even for pathological seeds (0, 1, 2…,
        // which are exactly the seeds humans type), while staying two
        // multiplies and three xors — no table, no branch.
        static std::uint64_t mix(std::uint64_t z) {

            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }

        std::uint64_t nextUint64() {

            state_ += 0x9e3779b97f4a7c15ull;
            return mix(state_);
        }
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_RNG_HPP
