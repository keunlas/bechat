#ifndef BECHAT_SESSION_H_
#define BECHAT_SESSION_H_

#include <asio.hpp>
#include <cstdint>
#include <memory>

#include "tlv/tlv_message.h"

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket);
  ~Session();

  void Start() { read_type(); }

 private:
  void read_type();

  void read_length();

  void read_value(uint16_t len);

  void do_write();

 private:
  asio::ip::tcp::socket socket_;
  TlvMessage input_msg_;
};

#endif  // !BECHAT_SESSION_H_
