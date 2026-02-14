#pragma once

#include <string>
#include <sw/redis++/redis++.h>

#include "BitvavoClient.h"

namespace recorder {

struct RedisPublisher {
    explicit RedisPublisher(const std::string& redis_uri);

    void PublishBBO(const connectors::BBO& bbo);
    void PublishTrade(const connectors::PublicTrade& trade);

private:
    sw::redis::Redis redis_;
};

} // namespace recorder
