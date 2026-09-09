// Distributed under the MIT License that can be found in the LICENSE file.
// https://github.com/keunlas/betools
//
// Author: Keunlas <keunlaz at gmail dot com>

#ifndef KEUNLAS_BETOOLS_BASE_HPP_
#define KEUNLAS_BETOOLS_BASE_HPP_

/**
 * @file base.hpp
 * @author Keunlas (keunlaz at gmail dot com)
 * @brief 本头文件包含 Base62 和 Base64 编码相关工具，
 * 这个文件是 header-only 且 self-contained 的，
 * 可以随便复制到任何路径下直接进行使用。
 * @version 1.2.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace betools::base::alphabet {

/**
 * @brief Base62 编码字符集，提供 0-9、A-Z、a-z 共 62 个字符的正向与反向映射表。
 */
struct base62 {
  static const std::array<char, 62>& data() noexcept {
    static constexpr std::array<char, 62> data{
        {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C',
         'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
         'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c',
         'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
         'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'}};
    return data;
  }
  static const std::array<int8_t, 256>& rdata() noexcept {
    static constexpr std::array<int8_t, 256> rdata{
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,
         6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, 16,
         17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
         35, -1, -1, -1, -1, -1, -1, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
         47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1}};
    return rdata;
  }
};

/**
 * @brief Base64 编码字符集，
 * 提供合法的 Base64 字符的正向与反向映射表及其填充字符串。
 */
struct base64 {
  static const std::array<char, 64>& data() noexcept {
    static constexpr std::array<char, 64> data{
        {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
         'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
         'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
         'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
         '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'}};
    return data;
  }
  static const std::array<int8_t, 256>& rdata() noexcept {
    static constexpr std::array<int8_t, 256> rdata{
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
         58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
         7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
         25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
         37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1}};
    return rdata;
  }
  static const std::string& fill() noexcept {
    static const std::string fill{"="};
    return fill;
  }
};

/**
 * @brief Base64 编码字符集,
 * url-safe 并且 filename-safe，
 * 提供合法的 Base64 字符的正向与反向映射表及其填充字符串。
 */
struct base64url {
  static const std::array<char, 64>& data() noexcept {
    static constexpr std::array<char, 64> data{
        {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
         'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
         'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
         'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
         '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'}};
    return data;
  }
  static const std::array<int8_t, 256>& rdata() noexcept {
    static constexpr std::array<int8_t, 256> rdata{
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, 52, 53, 54, 55, 56, 57,
         58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
         7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
         25, -1, -1, -1, -1, 63, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
         37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1}};
    return rdata;
  }
  static const std::string& fill() noexcept {
    static const std::string fill{"%3d"};
    return fill;
  }
};

}  // namespace betools::base::alphabet

namespace betools::base::base62::details {

/**
 * @brief base62::encode 的具体实现
 * @attention 请避免直接使用 details 命名空间下的接口或代码，
 * 它们随时可能进行大幅更改
 */
inline std::string encode(const std::string& binary_data,
                          const std::array<char, 62>& alphabet) {
  if (binary_data.empty()) return "";

  // 统计输入中的前导零字节（0x00）个数
  std::size_t zeros = 0;
  while (zeros < binary_data.size() && binary_data[zeros] == 0) {
    zeros += 1;
  }

  // 全零输入直接返回对应数量的 alphabet[0] 字符
  if (zeros == binary_data.size()) {
    return std::string(zeros, alphabet[0]);
  }

  // 将剩余字节复制到大整数向量（big-endian 表示，索引 0 为最高字节）
  std::vector<uint8_t> bignum;
  bignum.reserve(binary_data.size() - zeros);
  for (auto i = zeros; i < binary_data.size(); ++i) {
    bignum.push_back(static_cast<uint8_t>(binary_data[i]));
  }

  // 临时存放逆序的编码字符（低位在前）
  std::string tmp;
  tmp.reserve(binary_data.size() * 3 / 2 + 1);

  // 反复执行"大整数 / 62"操作，直到商为 0
  // 使用 start 索引代替 vector::erase，避免 O(n) 的头部删除
  std::size_t start = 0;
  while (start < bignum.size()) {
    int carry = 0;
    // 从高位到低位处理每个字节
    for (std::size_t i = start; i < bignum.size(); ++i) {
      int value = (carry << 8) + bignum[i];
      bignum[i] = static_cast<uint8_t>(value / 62);
      carry = value % 62;
    }
    // 跳过商的前导零，保持规范化
    while (start < bignum.size() && bignum[start] == 0) {
      ++start;
    }
    // carry 即为本次除法的余数，对应一个 base 字符
    tmp.push_back(alphabet[carry]);
  }

  // 组合最终结果：前导"0"字符 + 反转后的 tmp
  std::string result;
  result.reserve(zeros + tmp.size());
  result.append(zeros, alphabet[0]);
  result.append(tmp.rbegin(), tmp.rend());

  return result;
}

/**
 * @brief base62::decode 的具体实现
 * @attention 请避免直接使用 details 命名空间下的接口或代码，
 * 它们随时可能进行大幅更改
 */
inline std::string decode(const std::string& base_string,
                          const std::array<int8_t, 256>& rdata) {
  if (base_string.empty()) return "";

  // Base62 中 alphabet[0] 对应字符 '0'
  // 暂不考虑用户自定义 zero_char 的情况
  char zero_char = '0';

  // 统计输入字符串前导"零字符"的个数
  std::size_t zeros = 0;
  while (zeros < base_string.size() && base_string[zeros] == zero_char) {
    ++zeros;
  }

  // 初始化大整数（little-endian）
  std::vector<uint8_t> bignum(1, 0);

  // 遍历剩余字符，进行 bignum = bignum * 62 + digit
  for (std::size_t i = zeros; i < base_string.size(); ++i) {
    auto c = static_cast<uint8_t>(base_string[i]);
    auto digit = static_cast<int>(rdata[c]);
    if (digit < 0) {
      return "";  // 包含非法字符，直接返回空表示失败
    }
    // 从低位向高位处理
    int carry = digit;
    for (auto&& term : bignum) {
      int product = static_cast<int>(term) * 62 + carry;
      term = static_cast<uint8_t>(product % 256);
      carry = product / 256;
    }
    // 处理计算产生的额外进位
    while (carry > 0) {
      bignum.push_back(static_cast<uint8_t>(carry % 256));
      carry >>= 8;
    }
  }

  // 移除大整数前导零
  while (!bignum.empty() && bignum.back() == 0) {
    bignum.pop_back();
  }

  // 构造最终二进制结果：zeros 个 0x00 + bignum 字节序列
  std::string result;
  result.reserve(zeros + bignum.size());
  result.append(zeros, '\0');
  for (auto it = bignum.rbegin(); it != bignum.rend(); ++it) {
    result.push_back(static_cast<char>(*it));
  }

  return result;
}

}  // namespace betools::base::base62::details

