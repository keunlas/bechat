#include "bechat/core/session.h"

#include <endian.h>

#include "bechat/utils/logger.h"

Session::Session(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

Session::~Session() { INFO("Session {} has closed.", (void*)this); }

void Session::Start() {
  INFO("Session {} has started.", (void*)this);
  read_tag();
}

void Session::read_tag() {
  auto self(shared_from_this());
  asio::async_read(
      socket_, asio::buffer(input_msg_.mutable_tag(), sizeof(TlvMessage::TagT)),
      [this, self](std::error_code ec, std::size_t) {
        if (!ec) {
          input_msg_.set_tag(be16toh(input_msg_.tag()));
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
          TRACE("Session {} read a message: [0x{:x}][{}][{} bytes of value]",
                (void*)this, input_msg_.tag(), input_msg_.length(),
                input_msg_.value().size());

          do_write();
          read_tag();
        }
      });
}

void Session::do_write() {
  auto self(shared_from_this());
  auto msg = std::make_shared<TlvMessage>(input_msg_);
  asio::async_write(
      socket_, asio::buffer(msg->value()),
      [this, self, msg](std::error_code ec, std::size_t /*length*/) {
        if (!ec) {
          TRACE("do_write: done");
        }
      });
}
