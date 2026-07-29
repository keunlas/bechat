#include "listener.h"

#include <spdlog/spdlog.h>

#include <memory>

#include "session.h"

Listener::Listener(IoThreads& io_threads,
                   const asio::ip::tcp::endpoint& endpoint)
    : acceptor_(io_threads.io_context(), endpoint) {
  spdlog::info("Accepting from {}:{}", endpoint.address().to_string(),
               endpoint.port());
  do_accept();
}

void Listener::do_accept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket))->Start();
        } else {
          spdlog::error("Accept failed: {}", ec.message());
        }
        do_accept();
      });
}
