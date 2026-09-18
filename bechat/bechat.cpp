#include "bechat/core/io_contexts.h"
#include "bechat/core/server.h"
#include "bechat/core/server_context.h"
#include "bechat/core/ssl_server.h"
#include "bechat/utils/config.h"
#include "bechat/utils/global.h"
#include "bechat/utils/logger.h"

int main(int argc, char* argv[]) {
  try {
    Global::Init();
    IoContexts io_contexts(CFG_SERVER_IO_THREADS);
    ServerContexts server_context(io_contexts);
    Server server(io_contexts, server_context, CFG_SERVER_IP, CFG_SERVER_PORT);
    SslServer ssl_server(io_contexts, server_context, CFG_SERVER_IP, CFG_SERVER_SSL_PORT);
    io_contexts.Run();
  } catch (std::exception& e) {
    CRITICAL("Exception ocurred in main function: {}", e.what());
  }
  return 0;
}
