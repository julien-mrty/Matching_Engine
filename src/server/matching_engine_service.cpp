#include "server/matching_engine_service.hpp"

#include "domain/order.hpp"
#include "domain/side.hpp"
#include "storage/storage.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>

namespace mat_eng = matching_engine::v1;
using namespace std::chrono_literals;

// ============================= Impl =============================
struct MatchingEngineServiceImpl::Impl {
  explicit Impl(std::string db_path) : storage(std::move(db_path)), next_id(1) {
    storage.init();
    // Seed next_id_ so we don't collide with existing rows
    next_id.store(storage.load_next_oid_seq(), std::memory_order_relaxed);
  }

  Storage storage;                 // long-lived DB handle
  std::atomic<uint64_t> next_id;   // starts at 1
  std::mutex write_mu;             // serialize DB writes

  // Thread-safe monotonic id generator
  std::string gen_order_id() {
    const uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
    return "OID-" + std::to_string(id);
  }
};

// ========================== API surface =========================
MatchingEngineServiceImpl::MatchingEngineServiceImpl(std::string db_path) : d_(std::make_unique<Impl>(std::move(db_path))) {}

MatchingEngineServiceImpl::~MatchingEngineServiceImpl() = default;

// RPC: SubmitOrder(OrderRequest) -> OrderResponse
grpc::Status MatchingEngineServiceImpl::SubmitOrder(
    grpc::ServerContext* ctx,
    const mat_eng::OrderRequest* req,
    mat_eng::OrderResponse* resp) {

  const auto t0   = std::chrono::steady_clock::now();
  const auto peer = ctx ? ctx->peer() : "unknown";

  auto side_str = [req]() { return (req->side() == mat_eng::BUY) ? "BUY" : "SELL"; };
  auto type_str = [req]() { return (req->order_type() == mat_eng::LIMIT) ? "LIMIT" : "MARKET"; };

  // --- log ----------------------------------------------------------------
  std::cout << "[SERVER] [SubmitOrder] ============================================================= New Order\n"
            << " client_id=" << req->client_id()
            << " symbol="    << req->symbol()
            << " side="      << side_str()
            << " type="      << type_str()
            << " price="     << ((req->order_type() == mat_eng::LIMIT)
                                   ? std::to_string(req->price())
                                   : std::string("NULL"))
            << " scale="     << req->scale()
            << " qty="       << req->quantity()
            << std::endl;

  // --- validation ---------------------------------------------------------
  if (req->symbol().empty()) {
    resp->set_success(false);
    resp->set_error_message("symbol is required");
    std::cerr << "[SERVER] [SubmitOrder][reject] reason=missing_symbol\n";
    return grpc::Status::OK;
  }
  if (req->quantity() <= 0) {
    resp->set_success(false);
    resp->set_error_message("quantity must be > 0");
    std::cerr << "[SERVER] [SubmitOrder][reject] reason=non_positive_qty qty=" << req->quantity() << "\n";
    return grpc::Status::OK;
  }
  if (req->order_type() == mat_eng::LIMIT && req->price() <= 0) {
    resp->set_success(false);
    resp->set_error_message("price must be > 0 for LIMIT");
    std::cerr << "[SERVER] [SubmitOrder][reject] reason=non_positive_price price=" << req->price() << "\n";
    return grpc::Status::OK;
  }

  const auto order_id = d_->gen_order_id();
  std::cout << "[SERVER] [SubmitOrder] oid=" << order_id << " validated\n";

  // --- Order creation -----------------------------------------------------
  Order new_order = Order::FromRaw(
      order_id,
      req->client_id(),
      req->symbol(),
      req->price(),   // raw price
      req->scale(),   // raw scale
      req->quantity(),
      req->side()
  );

  // --- DB write -----------------------------------------------------------
  bool ok = false;
  {
    std::lock_guard<std::mutex> lk(d_->write_mu); // serialize writes to SQLite
    ok = d_->storage.insert_new_order(new_order);
  }

  // --- response & outcome log --------------------------------------------
  resp->set_order_id(order_id);
  resp->set_success(ok);
  if (!ok) {
    resp->set_error_message("DB insert failed");
    std::cerr << "[SERVER] [SubmitOrder][error] oid=" << order_id << " outcome=db_insert_failed\n";
  } else {
    std::cout << "[SERVER] [SubmitOrder][ok] oid=" << order_id << " inserted\n";
  }

  const auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::cout << "[SERVER] [SubmitOrder] oid=" << order_id << " done in " << dur_us << "us\n";

  return grpc::Status::OK;
}

grpc::Status MatchingEngineServiceImpl::GetOrderBook(
    grpc::ServerContext*,
    const mat_eng::OrderBookRequest*,
    mat_eng::OrderBookResponse*) {
  // TODO: implement (left blank like your original)
  return grpc::Status::OK;
}
