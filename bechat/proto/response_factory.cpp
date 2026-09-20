#include "bechat/proto/response_factory.h"

#include <bit>
#include <cstring>
#include <nlohmann/json.hpp>

#include "bechat/proto/tlv_constants.h"

/**
 * @brief 检查是否是大端序，不是则转换成大端序
 *
 */
template <typename T>
constexpr T check_and_byteswap(T x) {
  if constexpr (std::endian::native == std::endian::big) {
    return x;
  } else {
    return std::byteswap(x);
  }
}

std::string ResponseFactory::MakeResponse(uint16_t tag,
                                          const nlohmann::json& payload) {
  std::string value{payload.dump()};
  uint16_t value_size = value.size();

  std::string msg((sizeof(uint16_t) * 2) + value_size, '\0');

  const uint16_t tag_be{check_and_byteswap(tag)};
  const uint16_t length_be{check_and_byteswap(value_size)};

  std::memcpy(msg.data(), &tag_be, sizeof(uint16_t));
  std::memcpy(msg.data() + sizeof(uint16_t), &length_be, sizeof(uint16_t));
  std::memcpy(msg.data() + (sizeof(uint16_t) * 2), value.data(), value_size);

  return msg;
}

std::string ResponseFactory::MakeError(uint16_t tag, uint32_t request_id,
                                       uint32_t status_code) {
  nlohmann::json jvalue = nlohmann::json::object();
  jvalue["request_id"] = request_id;
  jvalue["status_code"] = status_code;
  std::string value{jvalue.dump()};
  uint16_t value_size = value.size();

  std::string msg(TlvConstants::kHeaderSize + value_size, '\0');

  const uint16_t tag_be{check_and_byteswap(tag)};
  const uint16_t length_be{check_and_byteswap(value_size)};

  std::memcpy(msg.data(), &tag_be, sizeof(uint16_t));
  std::memcpy(msg.data() + sizeof(uint16_t), &length_be, sizeof(uint16_t));
  std::memcpy(msg.data() + (sizeof(uint16_t) * 2), value.data(), value_size);

  return msg;
}
