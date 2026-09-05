/**
 * @file request_message.h
 * @author Keunlas
 * @brief 请求消息相关类
 * @date 2026-09-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_PROTO_REQUEST_MESSAGE_H_
#define BECHAT_PROTO_REQUEST_MESSAGE_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "bechat/tlv/tlv_message.h"
#include "bechat/utils/types.h"

class RequestMessage : public TlvMessage {
 public:
  RequestMessage(uint16_t tag, std::string value);

  ~RequestMessage() = default;

 public:
  /// @brief 获取 Request ID
  /// @return 如果 Request ID 存在则返回它的值，否则返回 0
  inline uint32_t request_id() const { return request_id_.value_or(0); };

  /// @brief 获取 Request ID 是否存在
  inline bool exist_request_id() const { return request_id_.has_value(); };

  /// @brief 获取载荷
  /// @return 排除掉 Request ID 后的载荷
  std::string_view payload() const;

 private:
  /// @brief 尝试获取 Request ID
  /// @return 如果 Request ID 存在则返回它的 opt 值，否则返回 std::nullopt
  std::optional<uint32_t> peek_request_id() const;

 private:
  std::optional<uint32_t> request_id_;
};

#endif  // !BECHAT_PROTO_REQUEST_MESSAGE_H_
