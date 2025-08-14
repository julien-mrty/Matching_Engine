#include <string>
#include <iostream>
#include <list>
#include <unordered_map>
#include <mutex>
#include <map>
#include "engine/model.hpp"

enum class OrdStatus { New, PartiallyFilled, Filled, Canceled };

struct PriceLevel {
  std::list<OrderMsg> fifo; // FIFO preserves time priority
};

struct OrderRef {
  Side side;                          // which side (bids/asks)
  int64_t price;                      // which price level
  std::list<OrderMsg*>::iterator it;  // iterator to the order inside that level's FIFO
};

struct SymbolBook {
  BookSide<std::greater<int64_t>> bids; // best bid at levels.begin()
  BookSide<std::less<int64_t>>    asks; // best ask at levels.begin()
  std::unordered_map<uint64_t, std::unique_ptr<OrderMsg>> store; // ownership
  std::unordered_map<uint64_t, OrderRef> index; // Locator of the order in the current level
  std::mutex mu; // per-symbol lock
};

template <typename Comparator>
class BookSide {
public:
  // For asks: std::less<int64_t>; for bids: std::greater<int64_t>
  std::map<uint64_t, PriceLevel, Comparator> levels;

  bool empty() const { return levels.empty(); }

  // best price level (lowest ask / highest bid)
  auto best_iter() { return levels.begin(); }
};
