#ifndef BECHAT_BECHAT_H_
#define BECHAT_BECHAT_H_

#include <asio.hpp>

#include "io_threads.h"

class BeChat {
 public:
  BeChat(asio::io_context& io_context, const asio::ip::tcp::endpoint& endpoint);

 private:
  void do_accept();

 private:
  asio::ip::tcp::acceptor acceptor_;
};

#endif  // !BECHAT_BECHAT_H_
