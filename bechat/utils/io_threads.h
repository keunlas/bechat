/**
 * @file io_threads.h
 * @author Keunlas
 * @brief 对 asio::io_context 的封装
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_UTILS_IO_THREADS_H_
#define BECHAT_UTILS_IO_THREADS_H_

#include <asio.hpp>
#include <thread>
#include <vector>

class IoThreads {
 public:
  IoThreads(int n_threads = 1);

  void Run();

  asio::io_context& GetIoContext();

 private:
  std::vector<std::unique_ptr<asio::io_context>> io_contexts_{};
  std::vector<std::thread> threads_{};
  int n_threads_;
};

#endif  // !BECHAT_UTILS_IO_THREADS_H_
