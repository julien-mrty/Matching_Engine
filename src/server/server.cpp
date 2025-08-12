#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"  // generated from your .proto
#include <iostream>
#include <memory>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class TradingEngineServiceImpl final : public te::TradingEngine::Service {
public:
  // Unary RPC: Say(Ping) -> Ping
  Status Say(ServerContext* /*ctx*/,
             const te::Ping* request,
             te::Ping* reply) override {
    // Minimal echo behavior: prepend something to prove it's the server
    reply->set_msg(std::string("server echo: ") + request->msg());
    return Status::OK;
  }
};

int main(int argc, char** argv) {
  std::string address = "0.0.0.0:50051";
  if (argc > 1) address = argv[1];

  TradingEngineServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials()); // use TLS in prod
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "[server] listening on " << address << std::endl;

  server->Wait(); // block forever
  return 0;
}
