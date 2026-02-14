cmake_minimum_required(VERSION 3.16)
project(MarketDataRecorder LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan
if(EXISTS "${CMAKE_BINARY_DIR}/conan_paths.cmake")
    include("${CMAKE_BINARY_DIR}/conan_paths.cmake")
endif()

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(hiredis REQUIRED)
find_package(redis++ REQUIRED)
find_package(clickhouse-cpp REQUIRED)

# bitvavo-connector submodule (exposes bitvavo_client target)
add_subdirectory(Dependencies/bitvavo-connector)

# Producer binary
add_executable(Producer src/producer_main.cpp src/RedisPublisher.cpp)
target_link_libraries(Producer PRIVATE bitvavo_client hiredis::hiredis redis++::redis++)

# Consumer binary
add_executable(Consumer src/consumer_main.cpp)
target_link_libraries(Consumer PRIVATE hiredis::hiredis redis++::redis++ clickhouse-cpp::clickhouse-cpp)
