MatchingEngine::MatchingEngine(Storage& db) : pImpl(std::make_unique<Impl>(db)) {}

MatchingEngine::~MatchingEngine() = default;         // needs complete Impl here
MatchingEngine::MatchingEngine(MatchingEngine&&) noexcept = default;
MatchingEngine& MatchingEngine::operator=(MatchingEngine&&) noexcept = default;

void MatchingEngine::submit(OrderMsg newOrd) {
  auto& sb = book(newOrd.symbol);
  std::unique_lock lk(sb.mu);

  SQLite::Transaction txn(db.sql());

  // 1) Insert NEW order row (status NEW, remaining=qty) — you already have insert_new_order(...)
  db_.insert_new_order(...);

  // 2) Match against opposite side
  if (newOrd.side == Buy)   match_buy(sb, taker, txn);
  else                      match_sell(sb, taker, txn);

  // 3) If remaining > 0 and it’s LIMIT, add to resting book; else mark Filled/IOC behavior
  if (taker.remaining > 0 && taker.type == OrdType::Limit)
      enqueue_resting(sb, std::move(ownerPtr), txn);
  else
      db_.update_order_status(taker.id, (taker.remaining==0? FILLED : CANCELED/REJECTED), taker.remaining, now);

  txn.commit();
}

void MatchingEngine::match_buy(SymbolBook& sb, Order& taker, SQLite::Transaction& txn) {
  while (taker.remaining > 0 && !sb.asks.empty()) {
    auto it = sb.asks.best_iter();               // lowest ask
    const int64_t bestPx = it->first;
    if (taker.type == OrdType::Limit && bestPx > taker.px) break;

    auto& lvl = it->second.fifo;
    Order* maker = lvl.front();

    int32_t fillQty = std::min(taker.remaining, maker->remaining);
    int64_t tradePx = maker->px;

    // Write fill row
    db_.add_fill({ maker->id /*or taker id twice if you log both*/, taker.symbol, tradePx, scale, fillQty, now });

    // Update maker
    maker->remaining -= fillQty;
    db_.update_order_status(maker->id,
        maker->remaining == 0 ? (int)OrdStatus::Filled : (int)OrdStatus::PartiallyFilled,
        maker->remaining, now);

    if (maker->remaining == 0) {
      lvl.pop_front();
      sb.index.erase(maker->id);
      sb.store.erase(maker->id); // frees memory
      if (lvl.empty()) sb.asks.levels.erase(it);
    }

    // Update taker
    taker.remaining -= fillQty;
  }
}

void MatchingEngine::enqueue_resting(SymbolBook& sb,
                                     std::unique_ptr<Order> o,
                                     SQLite::Transaction& txn) {
  auto* raw = o.get();
  auto& sideLevels = (o->side == Side::Buy) ? sb.bids.levels : sb.asks.levels;
  auto& lvl = sideLevels[o->px].fifo;   // creates level if missing
  auto it = lvl.insert(lvl.end(), raw); // FIFO: push_back

  sb.index.emplace(o->id, OrderRef{ o->side, o->px, it });
  sb.store.emplace(o->id, std::move(o));
  // DB row already exists as NEW; nothing else to do here.
}

bool MatchingEngine::remove_resting(SymbolBook& sb, const std::string& oid, SQLite::Transaction& txn) {
  auto hit = sb.index.find(oid);

  if (hit == sb.index.end()) return false;

  auto [side, px, lit] = hit->second;
  auto& sideLevels = (side == Side::Buy) ? sb.bids.levels : sb.asks.levels;
  auto pit = sideLevels.find(px);
  pit->second.fifo.erase(lit);

  if (pit->second.fifo.empty()) sideLevels.erase(pit);

  sb.index.erase(hit);
  sb.store.erase(oid);
  db_.update_order_status(oid, (int)OrdStatus::Canceled, /*remaining*/0, now);
  
  return true;
}


bool MatchingEngine::cancel(const std::string& oid) { /* ... */ return true; }
void MatchingEngine::snapshot(const std::string& s,int d,
                              std::vector<Level>& bids,
                              std::vector<Level>& asks) const { /* ... */ }



// -------------------------- PIMPL storage ------------------------------

struct MatchingEngine::Impl {
  Storage& db;

  // symbol -> book
  std::unordered_map<std::string, std::shared_ptr<SymbolBook>> books;
  mutable std::shared_mutex books_mu;  // protects the map (not the books)

  explicit Impl(Storage& db_) : db(db_) {}

  static std::string normalize(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
  }

  std::shared_ptr<SymbolBook> get_book(const std::string& symRaw) {
    const std::string sym = normalize(symRaw);
    std::unique_lock lk(books_mu);
    if (auto it = books.find(sym); it != books.end()) return it->second;
    auto sb = std::make_shared<SymbolBook>();
    books.emplace(sym, sb);
    return sb;
  }

  // ------- minimal matching helpers (sketch; fill with your logic) -----

  static bool crosses_buy(long long bestAsk, const Order& o) {
    return o.type == OrdType::Market || bestAsk <= o.price;
  }
  static bool crosses_sell(long long bestBid, const Order& o) {
    return o.type == OrdType::Market || bestBid >= o.price;
  }

