#ifndef BECHAT_CORE_SERVER_H_
#define BECHAT_CORE_SERVER_H_

#include <asio.hpp>
#include <cstdint>
#include <string>

#include "bechat/core/io_contexts.h"
#include "bechat/core/server_context.h"
#include "bechat/core/session.h"
#include "bechat/utils/logger.h"

class Server {
 public:
  Server(IoContexts& io_contexts, ServerContexts& server_contexts,
         const std::string& ip, uint16_t port);

 private:
  void start_accept();

 private:
  IoContexts& io_contexts_;
  ServerContexts& server_contexts_;
  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
};

#endif  // !BECHAT_CORE_SERVER_H_