namespace betools::base::base64::details {

/**
 * @brief base64::encode 的具体实现
 * @attention 请避免直接使用 details 命名空间下的接口或代码，
 * 它们随时可能进行大幅更改
 */
inline std::string encode(const std::string& binary_data,
                          const std::array<char, 64>& alphabet,
                          const std::string& fill) {
  if (binary_data.empty()) return "";

  std::string result;
  result.reserve(((binary_data.size() + 2) / 3) * 4);

  auto iter = binary_data.begin();
  for (; binary_data.end() - iter >= 3; iter += 3) {
    auto index1 = ((*iter) & 0xfc) >> 2;
    auto index2 = (((*iter) & 0x03) << 4) + (((*(iter + 1)) & 0xf0) >> 4);
    auto index3 = (((*(iter + 1)) & 0x0f) << 2) + (((*(iter + 2)) & 0xc0) >> 6);
    auto index4 = ((*(iter + 2)) & 0x3f);
    result.append({alphabet[index1], alphabet[index2], alphabet[index3],
                   alphabet[index4]});
  }

  if (auto remains = binary_data.end() - iter; remains == 1) {
    auto index1 = ((*iter) & 0xfc) >> 2;
    auto index2 = (((*iter) & 0x03) << 4);
    result.append({alphabet[index1], alphabet[index2]});
    result.append(fill + fill);
  } else if (remains == 2) {
    auto index1 = ((*iter) & 0xfc) >> 2;
    auto index2 = (((*iter) & 0x03) << 4) + (((*(iter + 1)) & 0xf0) >> 4);
    auto index3 = (((*(iter + 1)) & 0x0f) << 2);
    result.append({alphabet[index1], alphabet[index2], alphabet[index3]});
    result.append(fill);
  }

  return result;
}

/**
 * @brief base64::decode 的具体实现
 * @attention 请避免直接使用 details 命名空间下的接口或代码，
 * 它们随时可能进行大幅更改
 */
inline std::string decode(const std::string& base_string,
                          const std::array<int8_t, 256>& rdata,
                          const std::string& fill) {
  if (base_string.empty()) return "";

  std::string result;
  result.reserve(((base_string.size() + 3) / 4) * 3);

  size_t paddings = 0;
  size_t padlen = 0;
  auto pad_pos = base_string.find(fill);
  if (pad_pos != std::string_view::npos) {
    padlen = base_string.size() - pad_pos;
    paddings = padlen / fill.size();
  }

  auto iter = base_string.begin();
  for (; iter + 4 < base_string.end() - padlen; iter += 4) {
    auto index1 = rdata[static_cast<uint8_t>(*iter)];
    auto index2 = rdata[static_cast<uint8_t>(*(iter + 1))];
    auto index3 = rdata[static_cast<uint8_t>(*(iter + 2))];
    auto index4 = rdata[static_cast<uint8_t>(*(iter + 3))];

    if (index1 < 0 || index2 < 0 || index3 < 0 || index4 < 0) {
      return "";
    }

    auto char1 = ((index1 & 0x3f) << 2) | ((index2 & 0x30) >> 4);
    auto char2 = ((index2 & 0x0f) << 4) | ((index3 & 0x3c) >> 2);
    auto char3 = ((index3 & 0x03) << 6) | (index4 & 0x3f);

    result.append({static_cast<char>(char1), static_cast<char>(char2),
                   static_cast<char>(char3)});
  }

  if (iter == base_string.end()) {
    return result;
  }

  size_t remains = 0;
  if (paddings == 0) {
    remains = base_string.end() - iter;
  } else {
    remains = 4 - paddings;
  }

  if (remains > 1) {
    auto char1 = ((rdata[static_cast<uint8_t>(*iter)] & 0x3f) << 2) |
                 ((rdata[static_cast<uint8_t>(*(iter + 1))] & 0x30) >> 4);
    result.push_back(static_cast<char>(char1));
  }

  if (remains > 2) {
    auto char2 = ((rdata[static_cast<uint8_t>(*(iter + 1))] & 0x0f) << 4) |
                 ((rdata[static_cast<uint8_t>(*(iter + 2))] & 0x3c) >> 2);
    result.push_back(static_cast<char>(char2));
  }

  if (remains > 3) {
    auto char3 = ((rdata[static_cast<uint8_t>(*(iter + 2))] & 0x03) << 6) |
                 (rdata[static_cast<uint8_t>(*(iter + 3))] & 0x3f);
    result.push_back(static_cast<char>(char3));
  }

  return result;
}

/**
 * @brief base64::pad 的具体实现
 * @attention 请避免直接使用 details 命名空间下的接口或代码，
 * 它们随时可能进行大幅更改
 */
inline std::string pad(const std::string& base_string,
                       const std::string& fill) {
  std::string padding;
  for (std::size_t i = 0; i < (4 - base_string.size() % 4) % 4; ++i) {
    padding += fill;
  }
  return base_string + padding;
}

/**
 * @brief base64::trim 的具体实现
 */
inline std::string trim(const std::string& base_string,
                        const std::string& fill) {
  auto pos = base_string.find(fill);
  return base_string.substr(0, pos);
}

}  // namespace betools::base::base64::details