  void match_buy(SymbolBook& sb, Order& taker /*, txn, fill writer ...*/) {
    auto& opp = sb.asks.levels; // lowest ask first
    while (taker.remaining > 0 && !opp.empty()) {
      auto lvl_it = opp.begin();
      long long bestPx = lvl_it->first;
      if (!crosses_buy(bestPx, taker)) break;

      auto& q = lvl_it->second.fifo;        // FIFO at best ask
      Order& maker = q.front();

      const int fill = std::min(taker.remaining, maker.remaining);
      const long long tradePx = maker.price;

      // TODO: write fill via db.add_fill(...), update maker/taker rows in a txn
      maker.remaining -= fill;
      taker.remaining -= fill;

      if (maker.remaining == 0) {
        sb.index.erase(maker.id);
        q.pop_front();
        if (q.empty()) opp.erase(lvl_it);
      }
    }
  }

  void match_sell(SymbolBook& sb, Order& taker /*, txn, fill writer ...*/) {
    auto& opp = sb.bids.levels; // highest bid first
    while (taker.remaining > 0 && !opp.empty()) {
      auto lvl_it = opp.begin();
      long long bestPx = lvl_it->first;
      if (!crosses_sell(bestPx, taker)) break;

      auto& q = lvl_it->second.fifo;
      Order& maker = q.front();

      const int fill = std::min(taker.remaining, maker.remaining);
      const long long tradePx = maker.price;

      // TODO: write fill via db.add_fill(...), update maker/taker rows in a txn
      maker.remaining -= fill;
      taker.remaining -= fill;

      if (maker.remaining == 0) {
        sb.index.erase(maker.id);
        q.pop_front();
        if (q.empty()) opp.erase(lvl_it);
      }
    }
  }

}; // end Impl

// ------------------------ MatchingEngine API ---------------------------

MatchingEngine::MatchingEngine(Storage& db) : p_(std::make_unique<Impl>(db)) {}
MatchingEngine::~MatchingEngine() = default;
MatchingEngine::MatchingEngine(MatchingEngine&&) noexcept = default;
MatchingEngine& MatchingEngine::operator=(MatchingEngine&&) noexcept = default;

void MatchingEngine::submit(const OrderMsg& msg) {
  // Build internal Order from your public DTO
  Order o{/*id*/ msg.id, /*client*/ msg.client, /*symbol*/ msg.symbol,
           /*side*/ msg.side == 0 ? Side::Buy : Side::Sell,
           /*type*/ msg.type == 0 ? OrdType::Limit : OrdType::Market,
           /*price*/ msg.price, msg.scale, msg.qty, msg.qty, /*ts*/ 0};

  auto sb = p_->get_book(o.symbol);      // map lookup/creation under books_mu
  std::lock_guard g(sb->mu);             // exclusive access to this symbol

  // Begin SQLite txn (p_->db)
  // SQLite::Transaction txn(p_->db.sql());

  if (o.side == Side::Buy) {
    p_->match_buy(*sb, o /*, txn*/);
  } else {
    p_->match_sell(*sb, o /*, txn*/);
  }

  // If remaining > 0 and LIMIT, rest it on the appropriate side
  if (o.remaining > 0 && o.type == OrdType::Limit) {
    auto& side = (o.side == Side::Buy) ? sb->bids.levels : sb->asks.levels;
    auto& lvl  = side[o.price];
    auto it    = lvl.fifo.insert(lvl.fifo.end(), std::move(o));
    sb->index.emplace(it->id, Locator{ (it->side), (it->price), it });
    // db.update_order_status(it->id, PARTIALLY_FILLED/NEW, it->remaining, now);
  } else {
    // db.update_order_status(o.id, FILLED or CANCELED (if market remainder policy), 0, now);
  }

  // txn.commit();
}

bool MatchingEngine::cancel(const std::string& symbol, const std::string& oid) {
  auto sb = p_->get_book(symbol);
  std::lock_guard g(sb->mu);

  auto hit = sb->index.find(oid);
  if (hit == sb->index.end()) return false;

  const Locator& loc = hit->second;
  auto& sideLevels = (loc.side == Side::Buy) ? sb->bids.levels : sb->asks.levels;
  auto pit = sideLevels.find(loc.price);
  if (pit == sideLevels.end()) return false;

  pit->second.fifo.erase(loc.it);
  if (pit->second.fifo.empty()) sideLevels.erase(pit);
  sb->index.erase(hit);

  // db.update_order_status(oid, /*Canceled*/ 3, 0, now);
  return true;
}

void MatchingEngine::snapshot(const std::string& symbol, int depth,
                              std::vector<Level>& bids,
                              std::vector<Level>& asks) const {
  auto sb = p_->get_book(symbol);
  std::lock_guard g(sb->mu);

  bids.clear(); asks.clear();
  // collect up to depth price levels
  for (auto it = sb->bids.levels.begin();
       it != sb->bids.levels.end() && (int)bids.size() < depth; ++it) {
    int qty = 0; for (auto& o : it->second.fifo) qty += o.remaining;
    bids.push_back(Level{it->first, qty});
  }
  for (auto it = sb->asks.levels.begin();
       it != sb->asks.levels.end() && (int>asks.size() < depth; ++it)) {
    int qty = 0; for (auto& o : it->second.fifo) qty += o.remaining;
    asks.push_back(Level{it->first, qty});
  }
}

} // namespace me