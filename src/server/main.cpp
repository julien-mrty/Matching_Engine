#include "server/matching_engine_service.hpp"
#include "storage/storage.hpp"

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

static std::atomic<bool> g_stop{false};
static void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

int main(int argc, char** argv) {
  std::string addr = "0.0.0.0:50051"; // 0.0.0.0 listens on all local interfaces

  // Parse command line and flags
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--addr" && i + 1 < argc) addr = argv[++i];
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
      std::cerr << "[SERVER] ERROR: BuildAndStart() returned null\n";
      return 1;
    }
    if (selected_port == 0) {
      std::cerr << "[SERVER] ERROR: failed to bind " << addr << " (in use or permission issue)\n";
      return 1;
    }

    std::cout << "[SERVER] listening on " << addr << " ; db=" << db_file.string() << "\n";

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
    std::cerr << "[SERVER] SQLite error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "[SERVER] Fatal error: " << e.what() << "\n";
    return 3;
  }
}
