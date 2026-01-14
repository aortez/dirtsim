#include "HttpServer.h"

#include <WebResources.h>
#include <cpp-httplib/httplib.h>

#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Server {

struct HttpServer::Impl {
    std::atomic<bool> running_{ false };
    std::thread thread_;
    std::unique_ptr<httplib::Server> server_;

    void runServer(int port)
    {
        server_ = std::make_unique<httplib::Server>();

        server_->Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_redirect("/garden");
        });

        server_->Get("/garden", [](const httplib::Request&, httplib::Response& res) {
            // Serve embedded web resources (generated at build time from src/server/web/).
            res.set_content(std::string(WebResources::kGardenHtml), "text/html");
        });

        spdlog::info("HttpServer: Starting on port {}", port);
        spdlog::info("HttpServer: Dashboard available at http://localhost:{}/garden", port);

        running_ = true;
        if (!server_->listen("0.0.0.0", port)) {
            spdlog::error("HttpServer: Failed to start on port {}", port);
            running_ = false;
        }
    }
};

HttpServer::HttpServer(int port) : pImpl_(std::make_unique<Impl>()), port_(port)
{}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    if (pImpl_->running_) {
        return true;
    }

    pImpl_->thread_ = std::thread([this]() { pImpl_->runServer(port_); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return pImpl_->running_;
}

void HttpServer::stop()
{
    if (!pImpl_->running_) {
        return;
    }

    if (pImpl_->server_) {
        pImpl_->server_->stop();
    }

    if (pImpl_->thread_.joinable()) {
        pImpl_->thread_.join();
    }

    pImpl_->running_ = false;
    spdlog::info("HttpServer: Stopped");
}

bool HttpServer::isRunning() const
{
    return pImpl_->running_;
}

} // namespace Server
} // namespace DirtSim
