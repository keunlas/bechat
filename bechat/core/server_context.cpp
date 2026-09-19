#include "bechat/core/server_context.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "bechat/proto/message_tag.h"
#include "bechat/proto/request_factory.h"
#include "bechat/proto/response_factory.h"
#include "bechat/proto/status_code.h"
#include "bechat/utils/logger.h"

template <typename... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

ServerContexts::ServerContexts(IoContexts& io_context)
    : io_context_(io_context) {}

void ServerContexts::OnSessionMessage(
    const std::weak_ptr<SessionHandle>& session_handle, uint16_t message_tag,
    std::string message_value) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO]
  asio::post(io_context_.GetIoContext(), [this, session, tag = message_tag,
                                          value = std::move(message_value)]() {
    // switch (message_tag) {
    //   case BECHAT_TAG_SIGNUP: {
    //     nlohmann::json value = nlohmann::json::parse(message_value);
    //     auto ret = user_registry_.Signup(value["username"],
    //     value["password"]); if (ret == BECHAT_STATUS_SUCCESS) {
    //       std::string str = {'\x00', '\x01', '\x00', '\x02', 'O', 'K'};
    //       session->Send(std::move(str));
    //     } else {
    //       std::string str = {'\x00', '\x01', '\x00', '\x02', 'N', 'O'};
    //       session->Send(std::move(str));
    //     }
    //   } break;
    //   case BECHAT_TAG_LOGIN: {
    //     nlohmann::json value = nlohmann::json::parse(message_value);
    //     auto ret = user_registry_.Verify(value["username"],
    //     value["password"]); if (ret == BECHAT_STATUS_SUCCESS) {
    //       std::string str = {'\x00', '\x02', '\x00', '\x02', 'O', 'K'};
    //       session->Send(std::move(str));
    //     } else {
    //       std::string str = {'\x00', '\x02', '\x00', '\x02', 'N', 'O'};
    //       session->Send(std::move(str));
    //     }
    //   } break;
    //   default:
    //     session->Send(std::move(message_value));
    //     break;
    // }
    uint32_t request_id{0};
    auto res = RequestFactory::Parse(tag, value, &request_id);

    if (!res) {
      uint32_t status_code = res.error();
      auto error = ResponseFactory::MakeError(tag, request_id, status_code);
      session->Send(std::move(error));
      return;
    }

    std::visit(Overloaded{
                   [&](SignupParams p) { handle_signup(session, p); },
                   [&](LoginParams p) { handle_login(session, p); },
               },
               *res);
  });
}

void ServerContexts::OnSessionClose(
    const std::weak_ptr<SessionHandle>& session_handle) {
  auto session = session_handle.lock();
  if (!session) return;

  // [TODO] 暂时不需要处理 Session 关闭，后续可以在这里清理该 Session 的数据
  //        （比如在线用户列表、订阅关系等）
}

void ServerContexts::handle_signup(std::shared_ptr<SessionHandle> session,
                                   SignupParams params) {
  asio::post(io_context_.GetIoContext(), [this, session,
                                          params = std::move(params)]() {
    try {
      uint32_t status_code =
          user_registry_.Signup(params.username, params.password);

      nlohmann::json jvalue = nlohmann::json::object();
      jvalue["request_id"] = params.request_id;
      jvalue["status_code"] = status_code;
      std::string value{jvalue.dump()};

      auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_SIGNUP, value);
      session->Send(std::move(resp));

    } catch (const std::exception& e) {
      ERROR("ServerContexts::handle_signup error: {}", e.what());
      session->Send(ResponseFactory::MakeError(
          BECHAT_TAG_SIGNUP, params.request_id, BECHAT_STATUS_INTERNAL_ERROR));
    }
  });
}

void ServerContexts::handle_login(std::shared_ptr<SessionHandle> session,
                                  LoginParams params) {
  // auto status = user_registry_.Signup(params.username, params.password);
  // session->Send(MakeResponseFrame(request_tag, status));
  asio::post(io_context_.GetIoContext(), [this, session,
                                          params = std::move(params)]() {
    try {
      uint32_t status_code =
          user_registry_.Verify(params.username, params.password);

      nlohmann::json jvalue = nlohmann::json::object();
      jvalue["request_id"] = params.request_id;
      jvalue["status_code"] = status_code;
      std::string value{jvalue.dump()};

      auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_LOGIN, value);
      session->Send(std::move(resp));

    } catch (const std::exception& e) {
      ERROR("ServerContexts::handle_signup error: {}", e.what());
      session->Send(ResponseFactory::MakeError(
          BECHAT_TAG_LOGIN, params.request_id, BECHAT_STATUS_INTERNAL_ERROR));
    }
  });
}
