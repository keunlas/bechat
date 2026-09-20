#if !defined(BECHAT_PROTO_TLV_CONSTANTS_H_)
#define BECHAT_PROTO_TLV_CONSTANTS_H_

#include <cstdint>
#include <limits>

struct TlvConstants {
  // TLV 协议各字段大小
  static constexpr uint16_t kTagSize{sizeof(uint16_t)};
  static constexpr uint16_t kLengthSize{sizeof(uint16_t)};
  static constexpr uint16_t kHeaderSize{kTagSize + kLengthSize};
  static constexpr uint16_t kMaxValueSize{std::numeric_limits<uint16_t>::max()};

  // kMaxPayloadSize 必须比 kMaxValueSize 小才会起作用
  static constexpr uint16_t kMaxPayloadSize{kMaxValueSize};
};

#endif  // BECHAT_PROTO_TLV_CONSTANTS_H_
