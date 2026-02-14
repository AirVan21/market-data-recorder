#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include <boost/asio.hpp>

#include "BitvavoClient.h"
#include "RedisPublisher.h"

static std::atomic<bool> g_running{true};

void SignalHandler(int) {
    g_running = false;
}

int main() {
    std::cout << "Market Data Producer (Bitvavo -> Redis Streams)" << std::endl;

    std::signal(SIGINT, SignalHandler);

    const char* redis_uri = std::getenv("REDIS_URI");
    if (!redis_uri) {
        redis_uri = "tcp://127.0.0.1:6379";
    }

    auto publisher = std::make_shared<recorder::RedisPublisher>(redis_uri);

    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context);

    connectors::BitvavoClient::Callbacks callbacks;
    callbacks.handle_bbo_ = [publisher](const connectors::BBO& bbo) {
        publisher->PublishBBO(bbo);
    };
    callbacks.handle_public_trade_ = [publisher](const connectors::PublicTrade& trade) {
        publisher->PublishTrade(trade);
    };
    callbacks.handle_error_ = [](const std::string& error) {
        std::cerr << "[ERROR] " << error << std::endl;
    };
    callbacks.handle_connection_ = [](bool connected) {
        std::cout << (connected ? "[CONN] Connected" : "[CONN] Disconnected") << std::endl;
    };

    connectors::BitvavoClient client(io_context, std::move(callbacks));

    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    auto connect_future = client.Connect();
    if (!connect_future.get()) {
        std::cerr << "Failed to connect" << std::endl;
        work_guard.reset();
        io_context.stop();
        io_thread.join();
        return 1;
    }

    auto sub_ticker = client.SubscribeTicker({"BTC-EUR", "ETH-EUR"});
    if (!sub_ticker.get()) {
        std::cerr << "Failed to subscribe to ticker" << std::endl;
        client.Disconnect();
        work_guard.reset();
        io_context.stop();
        io_thread.join();
        return 1;
    }

    auto sub_trades = client.SubscribeTrades({"BTC-EUR", "ETH-EUR"});
    if (!sub_trades.get()) {
        std::cerr << "Failed to subscribe to trades" << std::endl;
        client.Disconnect();
        work_guard.reset();
        io_context.stop();
        io_thread.join();
        return 1;
    }

    std::cout << "Publishing to Redis. Ctrl+C to quit..." << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down..." << std::endl;
    client.Disconnect();
    work_guard.reset();
    io_context.stop();
    io_thread.join();

    return 0;
}
