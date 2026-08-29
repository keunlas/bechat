/**
 * @file tlv_codec.cpp
 * @author Keunlas
 * @brief 与 TlvMessage 相关的编解码操作
 * @date 2026-08-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/tlv/tlv_codec.h"

#include <bit>
#include <cassert>

std::optional<uint32_t> TlvCodec::PeekRequestId(const std::string& value) {
  if (value.size() < sizeof(uint32_t)) return std::nullopt;
  uint32_t request_id_be = *reinterpret_cast<const uint32_t*>(value.data());
  if (std::endian::native == std::endian::big) {
    return request_id_be;
  }
  return std::byteswap(request_id_be);
}

std::string_view TlvCodec::PayloadAfterRequestId(const std::string& value) {
  if (value.size() <= sizeof(uint32_t)) return {};
  return std::string_view(value).substr(sizeof(uint32_t));
}

TlvMessage TlvCodec::MakeResponse(MessageTag::Resp::Type resp_tag,
                                  uint32_t request_id, StatusCode::Type status,
                                  std::string_view body) {
  TlvMessage response;
  response.set_tag(static_cast<uint16_t>(resp_tag));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + body.size());

  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));
  writer.WriteBytes<const char>(body);

  response.set_value(value);
  return response;
}

TlvMessage TlvCodec::MakeError(uint32_t request_id, uint16_t request_tag,
                               StatusCode::Type status) {
  TlvMessage error;
  error.set_tag(static_cast<uint16_t>(MessageTag::Resp::Error));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));

  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(request_tag);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));

  error.set_value(value);
  return error;
}

TlvMessage TlvCodec::MakePush(MessageTag::Push::Type push_tag,
                              std::string_view body) {
  TlvMessage push;
  push.set_tag(static_cast<uint16_t>(push_tag));
  push.set_value(std::string(body));
  return push;
}
