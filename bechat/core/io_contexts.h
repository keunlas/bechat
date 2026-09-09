#ifndef BECHAT_CORE_IO_CONTEXT_H_
#define BECHAT_CORE_IO_CONTEXT_H_

#include <asio.hpp>
#include <list>
#include <thread>

class IoContexts {
 public:
  /**
   * @brief 构造 IoContexts 对象
   *
   * @param n_threads 线程数
   */
  IoContexts(int n_threads = 1);

  /**
   * @brief 让 IoContexts 运行起来
   *
   */
  void Run();

  /**
   * @brief 获取一个 asio::io_context 对象的引用
   *
   * @return asio::io_context&
   */
  asio::io_context& GetIoContext();

 private:
  int n_threads_;
  std::vector<std::thread> threads_{};
  std::list<asio::io_context> io_contexts_{};
};

#endif  // !BECHAT_CORE_IO_CONTEXT_H_
