/**
 * @file io_threads.cpp
 * @author Keunlas
 * @brief 对 asio::io_context 的封装
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/utils/io_threads.h"

#include "bechat/utils/logger.h"

IoThreads::IoThreads(int n_threads)
    : n_threads_(n_threads > 0 ? n_threads : 1) {
  // 目前只有一个 asio::io_context
  // 以后可能有多个 asio::io_context
  io_contexts_.emplace_back(std::make_unique<asio::io_context>());
}

void IoThreads::Run() {
  INFO("IO Threads has running with {} threads.", n_threads_);

  for (int i = 0; i < n_threads_; i += 1) {
    threads_.emplace_back([this]() {
      // 目前只有一个 asio::io_context
      // 以后可能有多个 asio::io_context
      // 比如每个线程都有自己的 asio::io_context
      io_contexts_.front()->run();
    });
  }

  for (auto&& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
}

asio::io_context& IoThreads::GetIoContext() {
  // 目前只有一个 asio::io_context
  // 以后可能会有其他的方法去返回 asio::io_context
  // 比如每个线程都有一个 asio::io_context
  return *io_contexts_.front();
}
