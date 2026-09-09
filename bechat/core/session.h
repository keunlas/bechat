#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
#include <asio/ssl.hpp>
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
      : server_contexts_(server_contexts), socket_(std::move(socket)) {}

  void Start() {
    if constexpr (IsSslStream<Socket>) {
      auto self(this->shared_from_this());
      socket_.async_handshake(asio::ssl::stream_base::server,
                              [this, self](const std::error_code& error) {
                                if (!error) {
                                  INFO("Session {} is a ssl socket",
                                       (void*)this);
                                } else {
                                  ERROR("handshake: {}", error.message());
                                }
                              });
    } else {
      INFO("Session {} is a nossl socket", (void*)this);
    }
  }

 private:
  ServerContexts& server_contexts_;
  Socket socket_;
};

using SslSession = Session<asio::ssl::stream<asio::ip::tcp::socket>>;
using NosslSession = Session<asio::ip::tcp::socket>;

#endif  // !BECHAT_CORE_SESSION_H_