namespace betools::base::base62 {

/**
 * @brief 将二进制数据编码为 Base62 字符串。
 * @attention 若输入的二进制数据由数字类型指针转换而来，
 * 应注意输入的数字需为大端序。
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base62。
 * @param binary_data 待编码的二进制字符串。
 * @return 编码后的字符串。
 */
template <typename Alphabets = alphabet::base62>
std::string encode(const std::string& binary_data) {
  return details::encode(binary_data, Alphabets::data());
}

/**
 * @brief 将 Base62 编码字符串解码为原始二进制数据。
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base62。
 * @param base_string 待解码的字符串。
 * @return 解码后的二进制数据；若包含非法字符则返回空字符串。
 */
template <typename Alphabets = alphabet::base62>
std::string decode(const std::string& base_string) {
  return details::decode(base_string, Alphabets::rdata());
}

}  // namespace betools::base::base62

namespace betools::base::base64 {

/**
 * @brief 将二进制数据编码为 Base64 字符串。
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base64。
 * @param binary_data 待编码的二进制字符串。
 * @return 编码后的字符串。
 */
template <typename Alphabets = alphabet::base64>
std::string encode(const std::string& binary_data) {
  return details::encode(binary_data, Alphabets::data(), Alphabets::fill());
}

/**
 * @brief 将 Base64 编码字符串解码为原始二进制数据。
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base64。
 * @param base_string 待解码的字符串。
 * @return 解码后的二进制数据；若包含非法字符则返回空字符串。
 */
template <typename Alphabets = alphabet::base64>
std::string decode(const std::string& base_string) {
  return details::decode(base_string, Alphabets::rdata(), Alphabets::fill());
}

/**
 * @brief 给修剪过的 Base64 编码字符串重新添加上填充。
 * @attention 当填充符长度大于 1 时，
 * 请确保传入的 base_string 一定是修剪过后的。
 * 否则可能会获得错误的结果。
 * 例如使用 base64url 时传入 "YSB2YT8%3d" 输出 "YSB2YT8%3d%3d%3d".
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base64。
 * @param base_string 修剪过的 Base64 编码字符串。
 * @return 重新添加上填充的 Base64 编码字符串。
 */
template <typename Alphabets = alphabet::base64>
std::string pad(const std::string& base_string) {
  return details::pad(base_string, Alphabets::fill());
}

/**
 * @brief 修剪 Base64 编码字符串，去掉末尾的填充。
 *
 * @tparam Alphabets 编码字符集类型，默认为 alphabet::base64。
 * @param base_string 完整的 Base64 编码字符串。
 * @returns 修剪过的 Base64 编码字符串。
 *
 * @note 在一些 url-safe 或者 filename-safe 的 Base64 标准中，
 * 会要求去掉末尾的填充字符。
 */
template <typename Alphabets = alphabet::base64>
std::string trim(const std::string& base_string) {
  return details::trim(base_string, Alphabets::fill());
}

}  // namespace betools::base::base64

#endif  // !KEUNLAS_BETOOLS_BASE_HPP_
