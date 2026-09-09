#ifndef BECHAT_CORE_SSL_SERVER_H_
#define BECHAT_CORE_SSL_SERVER_H_

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <cstdint>
#include <string>

#include "bechat/core/io_contexts.h"
#include "bechat/core/session.h"
#include "bechat/utils/logger.h"

class SslServer {
 public:
  SslServer(IoContexts& io_contexts, const std::string& ip, uint16_t port);

 private:
  void ssl_configure();

  void start_accept();

 private:
  IoContexts& io_contexts_;
  asio::ssl::context ssl_context_;
  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
};

#endif  // !BECHAT_CORE_SSL_SERVER_H_
