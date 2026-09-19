#if !defined(BECHAT_PROTO_RESPONSE_FACTORY_H_)
#define BECHAT_PROTO_RESPONSE_FACTORY_H_

#include <cstdint>
#include <string>

class ResponseFactory {
 public:
  static std::string MakeError(uint16_t tag, uint32_t request_id,
                               uint32_t status_code);

  static std::string MakeResponse(uint16_t tag, std::string_view payload);
};

#endif  // BECHAT_PROTO_RESPONSE_FACTORY_H_
