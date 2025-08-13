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

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using namespace std::chrono_literals;
namespace mat_eng = matching_engine::v1;

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

// In-memory book: just vectors of the generated proto Order messages.
struct Book {
    std::vector<mat_eng::Order> bids; // BUY
    std::vector<mat_eng::Order> asks; // SELL
};

class MatchingEngineServiceImpl final : public mat_eng::MatchingEngine::Service {
public:
    MatchingEngineServiceImpl() : next_id_(1) {}

    // RPC: SubmitOrder(OrderRequest) -> OrderResponse
    Status SubmitOrder(ServerContext*,
                       const mat_eng::OrderRequest* req,
                       mat_eng::OrderResponse* resp) override {
        // Validation 
        if (req->symbol().empty()) {
            resp->set_success(false);
            resp->set_error_message("symbol is required");
            return Status::OK;
        }
        if (req->quantity() <= 0) {
            resp->set_success(false);
            resp->set_error_message("quantity must be > 0");
            return Status::OK;
        }
        if (req->order_type() == mat_eng::OrderRequest::LIMIT && req->price() <= 0) {
            resp->set_success(false);
            resp->set_error_message("price must be > 0 for LIMIT");
            return Status::OK;
        }

        // Create an Order from the request
        mat_eng::Order order;
        order.set_client_id(req->client_id());
        order.set_quantity(req->quantity());
        order.set_price(req->price());
        order.set_scale(req->scale());
        order.set_side(req->side() == mat_eng::OrderRequest::BUY ? mat_eng::Order::BUY : mat_eng::Order::SELL);

        // Assign server-side order_id
        const std::string oid = std::to_string(next_id_.fetch_add(1));
        order.set_order_id(oid);

        // Store in book (LIMIT orders rest; MARKET orders here just validate and "accept")
        {
            std::lock_guard<std::mutex> lk(mu_);
            Book& b = books_[req->symbol()];
            if (order.side() == mat_eng::Order::BUY) b.bids.push_back(order);
            else b.asks.push_back(order);
        }

        // Respond
        resp->set_order_id(oid);
        resp->set_success(true);
        resp->set_error_message("");
        return Status::OK;
    }

    // Minimal implementation for GetOrderBook
    Status GetOrderBook(ServerContext*,
                        const mat_eng::OrderBookRequest* req,
                        mat_eng::OrderBookResponse* resp) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = books_.find(req->symbol());
        if (it == books_.end()) {
            return Status::OK; // empty book
        }
        const Book& b = it->second;
        for (const auto& bid : b.bids) { *resp->add_bids() = bid; }
        for (const auto& ask : b.asks) { *resp->add_asks() = ask; }
        return Status::OK;
    }

    // OPTIONAL: leave StreamMarketData unimplemented (default returns UNIMPLEMENTED)
    // Status StreamMarketData(...) override { ... }

    // Optional helper to seed the book for quick testing
    void seed_demo(const std::string& symbol) {
        std::lock_guard<std::mutex> lk(mu_);
        Book& b = books_[symbol];

        mat_eng::Order b1; b1.set_order_id(std::to_string(next_id_.fetch_add(1)));
        b1.set_client_id("seedB1"); b1.set_side(mat_eng::Order::BUY); b1.set_price(9950); b1.set_quantity(100);
        b.bids.push_back(b1);

        mat_eng::Order a1; a1.set_order_id(std::to_string(next_id_.fetch_add(1)));
        a1.set_client_id("seedA1"); a1.set_side(mat_eng::Order::SELL); a1.set_price(10050); a1.set_quantity(80);
        b.asks.push_back(a1);
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, Book> books_;
    std::atomic<uint64_t> next_id_;
};

int main(int argc, char** argv) {
    std::string addr = "0.0.0.0:50051"; // 0.0.0.0 listens on all local interfaces
    bool seed = false;
    std::string seed_symbol = "SYM";

    // Parse command line and flags
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--addr" && i + 1 < argc) addr = argv[++i]; // overrides the listen address
        else if (a == "--seed" && i + 1 < argc) { seed = true; seed_symbol = argv[++i]; } // turns on seeding and sets the symbol name
    }

    MatchingEngineServiceImpl service;
    if (seed) service.seed_demo(seed_symbol);

    ServerBuilder builder;
    // InsecureServerCredentials() is fine for local dev. For anything else, switch to TLS
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials()); 
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[server] listening on " << addr << (seed ? " (seeded)" : "") << "\n";

    // Install signal handlers (Ctrl+C = SIGINT; also handle SIGTERM)
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // Watcher thread: when signaled, ask gRPC to shut down gracefully
    std::thread stopper([&]{
      while (!g_stop.load(std::memory_order_relaxed)) std::this_thread::sleep_for(50ms);
      // Give active RPCs up to 2 seconds to finish
      server->Shutdown(std::chrono::system_clock::now() + 2s);
    });

    // Block here until Shutdown() is called
    server->Wait();
    stopper.join();
    return 0;
}
