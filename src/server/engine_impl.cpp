class TradingEngineImpl final : public TradingEngine::Service {
public:
    grpc::Status SubmitOrder(grpc::ServerContext* context, const OrderRequest* request, OrderResponse* response) override {
        Order order = convertToOrder(request);
        order_book.add_order(order);
        order_book.match_orders();
        response->set_success(true);
        return grpc::Status::OK;
    }
};