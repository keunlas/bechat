#include "bechat/core/server.h"

Server::Server(IoContexts& io_contexts, ServerContexts& server_contexts,
               const std::string& ip, uint16_t port)
    : io_contexts_(io_contexts),
      server_contexts_(server_contexts),
      endpoint_(asio::ip::make_address(ip), port),
      acceptor_(io_contexts_.GetIoContext(), endpoint_) {
  INFO("Server has running in {}:{}", ip, port);
  start_accept();
}

void Server::start_accept() {
  acceptor_.async_accept(
      [this](const std::error_code& error, asio::ip::tcp::socket socket) {
        if (!error) {
          std::make_shared<NosslSession>(server_contexts_, std::move(socket))
              ->Start();
        } else {
          ERROR("Server accept error occurred: {}", error.message());
        }
        start_accept();
      });
}
