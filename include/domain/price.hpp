#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

using PriceQ4 = int64_t;
inline constexpr int kTargetScale = 4;
inline constexpr int64_t POW10[19] = {
  1LL,10LL,100LL,1000LL,10000LL,100000LL,1000000LL,10000000LL,100000000LL,
  1000000000LL,10000000000LL,100000000000LL,1000000000000LL,10000000000000LL,
  100000000000000LL,1000000000000000LL,10000000000000000LL,
  100000000000000000LL,1000000000000000000LL
};

inline PriceQ4 normalize_to_q4(int64_t price, int raw_scale) {
  if (raw_scale < 0 || raw_scale > 18) throw std::invalid_argument("scale out of range");
  if (raw_scale == kTargetScale) return price;

  const int diff = kTargetScale - raw_scale;

  if (diff > 0) {
    const int64_t mul = POW10[diff];
    if (price > 0 && price > std::numeric_limits<int64_t>::max() / mul) throw std::overflow_error("overflow");
    if (price < 0 && price < std::numeric_limits<int64_t>::min() / mul) throw std::overflow_error("underflow");
    return price * mul;
  } else {
    return price / POW10[-diff];  // trunc toward 0
  }
}
