#include "bechat/core/session.h"

#include <endian.h>

#include "bechat/utils/logger.h"

Session::Session(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)),
      read_strand_(asio::make_strand(socket_.get_executor())),
      write_strand_(asio::make_strand(socket_.get_executor())) {}

Session::~Session() { INFO("Session {} has closed.", (void*)this); }

void Session::Start() {
  INFO("Session {} has started.", (void*)this);
  read_tag();
}

void Session::read_tag() {
  auto self(shared_from_this());
  asio::async_read(
      socket_, asio::buffer(input_msg_.mutable_tag(), sizeof(TlvMessage::TagT)),
      asio::bind_executor(read_strand_,
                          [this, self](std::error_code ec, std::size_t) {
                            if (!ec) {
                              input_msg_.set_tag(be16toh(input_msg_.tag()));
                              read_length();
                            } else {
                              WARN("Error ocurred in Session {}, read_tag: {}",
                                   (void*)this, ec.message());
                            }
                          }));
}

void Session::read_length() {
  auto self(shared_from_this());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_length(), sizeof(TlvMessage::LengthT)),
      asio::bind_executor(
          read_strand_, [this, self](std::error_code ec, std::size_t) {
            if (!ec) {
              input_msg_.set_length(be16toh(input_msg_.length()));
              read_value(input_msg_.length());
            } else {
              WARN("Error ocurred in Session {}, read_length: {}", (void*)this,
                   ec.message());
            }
          }));
}

void Session::read_value(uint16_t len) {
  assert(input_msg_.length() == len);
  (void)len;
  auto self(shared_from_this());
  input_msg_.mutable_value()->resize(input_msg_.length());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_value()->data(), input_msg_.length()),
      asio::bind_executor(read_strand_, [this, self](std::error_code ec,
                                                     std::size_t) {
        if (!ec) {
          TRACE("Session {} read a message: [0x{:x}][{}][{} bytes of value]",
                (void*)this, input_msg_.tag(), input_msg_.length(),
                input_msg_.value().size());

          write_resp();
          read_tag();  // TCP 全双工通信，读写可同时进行
        } else {
          WARN("Error ocurred in Session {}, read_value: {}", (void*)this,
               ec.message());
        }
      }));
}

void Session::write_resp() {
  auto self(shared_from_this());
  auto msg = std::make_shared<TlvMessage>(input_msg_);
  asio::post(write_strand_, [this, self, msg]() {
    write_queue_.push(std::move(msg));
    if (!writing_) {
      start_writing();
    }
  });
}

void Session::start_writing() {
  assert(write_strand_.running_in_this_thread());

  if (write_queue_.empty()) {
    writing_ = false;
    return;
  }

  writing_ = true;
  auto self(shared_from_this());
  auto msg = write_queue_.front();

  asio::async_write(
      socket_, asio::buffer(msg->value()),
      asio::bind_executor(
          write_strand_,
          [this, self, msg](std::error_code ec, std::size_t /*length*/) {
            write_queue_.pop();
            writing_ = false;

            if (!ec) {
              TRACE("writing a resp: done");
              start_writing();
            } else {
              WARN(
                  "Error ocurred in Session {}, start_writing: {}, "
                  "Session is shutting down.",
                  (void*)this, ec.message());
              socket_.shutdown(asio::socket_base::shutdown_both);
            }
          }));
}
