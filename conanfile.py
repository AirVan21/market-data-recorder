from conans import ConanFile, CMake

class MarketDataRecorderConan(ConanFile):
    name = "market-data-recorder"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake_find_package", "cmake_paths"

    def requirements(self):
        self.requires("hiredis/1.2.0")
        self.requires("redis-plus-plus/1.3.10")
        self.requires("clickhouse-cpp/2.5.1")
        self.requires("openssl/1.1.1t")
        self.requires("rapidjson/cci.20220822")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
