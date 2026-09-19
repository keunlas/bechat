#if !defined(BECHAT_PROTO_RESPONSE_FACTORY_H_)
#define BECHAT_PROTO_RESPONSE_FACTORY_H_

#include <cstdint>
#include <string>

class ResponseFactory {
 public:
  static std::string MakeError(uint16_t tag, uint32_t request_id,
                               uint32_t status_code);
};

#endif  // BECHAT_PROTO_RESPONSE_FACTORY_H_
