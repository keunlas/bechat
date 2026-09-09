#ifndef BECHAT_CORE_SERVER_CONTEXT_H_
#define BECHAT_CORE_SERVER_CONTEXT_H_

#include <asio.hpp>

#include "bechat/core/io_contexts.h"

class ServerContexts {
 public:
  ServerContexts(IoContexts& io_context);

 private:
  IoContexts& io_context_;
};

#endif  // !BECHAT_CORE_SERVER_CONTEXT_H_
