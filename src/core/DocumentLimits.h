#pragma once
#include <algorithm>
#include <cstdint>

namespace compositor::limits {
inline constexpr int maxSide=30000;
inline constexpr uint64_t surfacePixels=200000000;
// Separate an individual working surface from the sum of immutable document assets.
uint64_t documentPixels();
}
