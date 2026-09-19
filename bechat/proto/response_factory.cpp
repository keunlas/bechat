#include "bechat/proto/response_factory.h"

#include <bit>
#include <cstring>
#include <nlohmann/json.hpp>

#include "bechat/core/session.h"

std::string ResponseFactory::MakeError(uint16_t tag, uint32_t request_id,
                                       uint32_t status_code) {
  using nlohmann::json;
  json jvalue = json::object();
  jvalue["request_id"] = request_id;
  jvalue["status_code"] = status_code;
  std::string value{jvalue.dump()};
  uint16_t length = value.size();

  const uint16_t msg_length = NosslSession::kHeaderSize + length;
  std::string msg(msg_length, '\0');

  const uint16_t length_be{
      std::endian::native == std::endian::big ? length : std::byteswap(length)};
  const uint16_t tag_be{
      std::endian::native == std::endian::big ? tag : std::byteswap(tag)};

  std::memcpy(msg.data(), &tag_be, NosslSession::kTagSize);
  std::memcpy(msg.data() + NosslSession::kTagSize, &length_be,
              NosslSession::kLengthSize);
  std::memcpy(msg.data() + NosslSession::kHeaderSize, value.data(), length);

  return msg;
}
