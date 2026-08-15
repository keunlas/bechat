/**
 * @file tlv_message.h
 * @author Keunlas
 * @brief 最基础的 TLV 消息类
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_TLV_MESSAGE_H_
#define BECHAT_TLV_TLV_MESSAGE_H_

#include <cstdint>
#include <string>

class TlvMessage {
 public:
  using TagT = uint16_t;
  using LengthT = uint16_t;
  using ValueT = std::string;

 public:
  inline TagT tag() const { return tag_; }

  inline TagT* mutable_tag() { return &tag_; }

  inline void set_tag(TagT tag) { tag_ = tag; }

  inline LengthT length() const { return length_; }

  inline LengthT* mutable_length() { return &length_; }

  inline void set_length(LengthT length) { length_ = length; }

  inline const ValueT& value() const { return value_; }

  inline ValueT* mutable_value() { return &value_; }

  inline void set_value(const ValueT& value) { value_ = value; }

 private:
  TagT tag_{};
  LengthT length_{};
  ValueT value_{};
};

#endif  // !BECHAT_TLV_TLV_MESSAGE_H_
