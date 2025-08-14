#pragma once
#include "matching_engine.pb.h"

namespace mat_eng = matching_engine::v1;
using Side = mat_eng::Side;

// Optional: compile-time guard so DB/engine break loudly if proto values change
static_assert(int(mat_eng::BUY)  == 1, "Proto enum changed: update DB CHECK + code");
static_assert(int(mat_eng::SELL) == 2, "Proto enum changed: update DB CHECK + code");
