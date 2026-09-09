#include "bechat/core/io_contexts.h"

#include "bechat/utils/logger.h"

IoContexts::IoContexts(int n_threads)
    : n_threads_(n_threads > 0 ? n_threads : 1) {
  // 目前只有一个 asio::io_context
  // 以后可能有多个 asio::io_context
  io_contexts_.emplace_back();
}

void IoContexts::Run() {
  INFO("IO Threads has running with {} threads", n_threads_);

  for (int i = 0; i < n_threads_; i += 1) {
    threads_.emplace_back([this]() {
      // 目前只有一个 asio::io_context
      io_contexts_.front().run();
    });
  }

  for (auto&& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
}

asio::io_context& IoContexts::GetIoContext() {
  // 目前只有一个 asio::io_context
  return io_contexts_.front();
}
