/**
 * @file acceptor.cpp
 * @author Keunlas
 * @brief 用来监听并接受新的 TCP 连接
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/core/acceptor.h"

#include "bechat/core/session.h"
#include "bechat/utils/logger.h"

Acceptor::Acceptor(IoThreads& io_threads, const std::string& ip, uint16_t port)
    : endpoint_(asio::ip::make_address(ip), port),
      acceptor_(io_threads.GetIoContext(), endpoint_) {
  INFO("Acceptor is listening in {}:{}", ip, port);

  do_accept();
}

void Acceptor::do_accept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket))->Start();
        } else {
          ERROR("Acceptor error occurred: {}", ec.message());
        }
        do_accept();
      });
}
