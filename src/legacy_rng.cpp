#include "legacy_rng.h"

namespace { std::uint32_t state = 1; }

void elona_legacy_srand(std::uint32_t seed) { state = seed; }

int elona_legacy_rand() {
    state = state * UINT32_C(214013) + UINT32_C(2531011);
    return static_cast<int>((state >> 16) & UINT32_C(0x7fff));
}
