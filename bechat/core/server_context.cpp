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

  asio::post(io_context_.GetIoContext(), [this, session, tag = message_tag,
                                          value = std::move(message_value)]() {
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
                   [&](RefreshParams p) { handle_refresh(session, p); },
               },
               res.value());
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
  asio::post(
      io_context_.GetIoContext(), [this, session, p = std::move(params)]() {
        try {
          nlohmann::json jvalue = nlohmann::json::object();
          jvalue["request_id"] = p.request_id;
          jvalue["status_code"] = user_registry_.Signup(p.username, p.password);
          auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_SIGNUP, jvalue);
          session->Send(std::move(resp));
        } catch (const std::exception& e) {
          ERROR("ServerContexts::handle_signup error: {}", e.what());
          session->Send(ResponseFactory::MakeError(
              BECHAT_TAG_SIGNUP, p.request_id, BECHAT_STATUS_INTERNAL_ERROR));
        }
      });
}

void ServerContexts::handle_login(std::shared_ptr<SessionHandle> session,
                                  LoginParams params) {
  asio::post(io_context_.GetIoContext(), [this, session,
                                          p = std::move(params)]() {
    try {
      std::pair<std::string, std::string> tokens{};
      uint32_t status_code =
          session->IsAuthorized()
              ? BECHAT_STATUS_ALREADY_LOGIN
              : user_registry_.Login(p.username, p.password, session, &tokens);

      nlohmann::json jvalue = nlohmann::json::object();
      jvalue["request_id"] = p.request_id;
      jvalue["status_code"] = status_code;
      if (status_code == BECHAT_STATUS_SUCCESS) {
        jvalue["refresh_token"] = std::move(tokens.first);
        jvalue["access_token"] = std::move(tokens.second);
      }

      auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_LOGIN, jvalue);
      session->Send(std::move(resp));
    } catch (const std::exception& e) {
      ERROR("ServerContexts::handle_login error: {}", e.what());
      session->Send(ResponseFactory::MakeError(BECHAT_TAG_LOGIN, p.request_id,
                                               BECHAT_STATUS_INTERNAL_ERROR));
    }
  });
}

void ServerContexts::handle_refresh(std::shared_ptr<SessionHandle> session,
                                    RefreshParams params) {
  asio::post(io_context_.GetIoContext(), [this, session,
                                          p = std::move(params)]() {
    try {
      std::string access_token{};  // new access token
      uint32_t status_code =
          session->IsAuthorized()
              ? user_registry_.Refresh(session, p.refresh_token, &access_token)
              : BECHAT_STATUS_NOT_LOGIN;

      nlohmann::json jvalue = nlohmann::json::object();
      jvalue["request_id"] = p.request_id;
      jvalue["status_code"] = status_code;
      if (status_code == BECHAT_STATUS_SUCCESS) {
        jvalue["access_token"] = std::move(access_token);
      }

      auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_REFRESH, jvalue);
      session->Send(std::move(resp));
    } catch (const std::exception& e) {
      ERROR("ServerContexts::handle_refresh error: {}", e.what());
      session->Send(ResponseFactory::MakeError(BECHAT_TAG_REFRESH, p.request_id,
                                               BECHAT_STATUS_INTERNAL_ERROR));
    }
  });
}
