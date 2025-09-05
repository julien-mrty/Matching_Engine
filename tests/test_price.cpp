#include <gtest/gtest.h>
#include "domain/price.hpp"
#include "domain/order.hpp"
#include "matching_engine.pb.h" // for Side if needed

TEST(PriceScale, NormalizeExamples) {
  // raw=10050
  EXPECT_EQ(normalize_to_q4(10050, 9), 0);        // 10050 / 10^5 -> 0 (trunc)
  EXPECT_EQ(normalize_to_q4(10050, 8), 1);        // 10050 / 10^4 -> 1
  EXPECT_EQ(normalize_to_q4(10050, 7), 10);       // 10050 / 10^3 -> 10
  EXPECT_EQ(normalize_to_q4(10050, 6), 100);      // 10050 / 10^2 -> 100
  EXPECT_EQ(normalize_to_q4(10050, 2), 1005000);  // *10^(4-2)
  EXPECT_EQ(normalize_to_q4(10050, 0), 100500000);// *10^4
}

TEST(OrderFactory, FromRawNormalizes) {
  auto o = Order::FromRaw("OID-X", "C1", "SYM", 10050, 8, 10, matching_engine::v1::BUY);
  EXPECT_EQ(o.price_q4, 1);
  EXPECT_EQ(o.quantity, 10);
}
