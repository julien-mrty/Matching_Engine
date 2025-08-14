#pragma once

#include "domain/order.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <cstdint>
#include <optional>
#include <string>

// Row used when recording a fill
struct FillRow {
  std::string order_id;
  std::string symbol;
  int64_t     fill_price;     // scaled int
  int32_t     scale;
  int32_t     fill_quantity;
  int64_t     event_ts;       // epoch ms
};

// Lightweight persistence layer backed by SQLite (via SQLiteCpp).
// Notes:
//  - Call init() once after construction to set pragmas and create tables.
//  - All methods return bool on success; they never throw (exceptions are caught internally).
class Storage {
public:
  // Opens (or creates) the database file.
  // Thread-safe mode: OPEN_FULLMUTEX; you should still serialize writes at the app level.
  explicit Storage(const std::string& db_path);

  // PRAGMAs + schema creation.
  void init();

  uint64_t load_next_oid_seq() const;

  // Insert a new order in state NEW (status=0) with remaining_quantity=quantity.
  // price is nullable for MARKET orders (pass std::nullopt).
  bool insert_new_order(const Order& o);

  // Update order status and remaining qty.
  bool update_order_status(const std::string& order_id,
                           int status,           // 0 NEW, 1 PARTIALLY_FILLED, 2 FILLED, 3 CANCELED, 4 REJECTED
                           int32_t remaining_qty,
                           int64_t now_ms);

  // Append a fill row (use a short transaction when you also update the order).
  bool add_fill(const FillRow& f);

  // Convenience book snapshots (based on stored orders; real-time book should live in memory).
  std::optional<int64_t> best_bid(const std::string& symbol) const; // side=0
  std::optional<int64_t> best_ask(const std::string& symbol) const; // side=1

private:
  // Order and fills DDL.
  void create_schema_();

private:
  SQLite::Database db_;
};
