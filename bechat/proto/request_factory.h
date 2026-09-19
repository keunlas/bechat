#if !defined(BECHAT_PROTO_REQUEST_FACTORY_H_)
#define BECHAT_PROTO_REQUEST_FACTORY_H_

#include <cstdint>
#include <expected>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "bechat/proto/message_tag.h"
#include "bechat/proto/request_params.h"
#include "bechat/proto/status_code.h"
#include "bechat/utils/logger.h"

class RequestFactory {
 public:
  std::expected<RequestParams, int /* StatusCode */> Parse(
      uint16_t message_tag, const std::string& message_value) {
    using nlohmann::json;

    try {
      const json val = nlohmann::json::parse(message_value);

      switch (message_tag) {
        case BECHAT_TAG_SIGNUP: {
          SignupParams params(val["username"], val["password"]);
          return RequestParams{std::in_place_type<SignupParams>,
                               std::move(params)};
        } break;
        case BECHAT_TAG_LOGIN: {
          LoginParams params(val["username"], val["password"]);
          return RequestParams{std::in_place_type<LoginParams>,
                               std::move(params)};
        } break;
        default:
          return std::unexpected{BECHAT_STATUS_UNSUPPORTED_TAG};
          break;
      }

    } catch (const json::exception& e) {  // json err
      TRACE("RequestFactory::Parse json error id {}: {}", e.id, e.what());
      return std::unexpected{BECHAT_STATUS_MALFORMED_PAYLOAD};
    } catch (const std::exception& e) {  // other err
      ERROR("RequestFactory::Parse error: {}", e.what());
      return std::unexpected{BECHAT_STATUS_INTERNAL_ERROR};
    }
  }
};

#endif  // BECHAT_PROTO_REQUEST_FACTORY_H_
