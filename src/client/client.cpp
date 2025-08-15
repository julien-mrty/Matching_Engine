#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"
#include "matching_engine.pb.h"
#include <iostream>
#include <memory>
#include <string>

namespace mat_eng = matching_engine::v1;

static void usage(const char* prog) {
    std::cerr <<
      "Usage:\n"
      "  " << prog << " <addr> <client_id> <symbol> <BUY|SELL> <LIMIT|MARKET> <price> <scale> <qty>\n"
      "  Example:\n"
      "  " << prog << " localhost:50051 C1 SYM BUY LIMIT 10050 2 10\n"
      "  " << prog << " localhost:50051 C2 SYM SELL MARKET 0 0 25\n";
}

int main(int argc, char** argv) {
    if (argc < 9) { usage(argv[0]); return 1; }

    std::string addr     = argv[1];
    std::string clientId = argv[2];
    std::string symbol   = argv[3];
    std::string sSide    = argv[4];
    std::string sType    = argv[5];
    int64_t     price    = std::stoi(argv[6]);
    int         scale    = std::stoi(argv[7]);
    int         qty      = std::stoi(argv[8]);

    // InsecureServerCredentials() is fine for local dev. For anything else, switch to TLS
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    std::unique_ptr<mat_eng::MatchingEngine::Stub> stub = mat_eng::MatchingEngine::NewStub(channel);

    mat_eng::OrderRequest req;
    req.set_client_id(clientId);
    req.set_symbol(symbol);
    req.set_side( (sSide == "BUY") ? mat_eng::BUY : mat_eng::SELL );
    req.set_order_type( (sType == "LIMIT") ? mat_eng::LIMIT : mat_eng::MARKET );
    req.set_price(price);
    req.set_scale(scale);
    req.set_quantity(qty);

    grpc::ClientContext ctx;
    mat_eng::OrderResponse resp;
    grpc::Status status = stub->SubmitOrder(&ctx, req, &resp);

    if (!status.ok()) {
        std::cerr << "[client] RPC failed: " << status.error_code() << " - " << status.error_message() << "\n";
        return 2;
    }
    if (!resp.success()) {
        std::cerr << "[client] rejected: " << resp.error_message() << "\n";
        return 3;
    }
    std::cout << "[client] accepted order_id=" << resp.order_id() << "\n";
    return 0;
}
