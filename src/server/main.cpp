#include "trading_engine.grpc.pb.h"

class OrderBook {
private:
    std::map<double, std::list<Order>> bids;  // price -> orders (sorted high to low)
    std::map<double, std::list<Order>> asks;  // price -> orders (sorted low to high)
public:
    void add_order(const Order& order);
    void match_orders();
};