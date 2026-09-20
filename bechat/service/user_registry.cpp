#include "bechat/service/user_registry.h"

#include <jwt-cpp/jwt.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "bechat/proto/status_code.h"
#include "bechat/utils/config.h"
#include "bechat/utils/logger.h"

static std::string get_access_token_now(SessionHandle& session) {
  auto now = std::chrono::system_clock::now();
  std::string access_token =
      jwt::create()
          .set_type("JWT")
          .set_issuer("BeChat")
          .set_subject(session.Username())
          .set_audience("bechat")
          .set_issued_at(now)
          .set_not_before(now)
          .set_expires_at(now + std::chrono::minutes(10))
          .set_id(std::to_string(session.Id()))
          .set_payload_claim("use", jwt::claim(std::string{"access"}))
          .set_payload_claim("uname", jwt::claim(session.Username()))
          .set_payload_claim("ver", jwt::claim(CFG_VERSION))
          .sign(jwt::algorithm::hs256{CFG_JWT_SECRET_ACCESS});
  return access_token;
}

static std::string get_refresh_token_now(SessionHandle& session) {
  auto now = std::chrono::system_clock::now();
  std::string refresh_token =
      jwt::create()
          .set_type("JWT")
          .set_issuer("BeChat")
          .set_subject(session.Username())
          .set_audience("bechat")
          .set_issued_at(now)
          .set_not_before(now)
          .set_expires_at(now + std::chrono::days(30))
          .set_id(session.Username())
          .set_payload_claim("use", jwt::claim(std::string{"refresh"}))
          .set_payload_claim("uname", jwt::claim(session.Username()))
          .set_payload_claim("ver", jwt::claim(CFG_VERSION))
          .sign(jwt::algorithm::hs256{CFG_JWT_SECRET_REFRESH});
  return refresh_token;
}

UserRegisty::UserRegisty() {
  // Read persistent storage
  record_read_file(user_records_file_);
}

uint32_t UserRegisty::Signup(const std::string& username,
                             const std::string& password) {
  try {
    std::lock_guard guard(user_records_mtx_);

    // Check username is invalid
    if (username.empty()) return BECHAT_STATUS_INVALID_PARAMS;
    if (username.size() > kMaxUsernameSize) return BECHAT_STATUS_INVALID_PARAMS;

    // Check username is registered
    auto it = user_records_.find(username);
    if (it != user_records_.end()) return BECHAT_STATUS_EXISTED_USER;

    // Compute password hash
    std::string hash(crypto_pwhash_STRBYTES, '\0');
    auto hash_err = crypto_pwhash_str(
        hash.data(), password.c_str(), password.length(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);
    if (hash_err) return BECHAT_STATUS_INTERNAL_ERROR;
    hash.resize(std::strlen(hash.c_str()));

    // Store user record
    UserRecord record(username, std::move(hash));
    auto ret = user_records_.try_emplace(username, record);
    if (!ret.second) return BECHAT_STATUS_EXISTED_USER;

    // Persistent storage
    record_append_file(record);

    return BECHAT_STATUS_SUCCESS;
  } catch (const std::exception& e) {
    ERROR("UserRegisty::Signup error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}

uint32_t UserRegisty::Login(const std::string& username,
                            const std::string& password,
                            std::shared_ptr<SessionHandle> session,
                            std::pair<std::string, std::string>* tokens) {
  try {
    std::lock_guard guard(user_records_mtx_);

    // Check username is registered
    auto it = user_records_.find(username);
    if (it == user_records_.end()) return BECHAT_STATUS_LOGIN_FAIL;
    auto&& rec = it->second;

    // Check password hash
    auto verify_err = crypto_pwhash_str_verify(
        rec.password_hash.c_str(), password.c_str(), password.length());
    if (verify_err) return BECHAT_STATUS_LOGIN_FAIL;

    // Generate JWT
    if (tokens) {
      // [TODO] restore refresh_token
      std::string refresh_token = get_refresh_token_now(*session);
      tokens->first = std::move(refresh_token);
      std::string access_token = get_access_token_now(*session);
      tokens->second = std::move(access_token);
    }

    // Set session authorized
    session->SetAuthorized(username);
    return BECHAT_STATUS_SUCCESS;

  } catch (const std::exception& e) {
    ERROR("UserRegisty::Login error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}

uint32_t UserRegisty::Verify(std::shared_ptr<SessionHandle> session,
                             const std::string& access_token) {
  try {
    auto decoded_token = jwt::decode(access_token);
    std::error_code ec;
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{CFG_JWT_SECRET_ACCESS})
        .leeway(30)
        .with_issuer("BeChat")
        .with_audience("bechat")
        .with_subject(session->Username())
        .with_id(std::to_string(session->Id()))
        .with_claim("use", jwt::claim(std::string{"access"}))
        .with_claim("uname", jwt::claim(session->Username()))
        .verify(decoded_token, ec);

    if (!ec) {
      return BECHAT_STATUS_SUCCESS;
    }

    session->SetUnauthorized();
    switch (static_cast<jwt::error::token_verification_error>(ec.value())) {
      case jwt::error::token_verification_error::token_expired:
        return BECHAT_STATUS_EXPIRED_ACCESS_TOKEN;

      default:
        return BECHAT_STATUS_INVALID_ACCESS_TOKEN;
    }

  } catch (const std::exception& e) {
    ERROR("UserRegisty::Verify error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}

uint32_t UserRegisty::Refresh(std::shared_ptr<SessionHandle> session,
                              const std::string& refresh_token,
                              std::string* access_token) {
  try {
    auto decoded_token = jwt::decode(refresh_token);
    std::error_code ec;
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{CFG_JWT_SECRET_REFRESH})
        .leeway(30)
        .with_issuer("BeChat")
        .with_audience("bechat")
        .with_subject(session->Username())
        .with_id(session->Username())
        .with_claim("use", jwt::claim(std::string{"refresh"}))
        .with_claim("uname", jwt::claim(session->Username()))
        .verify(decoded_token, ec);

    if (!ec) {
      if (access_token) *access_token = get_access_token_now(*session);
      return BECHAT_STATUS_SUCCESS;
    }

    session->SetUnauthorized();
    switch (static_cast<jwt::error::token_verification_error>(ec.value())) {
      case jwt::error::token_verification_error::token_expired:
        return BECHAT_STATUS_EXPIRED_REFRESH_TOKEN;

      default:
        return BECHAT_STATUS_INVALID_REFRESH_TOKEN;
    }

  } catch (const std::exception& e) {
    ERROR("UserRegisty::Verify error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}

void UserRegisty::record_append_file(const UserRecord& rec) {
  std::ofstream out(user_records_file_, std::ios::app);
  if (!out) {
    CRITICAL("Failed save UserRecord(username: {}) to persistent storage",
             rec.username);
    return;
  }
  out << rec.username << ' ' << rec.password_hash << '\n';
}

void UserRegisty::record_read_file(const std::string& path) {
  if (!std::filesystem::exists(path)) return;
  std::ifstream in(path);
  if (!in) {
    CRITICAL("Failed read UserRecord from persistent storage \"{}\"", path);
    return;
  }
  std::string line{};
  std::string name{}, hash{};
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    iss >> name >> hash;
    {
      std::lock_guard g(user_records_mtx_);
      user_records_.try_emplace(name, name, hash);
    }
  }
}
