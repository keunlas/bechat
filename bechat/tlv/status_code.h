/**
 * @file status_code.h
 * @author Keunlas
 * @brief 返回状态码的枚举值
 * @date 2026-08-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_STATUS_CODE_H_
#define BECHAT_TLV_STATUS_CODE_H_

#include <cstdint>

struct StatusCode {
  enum Type : uint16_t {
    Ok = 0x0000,               // 成功
    MalformedPacket = 0x0001,  // 协议错误
    InvalidParameter,          // 参数错误
    NotLoggedIn,               // 未登录
    UsernameAlreadyExists,     // 用户名已存在
    InvalidCredentials,        // 凭据错误
    InvalidState,              // 状态错误
    InternalError = 0xFFFF,    // 服务端内部错误
  };
};

#endif  // !BECHAT_TLV_STATUS_CODE_H_
