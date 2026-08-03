#include "session.h"

#include <endian.h>

#include "logger.h"

Session::Session(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

Session::~Session() { Log::Trace("Session {} has closed.", (void*)this); }

void Session::read_type() {
  auto self(shared_from_this());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_type(), sizeof(TlvMessage::TypeT)),
      [this, self](std::error_code ec, std::size_t) {
        if (!ec) {
          input_msg_.set_type(be16toh(input_msg_.type()));
          Log::Trace("type: 0x{:x}", input_msg_.type());
          read_length();
        }
      });
}

void Session::read_length() {
  auto self(shared_from_this());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_length(), sizeof(TlvMessage::LengthT)),
      [this, self](std::error_code ec, std::size_t) {
        if (!ec) {
          input_msg_.set_length(be16toh(input_msg_.length()));
          Log::Trace("length: {}", input_msg_.length());
          read_value(input_msg_.length());
        }
      });
}

void Session::read_value(uint16_t len) {
  assert(input_msg_.length() == len);
  (void)len;
  auto self(shared_from_this());
  input_msg_.mutable_value()->resize(input_msg_.length());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_value()->data(), input_msg_.length()),
      [this, self](std::error_code ec, std::size_t) {
        if (!ec) {
          Log::Trace("value: recv done");
          do_write();
        }
      });
}

void Session::do_write() {
  auto self(shared_from_this());
  asio::async_write(socket_, asio::buffer(input_msg_.value()),
                    [this, self](std::error_code ec, std::size_t /*length*/) {
                      if (!ec) {
                        Log::Trace("do_write: done");
                      }
                    });
}
