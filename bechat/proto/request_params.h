#if !defined(BECHAT_PROTO_REQUEST_PARAMS_H_)
#define BECHAT_PROTO_REQUEST_PARAMS_H_

#include <cstdint>
#include <string>
#include <variant>

struct SignupParams {
  uint32_t request_id;
  std::string username;
  std::string password;

  SignupParams(uint32_t req_id, std::string uname, std::string passwd)
      : request_id(req_id),
        username(std::move(uname)),
        password(std::move(passwd)) {}
};

struct LoginParams {
  uint32_t request_id;
  std::string username;
  std::string password;

  LoginParams(uint32_t req_id, std::string uname, std::string passwd)
      : request_id(req_id),
        username(std::move(uname)),
        password(std::move(passwd)) {}
};

using RequestParams = std::variant<               /* 请求参数的各种变体 */
                                   SignupParams,  // 注册参数
                                   LoginParams    // 登录参数
                                   >;

#endif  // BECHAT_PROTO_REQUEST_PARAMS_H_
