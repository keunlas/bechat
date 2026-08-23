/**
 * @file tlv_value_writer.h
 * @author Keunlas
 * @brief 与 TlvMessage 的 value 字段相关的编码操作
 * @date 2026-08-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_VALUE_WRITER_H_
#define BECHAT_TLV_VALUE_WRITER_H_

#include <bit>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "bechat/utils/concepts.h"

/**
 * @brief 向 value 中写入数据
 *
 */
class ValueWriter {
 public:
  explicit ValueWriter(std::string& val) : value_(val) {}

  /// @brief 写入一个整数
  /// @tparam T 类型 T 应当为整数
  template <Integer T>
  void WriteInt(T integer) {
    if constexpr (sizeof(integer) != 1) {
      if constexpr (std::endian::native != std::endian::big) {
        integer = std::byteswap(integer);
      }
    }
    value_.append(reinterpret_cast<const char*>(&integer), sizeof(integer));
  }

  /// @brief 写入一个字符串
  /// (String：u16 length + bytes)
  void WriteString(std::string_view sv) {
    assert(sv.size() <= UINT16_MAX);
    WriteInt(static_cast<uint16_t>(sv.size()));
    value_.append(sv.data(), sv.size());
  }

  /// @brief 写入二进制字节数据
  template <OneByte T>
  void WriteBytes(std::span<T> bytes) {
    value_.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }

 private:
  std::string& value_;  // ref to value
};

#endif  // !BECHAT_TLV_VALUE_WRITER_H_
