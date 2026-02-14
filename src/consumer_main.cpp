#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <sw/redis++/redis++.h>
#include <clickhouse/client.h>

static std::atomic<bool> g_running{true};

void SignalHandler(int) {
    g_running = false;
}

struct BBORow {
    int64_t timestamp_ms_;
    std::string market_;
    std::optional<double> best_bid_;
    std::optional<double> best_bid_size_;
    std::optional<double> best_ask_;
    std::optional<double> best_ask_size_;
};

struct TradeRow {
    int64_t timestamp_ms_;
    std::string market_;
    std::string id_;
    double price_;
    double amount_;
    std::string side_;
};

static std::optional<double> ParseOptionalDouble(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string& key)
{
    auto it = fields.find(key);
    if (it != fields.end()) {
        return std::stod(it->second);
    }
    return std::nullopt;
}

static int64_t ParseStreamIdTimestamp(const std::string& stream_id) {
    auto dash = stream_id.find('-');
    if (dash != std::string::npos) {
        return std::stoll(stream_id.substr(0, dash));
    }
    return std::stoll(stream_id);
}

static void FlushBBOs(clickhouse::Client& ch, std::vector<BBORow>& rows) {
    if (rows.empty()) return;

    clickhouse::Block block;

    auto col_ts = std::make_shared<clickhouse::ColumnDateTime64>(3);
    auto col_market = std::make_shared<clickhouse::ColumnString>();
    auto col_bid = std::make_shared<clickhouse::ColumnNullableT<clickhouse::ColumnFloat64>>();
    auto col_bid_size = std::make_shared<clickhouse::ColumnNullableT<clickhouse::ColumnFloat64>>();
    auto col_ask = std::make_shared<clickhouse::ColumnNullableT<clickhouse::ColumnFloat64>>();
    auto col_ask_size = std::make_shared<clickhouse::ColumnNullableT<clickhouse::ColumnFloat64>>();

    for (const auto& row : rows) {
        col_ts->Append(row.timestamp_ms_);
        col_market->Append(row.market_);

        if (row.best_bid_) {
            col_bid->Append(*row.best_bid_);
        } else {
            col_bid->Append(std::nullopt);
        }
        if (row.best_bid_size_) {
            col_bid_size->Append(*row.best_bid_size_);
        } else {
            col_bid_size->Append(std::nullopt);
        }
        if (row.best_ask_) {
            col_ask->Append(*row.best_ask_);
        } else {
            col_ask->Append(std::nullopt);
        }
        if (row.best_ask_size_) {
            col_ask_size->Append(*row.best_ask_size_);
        } else {
            col_ask_size->Append(std::nullopt);
        }
    }

    block.AppendColumn("timestamp", col_ts);
    block.AppendColumn("market", col_market);
    block.AppendColumn("best_bid", col_bid);
    block.AppendColumn("best_bid_size", col_bid_size);
    block.AppendColumn("best_ask", col_ask);
    block.AppendColumn("best_ask_size", col_ask_size);

    ch.Insert("bbo", block);
    std::cout << "[CH] Flushed " << rows.size() << " BBO rows" << std::endl;
    rows.clear();
}

static void FlushTrades(clickhouse::Client& ch, std::vector<TradeRow>& rows) {
    if (rows.empty()) return;

    clickhouse::Block block;

    auto col_ts = std::make_shared<clickhouse::ColumnDateTime64>(3);
    auto col_market = std::make_shared<clickhouse::ColumnString>();
    auto col_id = std::make_shared<clickhouse::ColumnString>();
    auto col_price = std::make_shared<clickhouse::ColumnFloat64>();
    auto col_amount = std::make_shared<clickhouse::ColumnFloat64>();
    auto col_side = std::make_shared<clickhouse::ColumnString>();

    for (const auto& row : rows) {
        col_ts->Append(row.timestamp_ms_);
        col_market->Append(row.market_);
        col_id->Append(row.id_);
        col_price->Append(row.price_);
        col_amount->Append(row.amount_);
        col_side->Append(row.side_);
    }

    block.AppendColumn("timestamp", col_ts);
    block.AppendColumn("market", col_market);
    block.AppendColumn("id", col_id);
    block.AppendColumn("price", col_price);
    block.AppendColumn("amount", col_amount);
    block.AppendColumn("side", col_side);

    ch.Insert("trades", block);
    std::cout << "[CH] Flushed " << rows.size() << " trade rows" << std::endl;
    rows.clear();
}

