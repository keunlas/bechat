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

#include <endian.h>

#include <cassert>

uint32_t TlvCodec::PeekRequestId(const std::string& value) {
  assert(value.size() >= sizeof(uint32_t));
  uint32_t request_id_be = *reinterpret_cast<const uint32_t*>(value.data());
  return be32toh(request_id_be);
}

std::string_view TlvCodec::PayloadAfterRequestId(const std::string& value) {
  if (value.size() < sizeof(uint32_t)) return {};
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
  writer.WriteU32(request_id);
  writer.WriteU16(static_cast<uint16_t>(status));
  writer.WriteBytes(body);

  response.set_value(value);
  response.set_length(static_cast<TlvMessage::LengthT>(value.size()));
  return response;
}

TlvMessage TlvCodec::MakeError(uint32_t request_id, uint16_t request_tag,
                               StatusCode::Type status) {
  TlvMessage error;
  error.set_tag(static_cast<uint16_t>(MessageTag::Resp::Error));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));

  ValueWriter writer(value);
  writer.WriteU32(request_id);
  writer.WriteU16(request_tag);
  writer.WriteU16(static_cast<uint16_t>(status));

  error.set_value(value);
  error.set_length(static_cast<TlvMessage::LengthT>(value.size()));
  return error;
}

TlvMessage TlvCodec::MakePush(MessageTag::Push::Type push_tag,
                              std::string_view body) {
  TlvMessage push;
  push.set_tag(static_cast<uint16_t>(push_tag));
  push.set_value(std::string(body));
  push.set_length(static_cast<TlvMessage::LengthT>(body.size()));
  return push;
}

void ValueWriter::WriteU16(uint16_t num) {
  auto num_be = htobe16(num);
  out_.append(reinterpret_cast<const char*>(&num_be), sizeof(num_be));
}

void ValueWriter::WriteU32(uint32_t num) {
  auto num_be = htobe32(num);
  out_.append(reinterpret_cast<const char*>(&num_be), sizeof(num_be));
}

void ValueWriter::WriteU64(uint64_t num) {
  auto num_be = htobe64(num);
  out_.append(reinterpret_cast<const char*>(&num_be), sizeof(num_be));
}

void ValueWriter::WriteString(std::string_view sv) {
  assert(sv.size() <= UINT16_MAX);
  WriteU16(static_cast<uint16_t>(sv.size()));
  WriteBytes(sv);
}

void ValueWriter::WriteBytes(std::string_view bytes) {
  out_.append(bytes.data(), bytes.size());
}

bool ValueReader::ReadU16(uint16_t& out) {
  if (!ensure(sizeof(uint16_t))) return false;

  uint16_t u16_be = *reinterpret_cast<const uint16_t*>(
      data_.substr(pos_, sizeof(uint16_t)).data());

  out = be16toh(u16_be);
  pos_ += sizeof(u16_be);
  return true;
}

bool ValueReader::ReadU32(uint32_t& out) {
  if (!ensure(sizeof(uint32_t))) return false;

  uint32_t u32_be = *reinterpret_cast<const uint32_t*>(
      data_.substr(pos_, sizeof(uint32_t)).data());

  out = be32toh(u32_be);
  pos_ += sizeof(u32_be);
  return true;
}

bool ValueReader::ReadU64(uint64_t& out) {
  if (!ensure(sizeof(uint64_t))) return false;

  uint64_t u64_be = *reinterpret_cast<const uint64_t*>(
      data_.substr(pos_, sizeof(uint64_t)).data());

  out = be64toh(u64_be);
  pos_ += sizeof(u64_be);
  return true;
}

bool ValueReader::ReadString(std::string& out) {
  uint16_t len = 0;
  if (!ReadU16(len)) return false;
  if (len > Remaining()) return false;

  out.assign(data_.substr(pos_, len));
  pos_ += len;
  return true;
}
