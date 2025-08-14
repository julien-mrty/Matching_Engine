#include <grpcpp/grpcpp.h>
#include "matching_engine_service.grpc.pb.h"
#include "matching_engine_service.pb.h"

#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#include <iostream>
#include <optional>
#include <algorithm>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <csignal>
#include "storage/storage.hpp"

using namespace std::chrono_literals;
namespace mat_eng_service = matching_engine_service::v1;

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

class MatchingEngineServiceImpl final : public mat_eng_service::MatchingEngineService::Service {
public:
    // Main constructor: explicit to prevent accidental implicit conversions
    explicit MatchingEngineServiceImpl(std::string db_path)
        : storage_(std::move(db_path)), next_id_(1) {
        storage_.init();
        // Seed next_id_ so we don't collide with existing rows
        next_id_.store(storage_.load_next_oid_seq(), std::memory_order_relaxed);
    }

    // RPC: SubmitOrder(OrderRequest) -> OrderResponse
    grpc::Status SubmitOrder(grpc::ServerContext* ctx, const mat_eng_service::OrderRequest* req, 
                             mat_eng_service::OrderResponse* resp) override {

        const auto t0   = std::chrono::steady_clock::now();
        const auto peer = ctx ? ctx->peer() : "unknown";

        auto side_str = [req]() {
            return (req->side() == mat_eng_service::OrderRequest::BUY) ? "BUY" : "SELL";
        };
        auto type_str = [req]() {
            return (req->order_type() == mat_eng_service::OrderRequest::LIMIT) ? "LIMIT" : "MARKET";
        };

        // --- log ----------------------------------------------------------------
        std::cout << "[SubmitOrder] ================================================================= New Order" << std::endl
                << " client_id=" << req->client_id()
                << " symbol="    << req->symbol()
                << " side="      << side_str()
                << " type="      << type_str()
                << " price="     << ((req->order_type() == mat_eng_service::OrderRequest::LIMIT)
                                        ? std::to_string(req->price())
                                        : std::string("NULL"))
                << " scale="     << req->scale()
                << " qty="       << req->quantity()
                << std::endl;

        // --- validation ---------------------------------------------------------
        if (req->symbol().empty()) {
            resp->set_success(false);
            resp->set_error_message("symbol is required");
            std::cerr << "[SubmitOrder][reject] reason=missing_symbol" << std::endl;
            return grpc::Status::OK;
        }
        if (req->quantity() <= 0) {
            resp->set_success(false);
            resp->set_error_message("quantity must be > 0");
            std::cerr << "[SubmitOrder][reject] reason=non_positive_qty qty=" << req->quantity() << std::endl;
            return grpc::Status::OK;
        }
        if (req->order_type() == mat_eng_service::OrderRequest::LIMIT && req->price() <= 0) {
            resp->set_success(false);
            resp->set_error_message("price must be > 0 for LIMIT");
            std::cerr << "[SubmitOrder][reject] reason=non_positive_price price=" << req->price() << std::endl;
            return grpc::Status::OK;
        }

        const auto oid = gen_order_id();
        std::cout << "[SubmitOrder] oid=" << oid << " validated" << std::endl;

        const auto now_ms = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        // --- DB write -----------------------------------------------------------
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(write_mu_); // serialize writes to SQLite
            ok = storage_.insert_new_order(
                oid,
                req->client_id(),
                req->symbol(),
                static_cast<int>(req->side()),
                static_cast<int>(req->order_type()),
                (req->order_type() == mat_eng_service::OrderRequest::LIMIT)
                    ? std::optional<int64_t>(req->price())
                    : std::nullopt,
                req->scale(),
                req->quantity(),
                now_ms
            );
        }

        // --- response & outcome log --------------------------------------------
        resp->set_order_id(oid);
        resp->set_success(ok);
        if (!ok) {
            resp->set_error_message("DB insert failed");
            std::cerr << "[SubmitOrder][error] oid=" << oid << " outcome=db_insert_failed" << std::endl;
        } else {
            std::cout << "[SubmitOrder][ok] oid=" << oid << " inserted" << std::endl;
        }

        const auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "[SubmitOrder] oid=" << oid << " done in " << dur_us << "us" << std::endl;

        return grpc::Status::OK;
    }

    // Minimal implementation for GetOrderBook
    grpc::Status GetOrderBook(grpc::ServerContext*,
                        const mat_eng_service::OrderBookRequest* req,
                        mat_eng_service::OrderBookResponse* resp) override {

        // TODO
        return grpc::Status::OK;
                            /*
                                    std::lock_guard<std::mutex> lk(mu_);
        auto it = books_.find(req->symbol());
        if (it == books_.end()) {
            return Status::OK; // empty book
        }
        const Book& b = it->second;
        for (const auto& bid : b.bids) { *resp->add_bids() = bid; }
        for (const auto& ask : b.asks) { *resp->add_asks() = ask; }
        return Status::OK;
                            */

    }

    // OPTIONAL: leave StreamMarketData unimplemented (default returns UNIMPLEMENTED)
    // Status StreamMarketData(...) override { ... }

private:
    Storage storage_;                    // long-lived DB handle
    std::atomic<uint64_t> next_id_;      // starts at db last ID + 1
    std::mutex write_mu_;                // guard write ops

    // Thread safe monotonic id generator
    std::string gen_order_id() {
        const uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return "OID-" + std::to_string(id);
    }
};

int main(int argc, char** argv) {
    std::string addr = "0.0.0.0:50051"; // 0.0.0.0 listens on all local interfaces

    // Parse command line and flags
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--addr" && i + 1 < argc) addr = argv[++i]; // overrides the listen address
    }

    try {
        // Ensure directory exists and use a FILE path, not a directory
        std::filesystem::path db_file = std::filesystem::path("db") / "matching_engine.db";
        std::error_code ec;
        std::filesystem::create_directories(db_file.parent_path(), ec); // ok if already exists

        MatchingEngineServiceImpl service(db_file.string());

        grpc::ServerBuilder builder;

        int selected_port = 0;
        builder.AddListeningPort(addr, grpc::InsecureServerCredentials(), &selected_port);

        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

        if (!server) {
            std::cerr << "[server] ERROR: BuildAndStart() returned null\n";
            return 1;
        }
                
        if (selected_port == 0) {
            std::cerr << "[server] ERROR: failed to bind " << addr << " (in use or permission issue)\n";
            return 1;
        }

        std::cout << "[server] listening on " << addr << " ; db=" << db_file.string() << "\n";

        std::signal(SIGINT,  on_signal);
        std::signal(SIGTERM, on_signal);

        std::thread stopper([&]{
        while (!g_stop.load(std::memory_order_relaxed)) std::this_thread::sleep_for(50ms);
        server->Shutdown(std::chrono::system_clock::now() + 2s);
        });

        server->Wait();
        stopper.join();
        return 0;

  } catch (const SQLite::Exception& e) {
    std::cerr << "[server] SQLite error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "[server] Fatal error: " << e.what() << "\n";
    return 3;
  }
}
