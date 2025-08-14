#pragma once
#include <memory>
#include <string>
#include <vector>
#include "engine/model.hpp"

class Storage; // forward declaration, don’t pull SQLite headers here

struct Level { int64_t price; int32_t qty; };
class Storage;

class MatchingEngine {
public:
  explicit MatchingEngine(Storage& db);
  ~MatchingEngine();               // important: defined in .cpp (not inline)

  MatchingEngine(MatchingEngine&&) noexcept;
  MatchingEngine& operator=(MatchingEngine&&) noexcept;

  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;

  void submit(OrderMsg newOrd);
  bool cancel(const std::string& oid);
  void snapshot(const std::string& symbol, int depth,
                std::vector<Level>& bids,
                std::vector<Level>& asks) const;

private:
  struct Impl; // forward-declared, hidden
  std::unique_ptr<Impl> pImpl;
};