int main() {
    std::cout << "Market Data Consumer (Redis Streams -> ClickHouse)" << std::endl;

    std::signal(SIGINT, SignalHandler);

    const char* redis_uri = std::getenv("REDIS_URI");
    if (!redis_uri) {
        redis_uri = "tcp://127.0.0.1:6379";
    }

    const char* ch_host = std::getenv("CLICKHOUSE_HOST");
    if (!ch_host) {
        ch_host = "127.0.0.1";
    }

    sw::redis::Redis redis(redis_uri);

    clickhouse::ClientOptions ch_opts;
    ch_opts.SetHost(ch_host);
    clickhouse::Client ch(ch_opts);

    const size_t batch_size = 100;
    const auto flush_interval = std::chrono::milliseconds(1000);

    std::string last_bbo_id = "$";
    std::string last_trades_id = "$";

    std::vector<BBORow> bbo_buffer;
    std::vector<TradeRow> trade_buffer;
    auto last_flush = std::chrono::steady_clock::now();

    std::cout << "Consuming from Redis. Ctrl+C to quit..." << std::endl;

    while (g_running) {
        using Attrs = std::vector<std::pair<std::string, sw::redis::OptionalString>>;
        using Item = std::pair<std::string, Attrs>;
        using Stream = std::pair<std::string, std::vector<Item>>;
        std::vector<Stream> result;

        redis.xread("bbo", last_bbo_id, "trades", last_trades_id,
                     std::chrono::milliseconds(500), 0, std::back_inserter(result));

        for (const auto& [stream_key, items] : result) {
            for (const auto& [id, attrs] : items) {
                std::unordered_map<std::string, std::string> fields;
                for (const auto& [k, v] : attrs) {
                    if (v) {
                        fields[k] = *v;
                    }
                }

                int64_t ts = ParseStreamIdTimestamp(id);

                if (stream_key == "bbo") {
                    last_bbo_id = id;
                    BBORow row;
                    row.timestamp_ms_ = ts;
                    row.market_ = fields["market"];
                    row.best_bid_ = ParseOptionalDouble(fields, "best_bid");
                    row.best_bid_size_ = ParseOptionalDouble(fields, "best_bid_size");
                    row.best_ask_ = ParseOptionalDouble(fields, "best_ask");
                    row.best_ask_size_ = ParseOptionalDouble(fields, "best_ask_size");
                    bbo_buffer.push_back(std::move(row));
                } else if (stream_key == "trades") {
                    last_trades_id = id;
                    TradeRow row;
                    row.timestamp_ms_ = ts;
                    row.market_ = fields["market"];
                    row.id_ = fields["id"];
                    row.price_ = std::stod(fields["price"]);
                    row.amount_ = std::stod(fields["amount"]);
                    row.side_ = fields["side"];
                    trade_buffer.push_back(std::move(row));
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        bool time_to_flush = (now - last_flush) >= flush_interval;

        if (bbo_buffer.size() >= batch_size || (time_to_flush && !bbo_buffer.empty())) {
            FlushBBOs(ch, bbo_buffer);
        }
        if (trade_buffer.size() >= batch_size || (time_to_flush && !trade_buffer.empty())) {
            FlushTrades(ch, trade_buffer);
        }
        if (time_to_flush) {
            last_flush = now;
        }
    }

    // Final flush
    FlushBBOs(ch, bbo_buffer);
    FlushTrades(ch, trade_buffer);

    std::cout << "Consumer shut down." << std::endl;
    return 0;
}
