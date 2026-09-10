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
