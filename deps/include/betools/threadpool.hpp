// Distributed under the MIT License that can be found in the LICENSE file.
// https://github.com/keunlas/betools
//
// Author: Keunlas <keunlaz at gmail dot com>

#ifndef KEUNLAS_BETOOLS_THREADPOOL_HPP_
#define KEUNLAS_BETOOLS_THREADPOOL_HPP_

/**
 * @file threadpool.hpp
 * @author Keunlas (keunlaz at gmail dot com)
 * @brief 本头文件包含线程池实现，
 * 依赖 betools/lock_based_queue.hpp，
 * 复制时应当一并复制。
 * @version 1.0.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "betools/lock_based_queue.hpp"

namespace betools {

class ThreadPool {
 public:
  /**
   * @brief 线程池退出策略枚举
   *
   * 决定析构或显式关闭时如何处理队列中尚未完成的任务。
   */
  enum class ExitFlag : uint8_t {
    TASKS_DROP, /**< 放弃所有未完成的任务并立即退出 */
    TASKS_DONE  /**< 执行未完成的任务直至队列清空后再退出 */
  };

 public:
  /**
   * @brief 构造线程池，创建指定数量的工作线程和内部任务队列
   *
   * @param num_threads 工作线程数量
   * @param queue_size 内部任务队列容量
   */
  ThreadPool(size_t num_threads, size_t queue_size,
             ExitFlag exit_flag = ExitFlag::TASKS_DONE)
      : task_queue_(queue_size) {
    using std::chrono::milliseconds;
    for (size_t i = 0; i < num_threads; ++i) {
      workers_.emplace_back([this, exit_flag] {
        while (stop_.load() == false) {
          std::function<void()> task;
          bool not_timeout = task_queue_.TryDequeueFor(milliseconds(10), task);
          if (not_timeout) task();
        }
        if (exit_flag != ExitFlag::TASKS_DROP) {
          std::function<void()> task;
          while (task_queue_.TryDequeue(task)) task();
        }
      });
    }
  }

  /**
   * @brief 析构，等待所有任务完成后关闭线程池
   */
  ~ThreadPool() {
    stop_.store(true, std::memory_order_release);
    // 等待线程退出
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  /**
   * @brief 禁止拷贝构造
   */
  ThreadPool(const ThreadPool&) = delete;

  /**
   * @brief 禁止拷贝赋值
   */
  ThreadPool& operator=(const ThreadPool&) = delete;

  /**
   * @brief 禁止移动构造
   */
  ThreadPool(ThreadPool&&) = delete;

  /**
   * @brief 禁止移动赋值
   */
  ThreadPool& operator=(ThreadPool&&) = delete;

  /**
   * @brief 非阻塞地向任务队列中添加一个任务
   *
   * @param task 封装好的可调用对象 std::function<void()>
   * @return true  添加成功
   * @return false 队列已满，添加失败
   */
  bool AddTask(std::function<void()> task) {
    // 线程池停止后不再添加任何任务
    if (stop_) return false;
    return task_queue_.TryEnqueue(std::move(task));
  }

  /**
   * @brief 向线程池提交一个可调用对象及其参数，返回对应的 std::future
   *
   * @tparam F    可调用对象类型
   * @tparam Args 参数类型包
   * @param func  要执行的可调用对象
   * @param args  转发给可调用对象的参数
   * @return std::future<return_type> 用于获取异步执行结果；
   *         若队列已满导致提交失败，返回默认构造的空 future（valid() == false）
   */
  template <typename F, typename... Args>
  auto SubmitTask(F&& func, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    // 函数 func(args...) 的返回值类型
    using return_type = std::invoke_result_t<F, Args...>;
    // 线程池停止后不再添加任何任务
    if (stop_) return std::future<return_type>();
    // 从 std::bind 封装 std::packaged_task 对象
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<F>(func),
         ... args = std::forward<Args>(args)]() mutable {
          return std::invoke(std::forward<F>(func),
                             std::forward<Args>(args)...);
        });
    // 获取 std::packaged_task 返回值的 std::future 对象
    std::future<return_type> res = task->get_future();
    // 封装 std::packaged_task 为 std::function<void()> 并添加入任务队列
    bool success = AddTask([task]() { (*task)(); });
    if (!success) {
      // 默认构造的 std::future 的 valid() 接口返回 false
      res = std::future<return_type>();
    }
    return res;
  }

 private:
  LockBasedQueue<std::function<void()>> task_queue_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
};

}  // namespace betools

#endif  // !KEUNLAS_BETOOLS_THREADPOOL_HPP_
