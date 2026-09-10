#ifndef BECHAT_CORE_SERVER_CONTEXT_H_
#define BECHAT_CORE_SERVER_CONTEXT_H_

#include <asio.hpp>
#include <memory>
#include <string>

#include "bechat/core/io_contexts.h"
#include "bechat/core/session_handle.h"

class ServerContexts {
 public:
  ServerContexts(IoContexts& io_context);

  /**
   * @brief 处理 Session 收到的数据
   *
   * @param sender 收到数据的 Session，可以通过它向该 Session 发送数据
   * @param message Session 收到的数据
   */
  void OnSessionMessage(const std::weak_ptr<SessionHandle>& session_handle,
                        std::string message);

  /**
   * @brief 处理 Session 关闭
   *
   * @param session_handle 已经关闭的 Session，此时它已经不能发送数据
   */
  void OnSessionClose(const std::weak_ptr<SessionHandle>& session_handle);

 private:
  IoContexts& io_context_;
};

#endif  // !BECHAT_CORE_SERVER_CONTEXT_H_
