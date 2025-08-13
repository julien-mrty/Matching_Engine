#include "storage/storage.h"

#include <stdexcept>

// -------------------- ctor / init --------------------

Storage::Storage(const std::string& db_path)
  : db_(db_path.c_str(),
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX)
{
  // Simple contention handling for brief write-lock situations
  db_.setBusyTimeout(5000); // ms
}

void Storage::init() {
  // Pragmas: good defaults for a service (tune as you like)
  db_.exec("PRAGMA journal_mode=WAL;");
  db_.exec("PRAGMA synchronous=NORMAL;"); // use FULL for stronger durability
  db_.exec("PRAGMA foreign_keys=ON;");

  create_schema_();
}

void Storage::create_schema_() {
  db_.exec(R"SQL(
CREATE TABLE IF NOT EXISTS orders (
  order_id            TEXT PRIMARY KEY,
  client_id           TEXT NOT NULL,
  symbol              TEXT NOT NULL,
  side                INTEGER NOT NULL,        -- 0=BUY, 1=SELL
  order_type          INTEGER NOT NULL,        -- 0=LIMIT, 1=MARKET
  price               INTEGER,                 -- nullable (MARKET)
  scale               INTEGER NOT NULL,
  quantity            INTEGER NOT NULL,
  status              INTEGER NOT NULL,        -- 0 NEW, 1 PARTIALLY_FILLED, 2 FILLED, 3 CANCELED, 4 REJECTED
  remaining_quantity  INTEGER NOT NULL,
  created_ts          INTEGER NOT NULL,        -- epoch ms
  updated_ts          INTEGER NOT NULL
);
)SQL");

  db_.exec(R"SQL(
CREATE INDEX IF NOT EXISTS idx_orders_symbol_side
  ON orders(symbol, side);
)SQL");

  db_.exec(R"SQL(
CREATE INDEX IF NOT EXISTS idx_orders_client
  ON orders(client_id);
)SQL");

  db_.exec(R"SQL(
CREATE TABLE IF NOT EXISTS fills (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  order_id            TEXT NOT NULL,
  symbol              TEXT NOT NULL,
  fill_price          INTEGER NOT NULL,
  scale               INTEGER NOT NULL,
  fill_quantity       INTEGER NOT NULL,
  event_ts            INTEGER NOT NULL,
  FOREIGN KEY(order_id) REFERENCES orders(order_id)
);
)SQL");

  db_.exec(R"SQL(
CREATE INDEX IF NOT EXISTS idx_fills_order
  ON fills(order_id);
)SQL");
}

// -------------------- writes --------------------

bool Storage::insert_new_order(const std::string& order_id,
                               const std::string& client_id,
                               const std::string& symbol,
                               int side,
                               int order_type,
                               std::optional<int64_t> price,
                               int32_t scale,
                               int32_t quantity,
                               int64_t now_ms)
{
  try {
    SQLite::Transaction txn(db_);
    SQLite::Statement stmt(db_,
      "INSERT INTO orders(order_id, client_id, symbol, side, order_type, price, scale, quantity, status, remaining_quantity, created_ts, updated_ts) "
      "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");

    stmt.bind(1,  order_id);
    stmt.bind(2,  client_id);
    stmt.bind(3,  symbol);
    stmt.bind(4,  side);
    stmt.bind(5,  order_type);
    if (price.has_value())
      stmt.bind(6, static_cast<long long>(*price));
    else
      stmt.bind(6); // NULL
    stmt.bind(7,  scale);
    stmt.bind(8,  quantity);
    stmt.bind(9,  0); // NEW
    stmt.bind(10, quantity);
    stmt.bind(11, static_cast<long long>(now_ms));
    stmt.bind(12, static_cast<long long>(now_ms));

    stmt.exec();
    txn.commit();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool Storage::update_order_status(const std::string& order_id,
                                  int status,
                                  int32_t remaining_qty,
                                  int64_t now_ms)
{
  try {
    SQLite::Transaction txn(db_);
    SQLite::Statement stmt(db_,
      "UPDATE orders SET status=?, remaining_quantity=?, updated_ts=? WHERE order_id=?");

    stmt.bind(1, status);
    stmt.bind(2, remaining_qty);
    stmt.bind(3, static_cast<long long>(now_ms));
    stmt.bind(4, order_id);

    stmt.exec();
    txn.commit();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool Storage::add_fill(const FillRow& f)
{
  try {
    SQLite::Transaction txn(db_);

    SQLite::Statement stmt(db_,
      "INSERT INTO fills(order_id, symbol, fill_price, scale, fill_quantity, event_ts) "
      "VALUES (?,?,?,?,?,?)");

    stmt.bind(1, f.order_id);
    stmt.bind(2, f.symbol);
    stmt.bind(3, static_cast<long long>(f.fill_price));
    stmt.bind(4, f.scale);
    stmt.bind(5, f.fill_quantity);
    stmt.bind(6, static_cast<long long>(f.event_ts));

    stmt.exec();

    // If you also update the parent order here (e.g., decrement remaining), do it
    // before commit in the same txn for atomicity.

    txn.commit();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// -------------------- reads --------------------

std::optional<int64_t> Storage::best_bid(const std::string& symbol) const
{
  try {
    SQLite::Statement q(db_,
      "SELECT MAX(price) "
      "FROM orders "
      "WHERE symbol=? AND side=0 AND status IN (0,1)"); // NEW or PARTIALLY_FILLED

    q.bind(1, symbol);

    if (q.executeStep()) { // aggregates still return one row
      const auto col = q.getColumn(0);
      if (!col.isNull())
        return static_cast<int64_t>(col.getInt64());
    }
    return std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<int64_t> Storage::best_ask(const std::string& symbol) const
{
  try {
    SQLite::Statement q(db_,
      "SELECT MIN(price) "
      "FROM orders "
      "WHERE symbol=? AND side=1 AND status IN (0,1)");

    q.bind(1, symbol);

    if (q.executeStep()) {
      const auto col = q.getColumn(0);
      if (!col.isNull())
        return static_cast<int64_t>(col.getInt64());
    }
    return std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}
