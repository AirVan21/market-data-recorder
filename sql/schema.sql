CREATE TABLE IF NOT EXISTS bbo (
    timestamp DateTime64(3),
    market    LowCardinality(String),
    best_bid  Nullable(Float64),
    best_bid_size Nullable(Float64),
    best_ask  Nullable(Float64),
    best_ask_size Nullable(Float64)
) ENGINE = MergeTree() ORDER BY (market, timestamp);

CREATE TABLE IF NOT EXISTS trades (
    timestamp DateTime64(3),
    market    LowCardinality(String),
    id        String,
    price     Float64,
    amount    Float64,
    side      LowCardinality(String)
) ENGINE = MergeTree() ORDER BY (market, timestamp);
