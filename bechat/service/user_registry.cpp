#include "bechat/service/user_registry.h"

#include <cstring>

#include "bechat/proto/status_code.h"
#include "bechat/utils/logger.h"

int UserRegisty::Signup(const std::string& username,
                        const std::string& password) {
  try {
    std::lock_guard guard(user_records_mtx_);

    // 1. 查找 user 是否已经注册
    auto it = user_records_.find(username);
    if (it != user_records_.end()) return BECHAT_STATUS_SIGNUP_FAIL;

    // 2. 计算 password 哈希
    std::string hash(crypto_pwhash_STRBYTES, '\0');
    auto hash_err = crypto_pwhash_str(
        hash.data(), password.c_str(), password.length(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);
    if (hash_err) return BECHAT_STATUS_INTERNAL_ERROR;
    hash.resize(std::strlen(hash.c_str()));

    // 3. 存储用户记录
    UserRecord rec(username, std::move(hash));
    user_records_[username] = std::move(rec);

    // 4. 返回状态码
    return BECHAT_STATUS_SUCCESS;
  } catch (const std::exception& e) {
    ERROR("UserRegisty::Signup error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}

int UserRegisty::Verify(const std::string& username,
                        const std::string& password) {
  try {
    std::lock_guard guard(user_records_mtx_);

    // 1. 查找 user 是否已经注册
    auto it = user_records_.find(username);
    if (it == user_records_.end()) return BECHAT_STATUS_LOGIN_FAIL;
    auto&& rec = it->second;

    // 2. 对比 password 哈希
    auto verify_ret = crypto_pwhash_str_verify(
        rec.password_hash.c_str(), password.c_str(), password.length());
    if (verify_ret != 0) return BECHAT_STATUS_LOGIN_FAIL;

    // 3. 返回状态码
    return BECHAT_STATUS_SUCCESS;
  } catch (const std::exception& e) {
    ERROR("UserRegisty::Verify error: {}", e.what());
    return BECHAT_STATUS_INTERNAL_ERROR;
  }
}
