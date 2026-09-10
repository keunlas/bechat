#ifndef BECHAT_CORE_SESSION_HANDLE_H_
#define BECHAT_CORE_SESSION_HANDLE_H_

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Session 对外提供的接口
 *
 * Session 实现该接口，并把自身以句柄的形式交给上层（比如 ServerContexts）；
 * 上层持有该句柄就能操作对应的 Session，不需要关心 Session 的类型，
 * 也不需要关心这些操作具体是怎么完成的。
 *
 * 后续 Session 需要对外提供新的能力时，在这里增加接口即可。
 *
 */
class SessionHandle {
 public:
  virtual ~SessionHandle() = default;

  /**
   * @brief 向对端发送一条数据
   *
   * 线程安全，可以在任意线程调用；数据会按调用顺序排队，依次发送。
   * Session 关闭后调用不会发送任何数据。
   *
   * @param message 待发送的数据
   */
  virtual void Send(std::string message) = 0;

  /**
   * @brief 一次向对端发送多条数据
   *
   * 比多次调用 Send 少一次线程切换，并且这批数据会连续排队，按顺序发送，
   * 不会被别的线程发送的数据插到中间。
   *
   * 线程安全，可以在任意线程调用；数据会按调用顺序排队，依次发送。
   * Session 关闭后调用不会发送任何数据。
   *
   * @param messages 待发送的数据，按顺序发送
   */
  virtual void SendBatch(std::vector<std::string> messages) = 0;

  /**
   * @brief 获取该 Session 的 id
   *
   * id 在 Session 创建时分配，原则上唯一，可以用来区分不同的 Session，
   * 比如打日志或者给容器当 key。
   *
   * @return uint64_t
   */
  virtual uint64_t Id() const = 0;
};

#endif  // !BECHAT_CORE_SESSION_HANDLE_H_
