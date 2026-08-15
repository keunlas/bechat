/**
 * @file acceptor.h
 * @author Keunlas
 * @brief 用来监听并接受新的 TCP 连接
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_CORE_ACCEPTOR_H_
#define BECHAT_CORE_ACCEPTOR_H_

#include <asio.hpp>
#include <cstdint>

#include "bechat/utils/io_threads.h"

class Acceptor {
 public:
  Acceptor(IoThreads& io_threads, const std::string& ip, uint16_t port);

 private:
  void do_accept();

 private:
  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
};

#endif  // !BECHAT_CORE_ACCEPTOR_H_
