#include <asio.hpp>
#include <iostream>

#include "bechat.h"
#include "io_threads.h"
#include "session.h"

int main(int argc, char* argv[]) {
  try {
    IoThreads io_threads(2);
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 35565);
    BeChat server(io_threads.io_context(), endpoint);
    io_threads.Run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }
  return 0;
}
