#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

#include "bechat/utils/logger.h"

template <typename T>
struct IsSslStreamT : std::false_type {};

template <typename Socket>
struct IsSslStreamT<asio::ssl::stream<Socket>> : std::true_type {};

template <typename T>
inline constexpr bool IsSslStream =
    IsSslStreamT<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename Socket>
class Session : public std::enable_shared_from_this<Session<Socket>> {
 public:
  explicit Session(ServerContexts& server_contexts, Socket socket)
      : server_contexts_(server_contexts),
        socket_(std::move(socket)),
        read_strand_(asio::make_strand(socket_.get_executor())),
        write_strand_(asio::make_strand(socket_.get_executor())) {}

  /**
   * @brief Session 启动的入口
   *
   */
  void Start() {
    if constexpr (IsSslStream<Socket>) {
      INFO("Session {} is a SslSession", memaddr());
      ssl_handshake();
    } else {
      INFO("Session {} is a NosslSession", memaddr());
    }
  }

  /**
   * @brief Session 关闭的唯一入口
   *
   */
  void Shutdown() {
    if (!closing_.exchange(true)) {
      auto self(this->shared_from_this());
      asio::post(write_strand_, [self] { self->do_shutdown(); });
    }
  }

 private:
  /**
   * @brief 仅当该 Session 为 SslSession 时调用
   *
   */
  void ssl_handshake() {
    static_assert(IsSslStream<Socket>, "Only SslSession can do ssl_handshake");
    auto self(this->shared_from_this());
    socket_.async_handshake(asio::ssl::stream_base::server,
                            [this, self](const std::error_code& error) {
                              if (!error) {
                              } else {
                                handle_error(error);
                              }
                            });
  }

  /**
   * @brief 获取该 Session 的内存地址
   *
   * @return void*
   */
  const void* const memaddr() { return (void*)this; }

  /**
   * @brief 处理关闭的逻辑
   *
   */
  void do_shutdown() {
    assert(closing_.load() == true);
    assert(write_strand_.running_in_this_thread());

    // 1. [TODO] 待 server_contexts_ 完善后处理相关的逻辑

    // 2. 处理 socket 关闭的逻辑
    std::error_code _;  // 忽略过程的所有错误
    if constexpr (IsSslStream<Socket>) {
      socket_.lowest_layer().cancel(_);
      socket_.lowest_layer().shutdown(asio::socket_base::shutdown_both, _);
      socket_.lowest_layer().close(_);
    } else {
      socket_.cancel(_);
      socket_.shutdown(asio::socket_base::shutdown_both, _);
      socket_.close(_);
    }
  }

  /**
   * @brief 错误处理的逻辑
   *
   */
  void handle_error(const std::error_code& error) {
    if (error == asio::error::eof) {
      INFO("Session {} peer closed connection", memaddr());
    } else {
      ERROR("Session {} error: {}", memaddr(), error.message());
    }
    Shutdown();
  }

 private:
  ServerContexts& server_contexts_;
  Socket socket_;

  // 当出现错误或者异常时，置位 closing_
  std::atomic_bool closing_{false};

  // 读 strand 用来进行读操作
  asio::strand<asio::any_io_executor> read_strand_;

  // 写 strand 用来进行写操作
  asio::strand<asio::any_io_executor> write_strand_;
};

using SslSession = Session<asio::ssl::stream<asio::ip::tcp::socket>>;
using NosslSession = Session<asio::ip::tcp::socket>;

#endif  // !BECHAT_CORE_SESSION_H_
