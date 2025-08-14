#pragma once
#include <string>

enum class Side { Buy, Sell };
enum class OrdType { Limit, Market };

struct OrderMsg {
  uint64_t id;
  std::string client_id, symbol;
  Side side;
  OrdType type;
  int64_t price;        // integer price (use scale to convert)
  int32_t qty;          // original
  int32_t remaining;    // decremented on fills
  int64_t ts_ns;        // monotonic/timepoint for debugging
};

struct Level { 
  int64_t price; 
  int32_t qty; 
};
