#include "io_threads.h"

void IoThreads::Run() {
  for (int i = 0; i < n_threads_; i += 1) {
    threads_.emplace_back([this]() { io_context_.run(); });
  }
  for (auto&& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
}
