#pragma once
#include <cstdint>

// HSP 3.4 Windows reference sequences use the MSVC CRT 15-bit LCG.
// Keep its state independent of the host C library's rand implementation.
void elona_legacy_srand(std::uint32_t seed);
int elona_legacy_rand();
