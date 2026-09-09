#include "bechat/core/io_contexts.h"
#include "bechat/core/server.h"
#include "bechat/core/ssl_server.h"
#include "bechat/utils/config.h"
#include "bechat/utils/logger.h"

int main(int argc, char* argv[]) {
  try {
    IoContexts io_contexts(CFG_SERVER_IO_THREADS);
    Server server(io_contexts, CFG_SERVER_IP, CFG_SERVER_PORT);
    SslServer ssl_server(io_contexts, CFG_SERVER_IP, CFG_SERVER_PORT + 1);
    io_contexts.Run();
  } catch (std::exception& e) {
    CRITICAL("Exception ocurred in main function: {}", e.what());
  }
  return 0;
}
