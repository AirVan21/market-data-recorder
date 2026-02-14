#include "RedisPublisher.h"

#include <string>
#include <vector>

namespace recorder {

RedisPublisher::RedisPublisher(const std::string& redis_uri)
    : redis_(redis_uri) {}

void RedisPublisher::PublishBBO(const connectors::BBO& bbo) {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("market", bbo.market_);

    if (bbo.best_bid_.has_value()) {
        fields.emplace_back("best_bid", std::to_string(*bbo.best_bid_));
    }
    if (bbo.best_bid_size_.has_value()) {
        fields.emplace_back("best_bid_size", std::to_string(*bbo.best_bid_size_));
    }
    if (bbo.best_ask_.has_value()) {
        fields.emplace_back("best_ask", std::to_string(*bbo.best_ask_));
    }
    if (bbo.best_ask_size_.has_value()) {
        fields.emplace_back("best_ask_size", std::to_string(*bbo.best_ask_size_));
    }

    redis_.xadd("bbo", "*", fields.begin(), fields.end());
}

void RedisPublisher::PublishTrade(const connectors::PublicTrade& trade) {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("market", trade.market_);
    fields.emplace_back("id", trade.id_);
    fields.emplace_back("price", std::to_string(trade.price_));
    fields.emplace_back("amount", std::to_string(trade.amount_));
    fields.emplace_back("side", trade.side_);
    fields.emplace_back("timestamp", std::to_string(trade.timestamp_));

    redis_.xadd("trades", "*", fields.begin(), fields.end());
}

} // namespace recorder
