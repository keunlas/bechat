#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <iostream>

#include "io_threads.h"
#include "listener.h"

int main(int argc, char* argv[]) {
  try {
    IoThreads io_threads(2);
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 35565);
    Listener listen(io_threads, endpoint);
    spdlog::set_level(spdlog::level::trace);
    io_threads.Run();
  } catch (std::exception& e) {
    spdlog::error("Exception: {}", e.what());
  }
  return 0;
}
