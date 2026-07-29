#ifndef BECHAT_IO_THREADS_H_
#define BECHAT_IO_THREADS_H_

#include <asio.hpp>
#include <thread>
#include <vector>

class IoThreads {
 public:
  IoThreads(int n_threads) : n_threads_(n_threads > 0 ? n_threads : 1) {}

  void Run();

 public:
  inline asio::io_context& io_context() { return io_context_; }

 private:
  asio::io_context io_context_{};
  std::vector<std::thread> threads_{};
  int n_threads_;
};

#endif  // !BECHAT_IO_THREADS_H_
