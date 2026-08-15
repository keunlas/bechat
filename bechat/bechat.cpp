#include "bechat/core/acceptor.h"
#include "bechat/utils/io_threads.h"
#include "bechat/utils/logger.h"

int main(int argc, char* argv[]) {
  try {
    IoThreads io_threads(2);
    Acceptor acceptor(io_threads, "127.0.0.1", 35565);
    io_threads.Run();
  } catch (std::exception& e) {
    CRITICAL("Exception ocurred in main function: {}", e.what());
  }
  return 0;
}
