#include "engine/engine.hpp"
#include "engine/model.hpp"
#include "storage/storage.hpp"
#include "engine/model.cpp"
#include "utils/strings.hpp"
#include <map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cctype>

// All internals live here, not in the header:
struct MatchingEngine::Impl {
  Storage& db;

  // Symbol-< books
  std::unordered_map<std::string, std::shared_ptr<SymbolBook>> books;
  mutable std::shared_mutex books_mu; // protect the map not the books

  explicit Impl(Storage& db_) : db(db_) {}

  std::shared_ptr<SymbolBook> get_book(const std::string symbolRaw) {
    const std::string symbol = to_upper_ascii(symbolRaw);

    std::unique_lock lk(books_mu);
    // If the symbol exists, return the corresponding book
    if (auto it = books.find(symbol); it != books.end()) {
      return it->second;
    }

    // Otherwise, create a new book for this symbol
    auto new_book = std::make_shared<SymbolBook>();
    books.emplace(symbol, new_book);
    return new_book;
  }

  // -------------- minimal matching helpers --------------
  static bool crosses_buy(int64_t bestAsk, const OrderMsg& o) {
    return o.type == OrdType::Market || bestAsk <= o.price;
  }
  static bool crosses_sell(int64_t bestBid, const OrderMsg& o) {
    return o.type == OrdType::Market || bestBid >= o.price;
  }

  void match_buy(SymbolBook& sb, OrderMsg& taker) {
    auto& price_lvl = sb.asks.levels; // lowest ask first

    while (taker.remaining > 0 && !price_lvl.empty()) {
      auto lvl_it = price_lvl.begin();

      int64_t bestPrice = lvl_it->first;
      // If the best price (lowest ask) is higher than our bid, we can not match the order -> return 
      if (!crosses_buy(bestPrice, taker)) break;

      auto& que = lvl_it->second.fifo; // Take our best ask order
      OrderMsg& maker = que.front();

      const int32_t  filled = std::min(taker.remaining, maker.remaining);
      const int64_t tradePrice = maker.price;
      const int64_t now_ms  = now_epoch_ms();

      // 1) Write a fill row (one per side is fine, or one record that references taker or maker)
      db.add_fill(Storage::FillRow{
          /*order_id=*/ maker.id,          // or taker.id, your choice of convention
          /*symbol=*/   taker.symbol,
          /*fill_price=*/ tradePrice,
          /*scale=*/    taker.scale,
          /*fill_quantity=*/ filled,
          /*event_ts=*/ now_ms
      });

      // 2) Update maker remaining + status
      maker.remaining -= filled;
      db.update_order_status(
          maker.id,
          maker.remaining == 0 ? FILLED : PARTIALLY_FILLED,
          maker.remaining,
          now_ms
      );

      // 3) Update taker remaining + status (taker row already exists in DB)
      taker.remaining -= filled;
      db.update_order_status(
          taker.id,
          taker.remaining == 0 ? FILLED : PARTIALLY_FILLED,
          taker.remaining,
          now_ms
      );

      // 4) Clean up in-memory book
      if (maker.remaining == 0) {
        sb.index.erase(maker.id);
        que.pop_front();
        if (que.empty()) price_lvl.erase(lvl_it);
      }
    }

    // If taker is a LIMIT with remainder, you’ll enqueue it as resting elsewhere.
    // If taker is MARKET and has remainder, set to CANCELED or REJECTED per your policy.
  }

};

