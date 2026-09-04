/**
 * @file tlv_message.cpp
 * @author Keunlas
 * @brief 最基础的 TLV 消息类
 * @date 2026-08-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/tlv/tlv_message.h"

#include <bit>

std::string TlvMessage::SerializeToString() {
  std::string result{};
  result.reserve(value_.size() + sizeof(TagT) + sizeof(LengthT));

  auto tag = tag_;
  auto length = static_cast<LengthT>(value_.length());

  if (std::endian::native != std::endian::big) {
    tag = std::byteswap(tag);
    length = std::byteswap(length);
  }

  result.append(reinterpret_cast<char*>(&tag), sizeof(tag));
  result.append(reinterpret_cast<char*>(&length), sizeof(length));
  result.append(value_);

  return result;
}
