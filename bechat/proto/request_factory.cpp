#include "bechat/proto/request_factory.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "bechat/utils/logger.h"

std::expected<RequestParams, uint32_t /* StatusCode */> RequestFactory::Parse(
    uint16_t message_tag, const std::string& message_value,
    uint32_t* /* out */ req_id) {
  using nlohmann::json;
  uint32_t request_id{0};
  try {
    const json val = json::parse(message_value);
    if (val.contains("request_id"))
      request_id = val["request_id"].get<uint32_t>();
    if (req_id) *req_id = request_id;

    switch (message_tag) {
      case BECHAT_TAG_SIGNUP: {
        SignupParams params(request_id, val["username"], val["password"]);
        return RequestParams{std::in_place_type<SignupParams>,
                             std::move(params)};
      } break;
      case BECHAT_TAG_LOGIN: {
        LoginParams params(request_id, val["username"], val["password"]);
        return RequestParams{std::in_place_type<LoginParams>,
                             std::move(params)};
      } break;
      default:
        return std::unexpected{BECHAT_STATUS_UNSUPPORTED_TAG};
        break;
    }

  } catch (const json::exception& e) {  // json err
    TRACE("RequestFactory::Parse json error id {}: {}", e.id, e.what());
    if (req_id) *req_id = request_id;
    return std::unexpected{BECHAT_STATUS_MALFORMED_PAYLOAD};
  } catch (const std::exception& e) {  // other err
    ERROR("RequestFactory::Parse error: {}", e.what());
    if (req_id) *req_id = request_id;
    return std::unexpected{BECHAT_STATUS_INTERNAL_ERROR};
  }
}
