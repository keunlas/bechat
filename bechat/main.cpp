#include <asio.hpp>
#include <iostream>

#include "io_threads.h"
#include "listener.h"
#include "logger.h"

int main(int argc, char* argv[]) {
  try {
    IoThreads io_threads(2);
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 35565);
    Listener listen(io_threads, endpoint);
    io_threads.Run();
  } catch (std::exception& e) {
    Log::Error("Exception: {}", e.what());
  }
  return 0;
}
