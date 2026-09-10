#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
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

/**
 * @brief Session 类
 *
 * @tparam Socket 可以是 `asio::ip::tcp::socket`
 * 或者 `asio::ssl::stream<asio::ip::tcp::socket>` 等其他流式 socket
 */
template <typename Socket>
class Session : public std::enable_shared_from_this<Session<Socket>> {
 public:
  // TLS 挥手等待对方 close_notify 的超时时间
  static constexpr auto kShutdownTimeout{std::chrono::seconds(3)};

  // TLV 协议各字段大小
  static constexpr uint16_t kTagSize{sizeof(uint16_t)};
  static constexpr uint16_t kLengthSize{sizeof(uint16_t)};
  static constexpr uint16_t kMaxValueSize{std::numeric_limits<uint16_t>::max()};

  // 每次读取的块的大小
  static constexpr uint16_t kChunkSize{4096U};

  // kMaxPayloadSize 必须比 kMaxValueSize 小才会起作用
  static constexpr uint16_t kMaxPayloadSize{kMaxValueSize};

 public:
  explicit Session(ServerContexts& server_contexts, Socket socket)
      : server_contexts_(server_contexts),
        socket_(std::move(socket)),
        read_strand_(asio::make_strand(socket_.get_executor())),
        streambuf_(kTagSize + kLengthSize + kMaxValueSize),
        write_strand_(asio::make_strand(socket_.get_executor())),
        shutdown_timer_(socket_.get_executor()) {}

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
      start_read();
    }
  }

  /**
   * @brief Session 正常关闭的入口
   *
   * SslSession 会先和对方完成 TLS 挥手(close_notify)，然后再断开 socket；
   * NosslSession 会先发送 FIN，然后再断开 socket。
   *
   * 可以重复调用，只有第一次生效。
   *
   */
  void Shutdown() {
    if (!closing_.exchange(true)) {
      auto self(this->shared_from_this());
      asio::post(write_strand_, [self] { self->do_shutdown(); });
    }
  }

  /**
   * @brief Session 异常关闭的入口
   *
   * 不做 TLS 挥手，直接 cancel + 断开 socket；
   * 如果此时正常关闭正卡在等待对方的 close_notify，会打断这次等待。
   *
   * 可以重复调用，只有第一次生效。
   *
   */
  void Abort() {
    if (closed_.load()) return;
    aborted_.store(true);
    closing_.store(true);
    auto self(this->shared_from_this());
    asio::post(write_strand_, [self] { self->do_abort(); });
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
                                start_read();
                              } else {
                                handle_error(error);
                              }
                            });
  }

  /**
   * @brief 开始从对端读取消息
   *
   */
  void start_read() {
    auto self(this->shared_from_this());
    socket_.async_read_some(
        streambuf_.prepare(
            (streambuf_.size() + kChunkSize > streambuf_.max_size())
                ? (streambuf_.max_size() - streambuf_.size())
                : (kChunkSize)),
        asio::bind_executor(read_strand_,
                            [this, self](asio::error_code ec, size_t n) {
                              if (ec) {
                                handle_error(ec);
                              } else {
                                streambuf_.commit(n);
                                parse_messages();
                                start_read();
                              }
                            }));
  }

  void parse_messages() {
    // [TODO] 暂时使用 ECHO 逻辑
    std::vector<char> msg(streambuf_.size());
    asio::buffer_copy(asio::buffer(msg), streambuf_.data(), streambuf_.size());
    streambuf_.consume(streambuf_.size());
    auto message = std::make_shared<std::vector<char>>(std::move(msg));
    auto self(this->shared_from_this());
    auto n = asio::write(socket_, asio::buffer(*message));
    asio::async_write(socket_, ,
                      asio::bind_executor(write_strand_, [this, self, message](
                                                             std::error_code ec,
                                                             std::size_t n) {
                        if (ec) {
                          handle_error(ec);
                        } else {
                          INFO("write {} bytes", n);
                        }
                      }));
  }

  /**
   * @brief 获取该 Session 的内存地址
   *
   * @return void*
   */
  const void* const memaddr() { return (void*)this; }

  /**
   * @brief 处理正常关闭的逻辑（只在 write_strand_ 中执行）
   *
   */
  void do_shutdown() {
    assert(closing_.load() == true);
    assert(write_strand_.running_in_this_thread());

    // 正常关闭的过程中被要求异常关闭，交给 do_abort() 处理
    if (aborted_.load() || closed_.load()) return;

    // 1. [TODO] 待 server_contexts_ 完善后处理相关的逻辑

    // 2. 处理 socket 关闭的逻辑，SslSession 需要先完成 TLS 挥手
    if constexpr (IsSslStream<Socket>) {
      // TLS 挥手需要等待对方回复 close_notify，对方一直不回复则超时后直接断开
      auto self(this->shared_from_this());
      shutdown_timer_.expires_after(kShutdownTimeout);
      shutdown_timer_.async_wait(asio::bind_executor(
          write_strand_, [self](const std::error_code& error) {
            if (error) return;  // 挥手已经结束，定时器被取消
            WARN("Session {} tls shutdown timeout", self->memaddr());
            self->do_abort();
          }));

      // 取消还在进行的读写，避免和 TLS 挥手重叠
      std::error_code ignored;
      socket_.lowest_layer().cancel(ignored);

      socket_.async_shutdown(asio::bind_executor(
          write_strand_, [self](const std::error_code& error) {
            self->shutdown_timer_.cancel();
            if (error) {
              if (self->closed_.load()) return;  // 已经被异常关闭打断
              WARN("Session {} tls shutdown failed: {}", self->memaddr(),
                   error.message());
              self->do_abort();
              return;
            }
            self->close_socket(true);
          }));
    } else {
      close_socket(true);
    }
  }

  /**
   * @brief 处理异常关闭的逻辑（只在 write_strand_ 中执行）
   *
   */
  void do_abort() {
    assert(write_strand_.running_in_this_thread());

    // 1. [TODO] 待 server_contexts_ 完善后处理相关的逻辑

    // 2. 不做 TLS 挥手，直接断开 socket
    close_socket(false);
  }

  /**
   * @brief 断开 socket（关闭 Session 的最后一步，只会断开一次）
   *
   * @param graceful true 表示正常关闭（TLS 挥手已完成 / FIN 已发送），
   *                 false 表示异常关闭，直接断开
   */
  void close_socket(bool graceful) {
    assert(write_strand_.running_in_this_thread());
    if (closed_.exchange(true)) return;

    std::error_code _;  // 忽略过程的所有错误
    shutdown_timer_.cancel();

    auto shutdown_type = graceful ? asio::socket_base::shutdown_send
                                  : asio::socket_base::shutdown_both;

    if constexpr (IsSslStream<Socket>) {
      socket_.lowest_layer().cancel(_);
      socket_.lowest_layer().shutdown(shutdown_type, _);
      socket_.lowest_layer().close(_);
    } else {
      socket_.cancel(_);
      socket_.shutdown(shutdown_type, _);
      socket_.close(_);
    }
  }

  /**
   * @brief 错误处理的逻辑
   *
   */
  void handle_error(const std::error_code& error) {
    if (error == asio::error::eof) {
      // 对方先发起了正常的关闭(close_notify/FIN)，回一个正常的关闭
      INFO("Session {} peer closed connection", memaddr());
      Shutdown();
    } else if (error == asio::error::operation_aborted) {
      // Session 正在关闭，读写被取消属于预期情况
    } else {
      ERROR("Session {} error: {}", memaddr(), error.message());
      Abort();
    }
  }

 private:
  ServerContexts& server_contexts_;
  Socket socket_;

  // 读 strand 用来进行读操作
  asio::strand<asio::any_io_executor> read_strand_;
  asio::streambuf streambuf_;

  // 写 strand 用来进行写操作
  asio::strand<asio::any_io_executor> write_strand_;

  // 当出现错误或者异常时，置位 closing_
  std::atomic_bool closing_{false};

  // 当出现错误需要直接断开 socket 时，置位 aborted_
  std::atomic_bool aborted_{false};

  // 当 socket 已经断开时，置位 closed_
  std::atomic_bool closed_{false};

  // 正常关闭时，等待 TLS 挥手完成的定时器
  asio::steady_timer shutdown_timer_;
};

using SslSession = Session<asio::ssl::stream<asio::ip::tcp::socket>>;
using NosslSession = Session<asio::ip::tcp::socket>;

#endif  // !BECHAT_CORE_SESSION_H_
