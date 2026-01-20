#pragma once

#include <memory>
#include <string>

namespace DirtSim {
namespace Server {

class HttpServer {
public:
    HttpServer(int port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start(const std::string& bindAddress = "0.0.0.0");
    void stop();
    bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
    int port_;
};

} // namespace Server
} // namespace DirtSim
