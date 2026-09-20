#ifndef BECHAT_SERVICE_USER_REGISTRY_H_
#define BECHAT_SERVICE_USER_REGISTRY_H_

#include <sodium.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "bechat/core/session_handle.h"
#include "bechat/utils/config.h"

struct UserRecord {
  std::string username;
  std::string password_hash;

  UserRecord(std::string user_name, std::string passwd_hash)
      : username(std::move(user_name)), password_hash(std::move(passwd_hash)) {}
};

class UserRegisty {
 public:
  static constexpr uint16_t kMaxUsernameSize{32};

 public:
  UserRegisty();

 public:
  /**
   * @brief 注册用户
   *
   * @param username 用户名
   * @param password 密码
   * @return 状态码
   */
  uint32_t Signup(const std::string& username, const std::string& password);

  /**
   * @brief 登录用户
   *
   * @param username
   * @param password
   * @param session
   * @param [out] tokens
   * @return 状态码
   */
  uint32_t Login(const std::string& username, const std::string& password,
                 std::shared_ptr<SessionHandle> session,
                 std::pair<std::string, std::string>* tokens = nullptr);

  /**
   * @brief 验证用户
   *
   * @param session
   * @param access_token
   * @return 状态码
   */
  uint32_t Verify(std::shared_ptr<SessionHandle> session,
                  const std::string& access_token);

  /**
   * @brief 更新用户 access_token
   *
   * @param session
   * @param refresh_token
   * @param [out] access_token
   * @return 状态码
   */
  uint32_t Refresh(std::shared_ptr<SessionHandle> session,
                   const std::string& refresh_token, std::string* access_token);

 private:
  void record_append_file(const UserRecord& rec);
  void record_read_file(const std::string& path);

 private:
  std::unordered_map<std::string /* username */, UserRecord> user_records_{};
  std::mutex user_records_mtx_{};
  std::string user_records_file_{Config::CfgPath() + "/user_records.db"};
};

#endif  // !BECHAT_SERVICE_USER_REGISTRY_H_
