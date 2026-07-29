#include "bechat.h"

#include <memory>

#include "session.h"

BeChat::BeChat(asio::io_context& io_context,
               const asio::ip::tcp::endpoint& endpoint)
    : acceptor_(io_context, endpoint) {
  do_accept();
}

void BeChat::do_accept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket))->Start();
        }
        do_accept();
      });
}
