#include "io_threads.h"

#include "logger.h"

void IoThreads::Run() {
  Log::Info("IO Threads has running with {} threads.", n_threads_);
  for (int i = 0; i < n_threads_; i += 1) {
    threads_.emplace_back([this]() { io_context_.run(); });
  }
  for (auto&& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
}
