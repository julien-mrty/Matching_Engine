#pragma once
#include <grpcpp/grpcpp.h>
#include "matching_engine.grpc.pb.h"
#include <memory>
#include <string>

namespace mat_eng = matching_engine::v1;

class MatchingEngineServiceImpl final : public mat_eng::MatchingEngine::Service {
public:
  explicit MatchingEngineServiceImpl(std::string db_path);
  ~MatchingEngineServiceImpl() override;                       // needed for pimpl

  MatchingEngineServiceImpl(const MatchingEngineServiceImpl&)            = delete;
  MatchingEngineServiceImpl& operator=(const MatchingEngineServiceImpl&) = delete;
  MatchingEngineServiceImpl(MatchingEngineServiceImpl&&) noexcept        = default;
  MatchingEngineServiceImpl& operator=(MatchingEngineServiceImpl&&) noexcept = default;

  grpc::Status SubmitOrder(grpc::ServerContext*,
                           const mat_eng::OrderRequest*,
                           mat_eng::OrderResponse*) override;

  grpc::Status GetOrderBook(grpc::ServerContext*,
                            const mat_eng::OrderBookRequest*,
                            mat_eng::OrderBookResponse*) override;

private:
  struct Impl;                    // forward-declared implementation
  std::unique_ptr<Impl> d_;       // pimpl
};
