#if !defined(BECHAT_PROTO_STATUS_CODE_H_)
#define BECHAT_PROTO_STATUS_CODE_H_

// Success
#define BECHAT_STATUS_SUCCESS 0x0000U

// Tlv Parse Error
#define BECHAT_STATUS_INVALID_TAG 0x0001U      // for Session read tag stage
#define BECHAT_STATUS_UNSUPPORTED_TAG 0x0002U  // for RequestFactory parse stage
#define BECHAT_STATUS_MALFORMED_PAYLOAD 0x0003U

// User Registry Error
#define BECHAT_STATUS_SIGNUP_FAIL 0x0101U
#define BECHAT_STATUS_LOGIN_FAIL 0x0102U

// Server Internal Error
#define BECHAT_STATUS_INTERNAL_ERROR 0x7FFFU

#endif  // BECHAT_PROTO_STATUS_CODE_H_
