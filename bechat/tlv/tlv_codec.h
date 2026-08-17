/**
 * @file tlv_codec.h
 * @author Keunlas
 * @brief 与 TlvMessage 相关的编解码操作
 * @date 2026-08-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_TLV_CODEC_H_
#define BECHAT_TLV_TLV_CODEC_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "bechat/tlv/message_tag.h"
#include "bechat/tlv/status_code.h"
#include "bechat/tlv/tlv_message.h"

class TlvCodec {
 public:
  static uint32_t PeekRequestId(const std::string& value);

  static std::string_view PayloadAfterRequestId(const std::string& value);

  static TlvMessage MakeResponse(MessageTag::Resp::Type resp_tag,
                                 uint32_t request_id, StatusCode::Type status,
                                 std::string_view body = {});

  static TlvMessage MakeError(uint32_t request_id, uint16_t request_tag,
                              StatusCode::Type status);

  static TlvMessage MakePush(MessageTag::Push::Type push_tag,
                             std::string_view body = {});
};

class ValueWriter {
 public:
  explicit ValueWriter(std::string& out) : out_(out) {}

  void WriteU16(uint16_t num);

  void WriteU32(uint32_t num);

  void WriteU64(uint64_t num);

  void WriteString(std::string_view sv);

  void WriteBytes(std::string_view bytes);

 private:
  std::string& out_;
};

class ValueReader {
 public:
  explicit ValueReader(std::string_view data) : data_(data) {}

  bool ReadU16(uint16_t& out);

  bool ReadU32(uint32_t& out);

  bool ReadU64(uint64_t& out);

  bool ReadString(std::string& out);  // 协议 String：u16 length + bytes

  inline bool Done() const { return pos_ == data_.size(); }

  inline size_t Remaining() const { return data_.size() - pos_; }

 private:
  inline bool ensure(size_t n) const { return n <= Remaining(); }

 private:
  std::string_view data_;
  size_t pos_{0};
};

#endif  // !BECHAT_TLV_TLV_CODEC_H_
