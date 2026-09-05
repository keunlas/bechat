#ifndef BECHAT_PROTO_REQUEST_PARAMS_H_
#define BECHAT_PROTO_REQUEST_PARAMS_H_

#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>

struct RegisterParams {
  std::string_view username;
  std::string_view password;
};

struct LoginParams {
  std::string_view username;
  std::string_view password;
};

struct GetHistoryParams {
  uint64_t before_seq;
  uint16_t limit;
};

struct GetOnlineUsersParams {
  // 无参数字段
};

struct SendMessageParams {
  std::string_view content;
};

using RequestParams = std::variant<RegisterParams,        // 0
                                   LoginParams,           // 1
                                   GetHistoryParams,      // 2
                                   GetOnlineUsersParams,  // 3
                                   SendMessageParams      // 4
                                   >;

#endif  // !BECHAT_PROTO_REQUEST_PARAMS_H_
