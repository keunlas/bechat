/**
 * @file server_context.h
 * @author Keunlas
 * @brief 服务器主上下文
 * @date 2026-08-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_CORE_SERVER_CONTEXT_H_
#define BECHAT_CORE_SERVER_CONTEXT_H_

#include "bechat/core/session.h"
#include "bechat/proto/request_result.h"
#include "bechat/utils/io_threads.h"
#include "bechat/utils/types.h"

class ServerContext {
 public:
  ServerContext(IoThreads& io_threads);

 public:
  /// @brief 同步且阻塞的处理 session 收到的 request
  /// @param session std::shared_ptr<Session>
  /// @param request std::shared_ptr<TlvMessage>
  /// @return 处理的结果
  RequestResult HandleRequest(SessionPtr session, TlvMessagePtr request);

  /// @brief 向所有在线的 Session 推送一条消息
  /// @param session std::shared_ptr<Session> 触发广播推送的 session
  /// @param push std::shared_ptr<TlvMessage>
  void Broadcast(SessionPtr session, TlvMessagePtr push);

 private:
  IoThreads& io_threads_;
};

#endif  // !BECHAT_CORE_SERVER_CONTEXT_H_
