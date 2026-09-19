#include "bechat/core/server_context.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "bechat/proto/message_tag.h"
#include "bechat/proto/status_code.h"

ServerContexts::ServerContexts(IoContexts& io_context)
    : io_context_(io_context) {}

void ServerContexts::OnSessionMessage(
    const std::weak_ptr<SessionHandle>& session_handle, uint16_t message_tag,
    std::string message_value) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO]
  asio::post(io_context_.GetIoContext(), [this, session, message_tag,
                                          message_value =
                                              std::move(message_value)]() {
    switch (message_tag) {
      case BECHAT_TAG_SIGNUP: {
        nlohmann::json value = nlohmann::json::parse(message_value);
        auto ret = user_registry_.Signup(value["username"], value["password"]);
        if (ret == BECHAT_STATUS_SUCCESS) {
          std::string str = {'\x00', '\x01', '\x00', '\x02', 'O', 'K'};
          session->Send(std::move(str));
        } else {
          std::string str = {'\x00', '\x01', '\x00', '\x02', 'N', 'O'};
          session->Send(std::move(str));
        }
      } break;
      case BECHAT_TAG_LOGIN: {
        nlohmann::json value = nlohmann::json::parse(message_value);
        auto ret = user_registry_.Verify(value["username"], value["password"]);
        if (ret == BECHAT_STATUS_SUCCESS) {
          std::string str = {'\x00', '\x02', '\x00', '\x02', 'O', 'K'};
          session->Send(std::move(str));
        } else {
          std::string str = {'\x00', '\x02', '\x00', '\x02', 'N', 'O'};
          session->Send(std::move(str));
        }
      } break;
      default:
        session->Send(std::move(message_value));
        break;
    }
  });
}

void ServerContexts::OnSessionClose(
    const std::weak_ptr<SessionHandle>& session_handle) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO] 暂时不需要处理 Session 关闭，后续可以在这里清理该 Session 的数据
  //        （比如在线用户列表、订阅关系等）
}
