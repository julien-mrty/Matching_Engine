#pragma once
#include <string>
#include "price.hpp"
#include "domain/side.hpp"

struct Order {
  std::string order_id;
  std::string client_id;
  std::string symbol;
  PriceQ4     price_q4;   // ALWAYS Q4
  int64_t     quantity;
  Side        side;

  // Factory that enforces normalization
  static Order FromRaw(std::string order_id,
                       std::string client, 
                       std::string symbol,
                       int64_t raw_price, 
                       int raw_scale,
                       int64_t qty, 
                       Side side) {
    return Order(std::move(order_id),
                 std::move(client), 
                 std::move(symbol),
                 normalize_to_q4(raw_price, raw_scale),
                 qty, 
                 side);
  }

private:
  Order(std::string order_id,
        std::string client_id, 
        std::string symbol,
        PriceQ4 price_q4, 
        int64_t qty, 
        Side side)
    : order_id(std::move(order_id)),
      client_id(std::move(client_id)),
      symbol(std::move(symbol)),
      price_q4(price_q4),
      quantity(qty),
      side(side) {}
};
