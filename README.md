# Market Data Recorder

A C++20 pipeline that captures real-time cryptocurrency market data from [Bitvavo](https://bitvavo.com) and persists it to ClickHouse via Redis Streams.

## Architecture

```
                         Redis Streams
                        ┌────────────┐
┌──────────────┐  XADD  │  bbo       │  XREAD   ┌──────────────┐
│              │ ──────► │  trades    │ ────────► │              │
│   Producer   │         └────────────┘          │   Consumer   │
│              │                                  │              │
│  Bitvavo WS  │                                  │  Batch Insert│
│  → Redis     │                                  │  → ClickHouse│
└──────┬───────┘                                  └──────┬───────┘
       │                                                 │
       │ WebSocket                              Native protocol
       ▼                                                 ▼
┌──────────────┐                                 ┌──────────────┐
│   Bitvavo    │                                 │  ClickHouse  │
│   Exchange   │                                 │   (storage)  │
└──────────────┘                                 └──────────────┘
```

The system is split into two independent binaries connected by Redis Streams, allowing them to be scaled, deployed, and restarted independently.

### Producer

Connects to Bitvavo's WebSocket API via the [bitvavo-connector](https://github.com/AirVan21/bitvavo-connector) library. Subscribes to BBO (Best Bid/Offer) ticker updates and public trades for configured markets (default: BTC-EUR, ETH-EUR). Each incoming event is published to a Redis Stream using `XADD`:

- **`bbo` stream** — fields: `market`, `best_bid`, `best_bid_size`, `best_ask`, `best_ask_size` (optional fields omitted when absent)
- **`trades` stream** — fields: `market`, `id`, `price`, `amount`, `side`, `timestamp`

### Consumer

Reads from both Redis Streams using `XREAD BLOCK` and batch-inserts rows into ClickHouse. Flushing is triggered by either:

- **Batch size** — 100 rows accumulated per stream, or
- **Time interval** — 1 second since the last flush

On shutdown (SIGINT), any remaining buffered rows are flushed before exit.

## Project Structure

```
market-data-recorder/
├── Dependencies/
│   └── bitvavo-connector/       # Git submodule — WebSocket client library
├── src/
│   ├── RedisPublisher.h/.cpp    # XADD wrapper for BBO and trade streams
│   ├── producer_main.cpp        # Bitvavo WebSocket → Redis Streams
│   └── consumer_main.cpp        # Redis Streams → ClickHouse
├── sql/
│   └── schema.sql               # ClickHouse table definitions
├── CMakeLists.txt               # Builds Producer and Consumer binaries
├── conanfile.py                 # Conan 1.x dependency manifest
├── docker-compose.yml           # Redis + ClickHouse services
└── Dockerfile                   # Build image
```

## Prerequisites

- **CMake** 3.16+
- **C++20** compiler (GCC 10+, Clang 12+)
- **Conan 1.64.1** (not Conan 2.x) — `pip install conan==1.64.1`
- **Boost** (ASIO + Beast) and **OpenSSL** system packages
- **Docker** and **Docker Compose** (for Redis and ClickHouse)

### System packages

```bash
# Ubuntu / Debian
sudo apt-get install libboost-all-dev libssl-dev

# macOS
brew install boost openssl
```

## Quick Start

### 1. Clone

```bash
git clone --recursive https://github.com/AirVan21/market-data-recorder.git
cd market-data-recorder
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

### 2. Start infrastructure

```bash
docker compose up -d
```

This starts Redis 7 on port 6379 and ClickHouse on ports 8123 (HTTP) and 9000 (native). The ClickHouse schema (`sql/schema.sql`) is auto-applied on first startup.

### 3. Build

```bash
mkdir -p build && cd build
conan install .. --build=missing
cmake ..
cmake --build .
```

This produces two binaries: `build/Producer` and `build/Consumer`.

### 4. Run

In separate terminals:

```bash
# Terminal 1 — consume from Redis and write to ClickHouse
./build/Consumer

# Terminal 2 — connect to Bitvavo and publish to Redis
./build/Producer
```

## Configuration

Both binaries are configured via environment variables:

| Variable | Default | Used by |
|---|---|---|
| `REDIS_URI` | `tcp://127.0.0.1:6379` | Producer, Consumer |
| `CLICKHOUSE_HOST` | `127.0.0.1` | Consumer |

Example:

```bash
REDIS_URI=tcp://redis-host:6379 CLICKHOUSE_HOST=clickhouse-host ./build/Producer
```

## ClickHouse Schema

Two tables are created in the default database:

### `bbo` — Best Bid/Offer snapshots

| Column | Type | Description |
|---|---|---|
| `timestamp` | `DateTime64(3)` | Redis Stream entry timestamp (ms) |
| `market` | `LowCardinality(String)` | Trading pair (e.g. `BTC-EUR`) |
| `best_bid` | `Nullable(Float64)` | Best bid price |
| `best_bid_size` | `Nullable(Float64)` | Best bid volume |
| `best_ask` | `Nullable(Float64)` | Best ask price |
| `best_ask_size` | `Nullable(Float64)` | Best ask volume |

### `trades` — Public trades

| Column | Type | Description |
|---|---|---|
| `timestamp` | `DateTime64(3)` | Redis Stream entry timestamp (ms) |
| `market` | `LowCardinality(String)` | Trading pair |
| `id` | `String` | Exchange trade ID |
| `price` | `Float64` | Trade price |
| `amount` | `Float64` | Trade volume |
| `side` | `LowCardinality(String)` | `buy` or `sell` |

Both tables use `MergeTree()` engine ordered by `(market, timestamp)` for efficient range queries per market.

### Example queries

```sql
-- Latest BBO per market
SELECT market, best_bid, best_ask
FROM bbo
ORDER BY timestamp DESC
LIMIT 1 BY market;

-- Trade volume per market in the last hour
SELECT market, sum(amount) AS total_volume, count() AS trade_count
FROM trades
WHERE timestamp > now() - INTERVAL 1 HOUR
GROUP BY market;

-- OHLCV 1-minute candles
SELECT
    market,
    toStartOfMinute(timestamp) AS minute,
    argMin(price, timestamp) AS open,
    max(price) AS high,
    min(price) AS low,
    argMax(price, timestamp) AS close,
    sum(amount) AS volume
FROM trades
GROUP BY market, minute
ORDER BY market, minute;
```

## Dependencies

Managed by Conan 1.x (`conanfile.py`):

| Package | Version | Purpose |
|---|---|---|
| [hiredis](https://github.com/redis/hiredis) | 1.2.0 | C Redis client |
| [redis-plus-plus](https://github.com/sewenew/redis-plus-plus) | 1.3.10 | C++ Redis wrapper (Streams API) |
| [clickhouse-cpp](https://github.com/ClickHouse/clickhouse-cpp) | 2.5.1 | C++ ClickHouse native client |
| [openssl](https://www.openssl.org/) | 1.1.1t | TLS for WebSocket connection |
| [rapidjson](https://rapidjson.org/) | cci.20220822 | JSON parsing (transitive via bitvavo-connector) |

System dependencies (not managed by Conan): **Boost** (ASIO + Beast).

## Docker Build

To build both binaries inside a container:

```bash
docker build -t market-data-recorder .
```

## License

See [bitvavo-connector](https://github.com/AirVan21/bitvavo-connector) for upstream license terms.
