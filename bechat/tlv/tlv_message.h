#ifndef BECHAT_TLV_TLV_MESSAGE_H_
#define BECHAT_TLV_TLV_MESSAGE_H_

#include <cstdint>
#include <string>

class TlvMessage {
 public:
  using TypeT = uint16_t;
  using LengthT = uint16_t;
  using ValueT = std::string;

 public:
  inline TypeT type() const { return type_; }

  inline TypeT* mutable_type() { return &type_; }

  inline void set_type(TypeT type) { type_ = type; }

  inline LengthT length() const { return length_; }

  inline LengthT* mutable_length() { return &length_; }

  inline void set_length(LengthT length) { length_ = length; }

  inline const ValueT& value() const { return value_; }

  inline ValueT* mutable_value() { return &value_; }

  inline void set_value(const ValueT& value) { value_ = value; }

 private:
  TypeT type_{};
  LengthT length_{};
  ValueT value_{};
};

#endif  // !BECHAT_TLV_TLV_MESSAGE_H_
