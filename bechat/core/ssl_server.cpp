#include "bechat/core/ssl_server.h"

#include "bechat/utils/config.h"

SslServer::SslServer(IoContexts& io_contexts, const std::string& ip,
                     uint16_t port)
    : io_contexts_(io_contexts),
      ssl_context_(asio::ssl::context::sslv23_server),
      endpoint_(asio::ip::make_address(ip), port),
      acceptor_(io_contexts_.GetIoContext(), endpoint_) {
  INFO("SslServer has running in {}:{}", ip, port);
  ssl_configure();
  start_accept();
}

void SslServer::ssl_configure() {
  ssl_context_.set_options(
      asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
      asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 |
      asio::ssl::context::no_tlsv1_1 | asio::ssl::context::no_compression |
      asio::ssl::context::single_dh_use);
  ssl_context_.use_certificate_chain_file(CFG_SSL_CERT);
  ssl_context_.use_private_key_file(CFG_SSL_KEY, asio::ssl::context::pem);
  ssl_context_.use_tmp_dh_file(CFG_SSL_DH);
}

void SslServer::start_accept() {
  acceptor_.async_accept(
      [this](const std::error_code& error, asio::ip::tcp::socket socket) {
        if (!error) {
          std::make_shared<Session<asio::ssl::stream<asio::ip::tcp::socket>>>(
              asio::ssl::stream<asio::ip::tcp::socket>(std::move(socket),
                                                       ssl_context_))
              ->Start();
        } else {
          ERROR("Server accept error occurred: {}", error.message());
        }
        start_accept();
      });
}
