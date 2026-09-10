#include "bechat/core/server_context.h"

#include <utility>

ServerContexts::ServerContexts(IoContexts& io_context)
    : io_context_(io_context) {}

void ServerContexts::OnSessionMessage(
    const std::weak_ptr<SessionHandle>& session_handle, std::string message) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO] 暂时使用 ECHO 逻辑，后续在这里解析消息并分发请求
  session->Send(std::move(message));
}

void ServerContexts::OnSessionClose(
    const std::weak_ptr<SessionHandle>& session_handle) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO] 暂时不需要处理 Session 关闭，后续可以在这里清理该 Session 的数据
  //        （比如在线用户列表、订阅关系等）
}
