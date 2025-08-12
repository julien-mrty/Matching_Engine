#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"  // generated
#include "matching_engine.pb.h"       // generated (for te::Ping)
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
  // Args: <addr> [message]
  std::string addr   = (argc > 1) ? argv[1] : "localhost:50051";
  std::string prompt = (argc > 2) ? argv[2] : "hello";

  auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  std::unique_ptr<te::TradingEngine::Stub> stub = te::TradingEngine::NewStub(channel);

  te::Ping req;
  req.set_msg(prompt);

  grpc::ClientContext ctx;                // you can set deadlines/metadata here
  te::Ping resp;
  grpc::Status status = stub->Say(&ctx, req, &resp);

  if (!status.ok()) {
    std::cerr << "[client] RPC failed: " << status.error_code()
              << " - " << status.error_message() << "\n";
    return 1;
  }

  std::cout << "[client] reply: " << resp.msg() << "\n";
  return 0;
}
