#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"
#include "matching_engine.pb.h"
#include "storage/storage.hpp"
#include "domain/price.hpp"
#include "server/matching_engine_service.hpp"

using namespace std::chrono_literals;
namespace mat_eng = matching_engine::v1;

static std::string temp_db_path() {
  #ifdef _WIN32
    char buf[MAX_PATH]; GetTempPathA(MAX_PATH, buf);
    std::string dir(buf);
    return dir + "server_test.sqlite";
  #else
    return "/tmp/server_test.sqlite";
  #endif
}

struct ServerFixture : ::testing::Test {
  std::unique_ptr<grpc::Server> server;
  int selected_port = 0;
  std::string db_path;
  std::unique_ptr<mat_eng::MatchingEngine::Stub> stub;
  std::unique_ptr<MatchingEngineServiceImpl> service;

  void SetUp() override {
    db_path = temp_db_path();
    // Clean up any old file
    std::remove(db_path.c_str());

    // Construct your service with db_path (adjust ctor as in your server)
    service = std::make_unique<MatchingEngineServiceImpl>(db_path);

    grpc::ServerBuilder builder;
    builder.RegisterService(service.get());
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    server = builder.BuildAndStart();
    ASSERT_TRUE(server);
    // Keep service alive by moving it into the server's completion queue holder if needed.
    // If your MatchingEngineServiceImpl must outlive this scope, store it as a member.

    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                       grpc::InsecureChannelCredentials());
    stub = mat_eng::MatchingEngine::NewStub(channel);
  }

  void TearDown() override {
    if (server) server->Shutdown();
    std::remove(db_path.c_str());
  }
};

TEST_F(ServerFixture, SubmitOrder_NormalizesAndPersists) {
  mat_eng::OrderRequest req;
  req.set_client_id("C1");
  req.set_symbol("SYM");
  req.set_order_type(mat_eng::LIMIT);
  req.set_side(mat_eng::BUY);       // top-level enum after fix
  req.set_price(10050);
  req.set_scale(8);
  req.set_quantity(10);

  grpc::ClientContext ctx;
  mat_eng::OrderResponse resp;
  auto status = stub->SubmitOrder(&ctx, req, &resp);
  ASSERT_TRUE(status.ok()) << status.error_message();
  ASSERT_TRUE(resp.success());
  ASSERT_FALSE(resp.order_id().empty());

  // Open DB and assert price_q4
  SQLite::Database db(db_path, SQLite::OPEN_READONLY);
  SQLite::Statement stmt(db, "SELECT price FROM orders WHERE order_id=?");
  stmt.bind(1, resp.order_id());
  ASSERT_TRUE(stmt.executeStep());
  auto price_q4 = stmt.getColumn(0).getInt64();
  EXPECT_EQ(price_q4, 1);  // 10050 with scale 8 -> Q4 == 1
}
