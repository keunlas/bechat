// Distributed under the MIT License that can be found in the LICENSE file.
// https://github.com/keunlas/betools
//
// Author: Keunlas <keunlaz at gmail dot com>

#ifndef KEUNLAS_BETOOLS_SINGLETON_HPP_
#define KEUNLAS_BETOOLS_SINGLETON_HPP_

/**
 * @file singleton.hpp
 * @author Keunlas (keunlaz at gmail dot com)
 * @brief 本头文件包含单例模式模板实现，
 * 这个文件是 header-only 且 self-contained 的，
 * 可以随便复制到任何路径下直接进行使用。
 * @date 2026-06-11
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <utility>

namespace betools {

/**
 * @brief 单例持有者模板，为任意类型 T 提供全局唯一实例
 *
 * @tparam T   需要作为单例管理的类型
 * @tparam Tag 用于区分同类型不同用途的标签类型，缺省为 `void`
 *
 * @note 不同的 Tag 会返回不同的全局唯一实例
 * ```
 * struct Tool {};         // This struct that you need a singleton.
 * struct MyToolTag {};    // for my Tool's singleton
 * struct YourToolTag {};  // for your Tool's singleton
 *
 * // my_tool 和 your_tool 是两个完全不同的类型 Tool 的全局唯一实例
 * auto my_tool = Singleton<Tool, MyTool>::Instance();
 * auto your_tool = Singleton<Tool, YourToolTag>::Instance();
 * ```
 *
 */
template <typename T, typename Tag = void>
class Singleton {
 public:
  /**
   * @brief 获取 T 的全局唯一实例
   *
   * @tparam Args 构造 T 所需的参数类型
   * @param args  转发给 T 构造函数的参数
   * @return T& 全局唯一实例的引用
   *
   * @note 1. 若 T 的构造函数抛出异常，下一次调用将重新尝试构造。
   * @note 2. 首次调用时以 `args...` 构造类型 T 的全局唯一实例，
   * 后续调用同类型 `args...` 时直接返回已构造的实例。
   * @note 3. 不同类型的 `args...` 会构造新的全局唯一实例,
   * 例如 Instance(std::string("hello")) 与 Instance("hello")
   * 将会返回不同的全局唯一实例。
   */
  template <typename... Args>
  static inline T& Instance(Args&&... args) {
    static T instance{std::forward<Args>(args)...};
    return instance;
  }

  /// @brief 禁止构造
  Singleton() = delete;
  /// @brief 禁止析构
  ~Singleton() = delete;
  /// @brief 禁止拷贝构造
  Singleton(const Singleton&) = delete;
  /// @brief 禁止拷贝赋值
  Singleton& operator=(const Singleton&) = delete;
  /// @brief 禁止移动构造
  Singleton(Singleton&&) = delete;
  /// @brief 禁止移动赋值
  Singleton& operator=(Singleton&&) = delete;
};

}  // namespace betools

#endif  // !KEUNLAS_BETOOLS_SINGLETON_HPP_
