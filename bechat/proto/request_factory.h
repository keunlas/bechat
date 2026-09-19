#if !defined(BECHAT_PROTO_REQUEST_FACTORY_H_)
#define BECHAT_PROTO_REQUEST_FACTORY_H_

#include <cstdint>
#include <expected>
#include <string>

#include "bechat/proto/message_tag.h"
#include "bechat/proto/request_params.h"
#include "bechat/proto/status_code.h"

class RequestFactory {
 public:
  static std::expected<RequestParams, uint32_t /* StatusCode */> Parse(
      uint16_t message_tag, const std::string& message_value,
      uint32_t* /* out */ req_id = nullptr);
};

#endif  // BECHAT_PROTO_REQUEST_FACTORY_H_
