#ifndef BECHAT_LISTENER_H_
#define BECHAT_LISTENER_H_

#include <asio.hpp>

#include "io_threads.h"

class Listener {
 public:
  Listener(IoThreads& io_threads, const asio::ip::tcp::endpoint& endpoint);

 private:
  void do_accept();

 private:
  asio::ip::tcp::acceptor acceptor_;
};

#endif  // !BECHAT_LISTENER_H_
