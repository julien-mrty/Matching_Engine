#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"
#include "matching_engine.pb.h"

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
#include "storage/storage.h"

using namespace std::chrono_literals;
namespace mat_eng = matching_engine::v1;

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

class MatchingEngineServiceImpl final : public mat_eng::MatchingEngine::Service {
public:
    //MatchingEngineServiceImpl() : next_id_(1) {    }
    // Main ctor: explicit to prevent accidental implicit conversions
    explicit MatchingEngineServiceImpl(std::string db_path)
        : storage_(std::move(db_path)), next_id_(1) {
        storage_.init();
    }

    // RPC: SubmitOrder(OrderRequest) -> OrderResponse
    grpc::Status SubmitOrder(grpc::ServerContext*,
                       const mat_eng::OrderRequest* req,
                       mat_eng::OrderResponse* resp) override {
        // Validation 
        if (req->symbol().empty()) {
            resp->set_success(false);
            resp->set_error_message("symbol is required");
            return grpc::Status::OK;
        }
        if (req->quantity() <= 0) {
            resp->set_success(false);
            resp->set_error_message("quantity must be > 0");
            return grpc::Status::OK;
        }
        if (req->order_type() == mat_eng::OrderRequest::LIMIT && req->price() <= 0) {
            resp->set_success(false);
            resp->set_error_message("price must be > 0 for LIMIT");
            return grpc::Status::OK;
        }

        const auto oid = gen_order_id();

        const auto now_ms = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        // Serialize writes; SQLite is single-writer-friendly
        std::lock_guard<std::mutex> lk(write_mu_);
        const bool ok = storage_.insert_new_order(
            oid,
            req->client_id(), req->symbol(),
            static_cast<int>(req->side()),
            static_cast<int>(req->order_type()),
            (req->order_type() == mat_eng::OrderRequest::LIMIT)
                ? std::optional<int64_t>(req->price())
                : std::nullopt,
            req->scale(), 
            req->quantity(), 
            now_ms
        );

        resp->set_order_id(oid);
        resp->set_success(ok);
        if (!ok) resp->set_error_message("DB insert failed");
        return grpc::Status::OK;
    }

    // Minimal implementation for GetOrderBook
    grpc::Status GetOrderBook(grpc::ServerContext*,
                        const mat_eng::OrderBookRequest* req,
                        mat_eng::OrderBookResponse* resp) override {

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
    std::atomic<uint64_t> next_id_;      // starts at 1
    std::mutex write_mu_;                // guard write ops

    // Thread-safe monotonic id generator
    std::string gen_order_id() {
        const uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return "OID-" + std::to_string(id);
    }
};

int main(int argc, char** argv) {
    std::string addr = "0.0.0.0:50051"; // 0.0.0.0 listens on all local interfaces
    std::string seed_symbol = "SYM";

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
        if (selected_port == 0) {
        std::cerr << "[server] ERROR: failed to bind " << addr << " (in use or permission issue)\n";
        return 1;
        }

        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
        if (!server) {
        std::cerr << "[server] ERROR: BuildAndStart() returned null\n";
        return 1;
        }

        std::cout << "[server] listening on " << addr
                << " ; db=" << db_file.string() << "\n";

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
