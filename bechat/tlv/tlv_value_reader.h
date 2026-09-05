/**
 * @file tlv_value_reader.h
 * @author Keunlas
 * @brief 与 TlvMessage 的 value 字段相关的解码操作
 * @date 2026-08-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_VALUE_READER_H_
#define BECHAT_TLV_VALUE_READER_H_

#include <bit>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "bechat/utils/concepts.h"

/**
 * @brief 从 value 中读出数据
 *
 */
class ValueReader {
 public:
  explicit ValueReader(std::string_view val)
      : value_(val), pos_(value_.begin()) {}

  /// @brief 读出一个整数
  /// @tparam T 类型 T 应当为整数
  template <Integer T>
  bool ReadInt(T& integer) {
    if (!ensure(sizeof(integer))) return false;

    T raw = *reinterpret_cast<const T*>(pos_);
    if constexpr (std::endian::native != std::endian::big) {
      integer = std::byteswap(raw);
    } else {
      integer = raw;
    }
    pos_ += sizeof(integer);

    return true;
  }

  /// @brief 读出一个字符串
  /// (String：u16 length + bytes)
  bool ReadString(std::string& out) {
    uint16_t len = 0;
    if (!ReadInt<uint16_t>(len)) return false;
    if (len > Remaining()) return false;
    out.assign(pos_, len);
    pos_ += len;
    return true;
  }

  /// @brief 读出一个字符串视图
  /// (String：u16 length + bytes)
  /// @attention 请确保使用 out 时，构造用的 value 依旧可访问
  bool ReadStringView(std::string_view& out) {
    uint16_t len = 0;
    if (!ReadInt<uint16_t>(len)) return false;
    if (len > Remaining()) return false;
    out = std::string_view(pos_, len);
    pos_ += len;
    return true;
  }

 public:
  /// @brief 获取 value 还剩下多少未读字节
  inline size_t Remaining() const {
    auto dis = std::distance(pos_, value_.end());
    return dis < 0 ? 0U : static_cast<size_t>(dis);
  }

  /// @brief 获取 value 是否读完
  inline bool Done() const { return pos_ >= value_.end(); }

 private:
  inline bool ensure(size_t n) const { return n <= Remaining(); }

 private:
  std::string_view value_;
  std::string_view::iterator pos_;
};

#endif  // !BECHAT_TLV_VALUE_READER_H_
