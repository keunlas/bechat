/**
 * @file request_message.cpp
 * @author Keunlas
 * @brief 请求消息相关类
 * @date 2026-09-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/proto/request_message.h"

RequestMessage::RequestMessage(uint16_t tag, std::string value)
    : TlvMessage(tag, std::move(value)) {}

/// @brief 获取 Request ID
uint32_t RequestMessage::request_id() const {
  auto id = PeekRequestId();
  return id.has_value() ? id.value() : 0;
}

/// @brief 获取载荷
std::string_view RequestMessage::payload() const {
  if (length() <= sizeof(uint32_t)) return {};
  return std::string_view(value()).substr(sizeof(uint32_t));
}

/// @brief 尝试获取 Request ID
std::optional<uint32_t> RequestMessage::PeekRequestId() const {
  if (length() < sizeof(uint32_t)) return std::nullopt;
  uint32_t request_id = *reinterpret_cast<const uint32_t*>(value().data());
  if (std::endian::native == std::endian::big) {
    return request_id;
  }
  return std::byteswap(request_id);
}
